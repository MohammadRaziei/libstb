"""
bench_op_memory_driver.py: orchestrate bench_op_memory_one.py across
every (corpus entry, operation, library) combination and combine the
results into throughput_memory.json.

This driver is a single CMake step (not one target per data point)
specifically BECAUSE it never touches the image data itself: it reads
only the manifest's metadata (genre, size, path, byte count) and hands
each entry to a freshly spawned child via subprocess.run. The same
"spawning process must stay unspoiled" rule documented in
benchmarks/README.md: on Linux ru_maxrss is NOT reset by execve(), so a
child spawned by a process that had already inflated itself with pixel
data would report that inflation as its own peak. The driver stays a
few MB, so every child starts from a clean floor.

Not everything is measured, on purpose:
  - images under MIN_PIXELS: the whole operation fits inside the noise
    of the allocator, so the delta would be measuring nothing.
  - decode / load_file only on the png and jpg files: bmp and tga are
    uncompressed, so their memory story is "the file size".
  - info: it allocates nothing worth measuring.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import runners

WORKER = Path(__file__).with_name("bench_op_memory_one.py")

MIN_PIXELS = 256 * 256
SKIPPED_OPS = {"info"}
PNG_JPG_ONLY_OPS = {"decode", "load_file"}

ENTRY_KEYS = ("genre", "size", "format", "bytes", "width", "height", "channels", "pixels")


def wanted(op, entry):
    if op in SKIPPED_OPS or entry["pixels"] < MIN_PIXELS:
        return False
    if op in PNG_JPG_ONLY_OPS and entry["format"] not in ("png", "jpg"):
        return False
    return runners.applies(op, entry)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("paths", nargs="+",
                   help="one or more JSON manifests (merged), followed by the path to write throughput_memory.json to")
    args = p.parse_args()
    if len(args.paths) < 2:
        p.error("need at least one manifest and an output path")
    *manifest_paths, output = args.paths

    manifest = []
    for m_path in manifest_paths:
        with open(m_path, "r", encoding="utf-8") as f:
            manifest.extend(json.load(f))  # metadata only: paths and byte counts, never pixels

    plan = [(op, entry, lib) for op, libs in runners.OPS.items() for entry in manifest
            if wanted(op, entry) for lib in libs if runners.library_available(lib)]
    result = {}
    done = 0
    for op in runners.OPS:
        rows = {}
        for o, entry, lib in plan:
            if o != op:
                continue
            key = (entry["genre"], entry["size"], entry["format"])
            row = rows.setdefault(key, {**{k: entry.get(k) for k in ENTRY_KEYS}, "libraries": {}})
            proc = subprocess.run([sys.executable, str(WORKER), op, lib, json.dumps(entry)],
                                  capture_output=True, text=True, check=True)
            row["libraries"][lib] = json.loads(proc.stdout)
            done += 1
        if rows:
            result[op] = list(rows.values())
            print(f"bench_op_memory_driver: {op} done ({done}/{len(plan)})", file=sys.stderr)

    with open(output, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    print(f"bench_op_memory_driver: wrote {output}", file=sys.stderr)


if __name__ == "__main__":
    main()
