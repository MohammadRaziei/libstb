"""
bench_throughput.py: speed of every operation in runners.py, across
every corpus entry, for every library that is installed.

Each (library, corpus entry, operation) cell is repeated REPEATS times
(configurable via --repeats / LIBSTB_BENCH_REPEATS, default 7) and the
best (min) wall-clock time is kept: standard practice for
micro-benchmarks, since it is the closest a single run gets to "no
other process happened to interrupt this one". One untimed call runs
first, both to warm up and to record what the operation produced
(output size in bytes for encoders, output shape for the rest).

What each operation means, and why each pairing is fair, is written
down once, in runners.py's module docstring: this script only loops.
A library that cannot do an operation (OpenCV has no TGA codec,
scikit-image has no box filter) is recorded as unavailable with the
reason, and shows up in the report as a gap rather than being dropped
silently.
"""
import gc
import json
import sys
import time

import runners

try:
    from tqdm import tqdm
except ImportError:
    def tqdm(iterable, **kwargs):
        return iterable

DEFAULT_REPEATS = 7

ENTRY_KEYS = ("genre", "size", "format", "bytes", "width", "height", "channels", "pixels", "canonical")


def _best_time(fn, repeats):
    best = None
    for _ in range(repeats):
        gc.collect()
        t0 = time.perf_counter()
        fn()
        dt = time.perf_counter() - t0
        if best is None or dt < best:
            best = dt
    return best


def _cell(op, lib, inp, repeats):
    try:
        fn = runners.prepare(op, lib, inp)
        out = fn()  # untimed: warm-up, and proof the operation works at all
        info = runners.describe(out)
        del out
        best = _best_time(fn, repeats)
    except Exception as e:  # noqa: BLE001 - any failure is a reportable gap
        return {"available": False, "error": f"{type(e).__name__}: {e}"}
    cell = {"available": True, "seconds": best}
    cell.update(info)
    return cell


def run(manifest, repeats=DEFAULT_REPEATS):
    results = []
    for entry in tqdm(manifest, desc="bench_throughput", unit="file"):
        inp = runners.Inputs(entry)
        row = {k: entry.get(k) for k in ENTRY_KEYS}
        for op, libs in runners.OPS.items():
            if not runners.applies(op, entry):
                continue
            row[op] = {lib: _cell(op, lib, inp, repeats) for lib in libs if runners.library_available(lib)}
        results.append(row)

    return {
        "operation_notes": {
            "info": "read width/height/channels from the header, no pixel decoding",
            "decode": "encoded bytes in, uint8 pixel array out",
            "load_file": "path in, uint8 pixel array out (file I/O included)",
            "encode_png": f"raw pixels in, PNG bytes out (zlib level {runners.PNG_LEVEL})",
            "encode_jpg": f"raw pixels in, JPEG bytes out (quality {runners.JPEG_QUALITY})",
            "encode_bmp": "raw pixels in, BMP bytes out",
            "encode_tga": "raw pixels in, uncompressed TGA bytes out",
            "resize": "half size (down) or double size (up), 3-channel images",
        },
        "repeats": repeats,
        "metric": "best (min) wall-clock seconds over repeats",
        "results": results,
    }


def load_manifests(paths):
    manifest = []
    for m_path in paths:
        with open(m_path, "r", encoding="utf-8") as f:
            manifest.extend(json.load(f))
    return manifest


if __name__ == "__main__":
    import argparse

    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("paths", nargs="+",
                   help="one or more JSON manifests from corpus.py / real_corpus_manifest.py "
                        "(merged together), followed by the path to write the results JSON to")
    p.add_argument("--repeats", type=int, default=DEFAULT_REPEATS,
                   help=f"timed repeats per cell; best (min) is kept (default: {DEFAULT_REPEATS})")
    args = p.parse_args()

    if len(args.paths) < 2:
        p.error("need at least one manifest and an output path")
    *manifest_paths, output = args.paths
    result = run(load_manifests(manifest_paths), repeats=args.repeats)

    with open(output, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    ops = [op for op in runners.OPS if any(op in row for row in result["results"])]
    libs = {lib for row in result["results"] for op in ops for lib in row.get(op, {})}
    print(f"bench_throughput: {len(result['results'])} corpus entries, {len(libs)} libraries, "
          f"{len(ops)} operations -> {output}", file=sys.stderr)
