"""
verify.py: are the benchmarked operations actually doing the same job?

A speed number for something that produces the wrong output is worse
than no number. Before anything is timed, this script runs every
(operation, library) pair from runners.py on a few tiny NON-SQUARE
images (so a swapped width/height cannot hide) and checks the output:

  decode / load_file   lossless formats (png, bmp, tga) must reproduce
                       the original pixels EXACTLY, channel order
                       included; jpg must agree with Pillow's decode to
                       within JPEG_NMAE (decoders round and upsample
                       chroma slightly differently).
  encode_*             the produced bytes are decoded by Pillow: exact
                       for png/bmp/tga; for jpg the loss against the
                       original may exceed Pillow's own by at most
                       JPEG_NMAE.
  resize_*             the output has exactly the requested shape and is
                       within RESIZE_NMAE of Pillow's result for the same
                       filter, on a smooth image (so OpenCV's
                       non-antialiased shrink stays under the limit).

Differences are measured as nmae, the mean absolute difference divided
by 255 (1e-4 is 0.0255 of one 8-bit level).
  info                 width, height and channels are right.

Failures exit non-zero, which stops the CMake build before any
benchmark runs. A library that cannot do an operation at all
(Unavailable) is not a failure: it is the same gap the report shows.
"""
import argparse
import io
import json
import os
import sys
import tempfile

import numpy as np
from PIL import Image

import runners
from corpus import gen_gradient, gen_graphics, gen_photo, manifest_entries

# One limit per kind of comparison, shared by every library (measured worst cases in brackets).
JPEG_NMAE = 5e-4    # decode vs Pillow [libstb 3.2e-4, the rest 0]; encode loss vs Pillow's [libstb -3.2e-5]
RESIZE_NMAE = 1e-2  # vs Pillow, same filter [libstb 2.0e-3, skimage 4.5e-3, OpenCV 6.6e-3 (no antialiasing)]


def _to_array(result, lib):
    if isinstance(result, np.ndarray):
        a = result
    elif hasattr(result, "array") and hasattr(result, "channels"):  # libstb.Image
        a = result.array
    else:  # PIL image
        a = np.asarray(result)
    if lib == "opencv" and a.ndim == 3:
        a = a[..., [2, 1, 0] if a.shape[2] == 3 else [2, 1, 0, 3]]
    return a


def _decode_bytes(data):
    return np.asarray(Image.open(io.BytesIO(data)))


def _mean_diff(a, b):
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def _nmae(a, b):
    return _mean_diff(a, b) / 255


def _check_decode(op, lib, entry, out, expected_rgb):
    a = _to_array(out, lib)
    fmt = entry["format"]
    ref = expected_rgb if fmt != "jpg" else np.asarray(Image.open(entry["path"]))
    if a.shape != ref.shape:
        return f"shape {a.shape} != {ref.shape}"
    if fmt == "jpg":
        d = _nmae(a, ref)
        return None if d <= JPEG_NMAE else f"nmae {d:.2e} vs Pillow's decode (limit {JPEG_NMAE:.0e})"
    d = _mean_diff(a, ref)
    return None if d == 0.0 else f"not pixel-exact (mean error {d:.3f})"


def _check_encode(op, entry, out, arr):
    fmt = op[len("encode_"):]
    if not isinstance(out, (bytes, bytearray)) or not out:
        return "did not return bytes"
    back = _decode_bytes(bytes(out))
    if back.shape != arr.shape:
        return f"round-trip shape {back.shape} != {arr.shape}"
    if fmt == "jpg":
        pillow_back = _decode_bytes(bytes(runners.prepare("encode_jpg", "pillow", runners.Inputs(entry))()))
        loss, pillow_loss = _nmae(back, arr), _nmae(pillow_back, arr)
        return None if loss <= pillow_loss + JPEG_NMAE else (
            f"round-trip nmae {loss:.2e} vs Pillow's {pillow_loss:.2e} (limit +{JPEG_NMAE:.0e})")
    d = _mean_diff(back, arr)
    return None if d == 0.0 else f"round-trip not pixel-exact (mean error {d:.3f})"


def _check_resize(op, lib, entry, out, arr):
    a = _to_array(out, lib)
    w, h = runners._resize_target(op, runners.Inputs(entry))
    if a.shape[:2] != (h, w):
        return f"output {a.shape[:2]} != requested {(h, w)}"
    ref = _to_array(runners.prepare(op, "pillow", runners.Inputs(entry))(), "pillow")
    d = _nmae(a, ref)
    return None if d <= RESIZE_NMAE else f"nmae {d:.2e} vs Pillow with the same filter (limit {RESIZE_NMAE:.0e})"


def run(tmp):
    entries = []
    for genre, gen, (h, w) in (("gradient", gen_gradient, (72, 104)),
                               ("photo", gen_photo, (80, 112)),
                               ("graphics", gen_graphics, (64, 96))):
        arr = gen(h, w)
        entries.extend(manifest_entries(genre, f"{w}x{h}", arr, os.path.join(tmp, f"{genre}_{w}x{h}")))

    results = []
    for entry in entries:
        inp = runners.Inputs(entry)
        # every format of one image shares one .npy, named after the file's base
        raw = np.load(entry["path"].rsplit(".", 1)[0] + ".npy")
        for op, libs in runners.OPS.items():
            if not runners.applies(op, entry):
                continue
            for lib in libs:
                if not runners.library_available(lib):
                    continue
                rec = {"op": op, "lib": lib, "entry": f"{entry['genre']}/{entry['size']}/{entry['format']}"}
                try:
                    out = runners.prepare(op, lib, inp)()
                except runners.Unavailable as e:
                    rec["status"] = "unavailable"
                    rec["detail"] = str(e)
                    results.append(rec)
                    continue
                except Exception as e:  # noqa: BLE001
                    rec["status"], rec["detail"] = "FAIL", f"{type(e).__name__}: {e}"
                    results.append(rec)
                    continue

                if op == "info":
                    info = runners.describe(out).get("out_shape", [])
                    want = [entry["height"], entry["width"], entry["channels"]]
                    err = None if info[:3] == want else f"got {info}, want {want}"
                elif op in ("decode", "load_file"):
                    # BMP and JPEG files of an RGBA image are RGB: alpha dropped, not composited
                    want = raw if raw.shape[2] == entry["channels"] else raw[..., :entry["channels"]]
                    err = _check_decode(op, lib, entry, out, want)
                elif op.startswith("encode_"):
                    err = _check_encode(op, entry, out, raw)
                else:
                    err = _check_resize(op, lib, entry, out, raw)
                rec["status"] = "ok" if err is None else "FAIL"
                if err:
                    rec["detail"] = err
                results.append(rec)
    return results


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("output", nargs="?", default=None, help="optional path to write the results JSON to")
    args = p.parse_args()

    with tempfile.TemporaryDirectory(prefix="libstb_bench_verify_") as tmp:
        results = run(tmp)

    ok = sum(r["status"] == "ok" for r in results)
    na = sum(r["status"] == "unavailable" for r in results)
    bad = [r for r in results if r["status"] == "FAIL"]
    for r in bad:
        print(f"FAIL  {r['op']:20s} {r['lib']:12s} {r['entry']:28s} {r.get('detail', '')}", file=sys.stderr)
    print(f"verify: {ok} checks passed, {na} unavailable (expected gaps), {len(bad)} failed", file=sys.stderr)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            json.dump({"passed": ok, "unavailable": na, "failed": len(bad), "failures": bad}, f, indent=2)
    sys.exit(1 if bad else 0)
