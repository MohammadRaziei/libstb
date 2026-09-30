"""
bench_op_memory_one.py: measure peak memory for ONE (operation, library,
corpus entry) combination, in THIS process, and print one JSON line to
stdout. This is the "worker" half of the pair with
bench_op_memory_driver.py: it never reads the manifest, never loops,
never generates data. It is handed exactly one entry and does exactly
one thing, so it is safe to spawn fresh (see the ru_maxrss/execve
gotcha in benchmarks/README.md).

What is reported, and why two numbers:

  baseline_rss_mb   the high-water mark AFTER the library is imported and
                    the input is loaded and the library's own native
                    image object is built (all of that is runners.prepare,
                    which is not the operation), i.e. everything the
                    operation starts with.
  peak_rss_mb       the high-water mark after running the operation.
  delta_mb          peak - baseline: the memory the operation itself
                    needed on top of that, the number the report charts.
                    The absolute peak alone would mostly measure how
                    heavy each library is to import (OpenCV alone is
                    tens of MB), which says nothing about a 2 MP resize.

The operation runs REPEATS times before peak is read: ru_maxrss is a
whole-process high-water mark, so repeating can only ever report the
same or a higher peak, never an artificially low one.
"""
import argparse
import gc
import json
import resource
import sys

import runners

REPEATS = 3


def _rss_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0  # Linux: KiB


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("operation")
    p.add_argument("library")
    p.add_argument("entry", help="the corpus entry, as a JSON object")
    args = p.parse_args()

    entry = json.loads(args.entry)
    result = {"operation": args.operation, "library": args.library, "bytes": entry.get("bytes")}
    try:
        fn = runners.prepare(args.operation, args.library, runners.Inputs(entry))
        gc.collect()
        baseline = _rss_mb()
        for _ in range(REPEATS):
            out = fn()
            del out
        peak = _rss_mb()
        result.update(available=True, baseline_rss_mb=baseline, peak_rss_mb=peak, delta_mb=max(0.0, peak - baseline))
    except Exception as e:  # noqa: BLE001 - an unsupported operation is a reportable gap
        result.update(available=False, error=f"{type(e).__name__}: {e}")

    print(json.dumps(result))


if __name__ == "__main__":
    sys.exit(main())
