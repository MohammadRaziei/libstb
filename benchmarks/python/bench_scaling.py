"""
bench_scaling.py: time vs image size, not just a handful of fixed sizes.

bench_throughput.py compares libraries at each corpus entry; this asks
the follow-up question a per-entry table cannot answer cleanly: does
the *slope* change? A decoder can win at 256x256 (everything fits in
cache, setup cost dominates) and lose at 4096x4096 (memory bandwidth
dominates), or the reverse. So one photo-genre image is generated at
each side length from 128 up to 4096 pixels (0.016 to 16.8 MP) and a
representative operation of each kind is timed on it:

  decode (png)      the format where compressibility matters most
  decode (jpg)      the format where the DCT matters most
  encode_png        the slow direction for every library
  encode_jpg
  resize_down_cubic the most common resize

for the three libraries that can do all of them (libstb, Pillow,
OpenCV). Images are built with the same runners.py callables as the
throughput benchmark, from a raw .npy plus a Pillow-encoded file, so
"decode a PNG" is the identical operation here and there.

Best of REPEATS runs per point, same as everywhere else in this suite.
The report plots throughput (megapixels per second) against image
size, both log scale: a library whose line is flat is scaling linearly,
a falling line is paying more per pixel as images grow.
"""
import argparse
import gc
import json
import os
import sys
import tempfile
import time

import numpy as np

import runners
from corpus import gen_photo, manifest_entries

try:
    from tqdm import tqdm
except ImportError:
    def tqdm(iterable, **kwargs):
        return iterable

SIDES = [128, 256, 512, 1024, 2048, 4096]
LIBRARIES = ["libstb", "pillow", "opencv"]
# (label, operation, entry format to run it on)
CASES = [
    ("decode_png", "decode", "png"),
    ("decode_jpg", "decode", "jpg"),
    ("encode_png", "encode_png", "png"),
    ("encode_jpg", "encode_jpg", "png"),
    ("resize_down_cubic", "resize_down_cubic", "png"),
]
REPEATS = 3


def _best(fn, repeats):
    best = None
    for _ in range(repeats):
        gc.collect()
        t0 = time.perf_counter()
        fn()
        dt = time.perf_counter() - t0
        best = dt if best is None or dt < best else best
    return best


def run(tmp_dir, sides=SIDES, repeats=REPEATS):
    result = {label: {lib: {"available": True, "points": []} for lib in LIBRARIES if runners.library_available(lib)}
              for label, _, _ in CASES}

    for side in tqdm(sides, desc="bench_scaling", unit="size"):
        arr = gen_photo(side, side)
        entries = {e["format"]: e for e in manifest_entries("photo", f"{side}x{side}", arr,
                                                            os.path.join(tmp_dir, f"scaling_{side}"),
                                                            formats=("png", "jpg"))}
        del arr
        for label, op, fmt in CASES:
            entry = entries[fmt]
            # the raw .npy only hangs off the png entry; jpg decode does not need it
            inp = runners.Inputs(entry)
            for lib, series in result[label].items():
                if not series["available"]:
                    continue
                try:
                    fn = runners.prepare(op, lib, inp)
                    fn()  # untimed warm-up
                    seconds = _best(fn, repeats)
                except Exception as e:  # noqa: BLE001
                    result[label][lib] = {"available": False, "error": f"{type(e).__name__}: {e}"}
                    continue
                series["points"].append({"side": side, "pixels": side * side,
                                         "bytes": entry["bytes"], "seconds": seconds})
        for e in entries.values():
            for path in (e["path"], e.get("raw")):
                if path and os.path.exists(path):
                    os.remove(path)
    return {"repeats": repeats, "metric": "best (min) wall-clock seconds over repeats",
            "sides": list(sides), "cases": result}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("output", help="path to write results JSON")
    p.add_argument("--tmp-dir", default=None, help="scratch dir for generated images (default: a temp dir)")
    p.add_argument("--repeats", type=int, default=REPEATS)
    args = p.parse_args()

    tmp_dir = args.tmp_dir or tempfile.mkdtemp(prefix="libstb_bench_scaling_")
    os.makedirs(tmp_dir, exist_ok=True)

    result = run(tmp_dir, repeats=args.repeats)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    print(f"bench_scaling: wrote {args.output}", file=sys.stderr)
