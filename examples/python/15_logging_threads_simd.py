"""Logging, threads and SIMD: what libstb decided, and how to change it for one block.

    python 15_logging_threads_simd.py

You never have to set any of this: the fastest SIMD backend is picked at run time and threads are used only
where the work pays for them. When you do want control (a library, a thread pool, a diagnosis), a `with`
block changes ONE thread only, and the previous setting comes back when it ends.
"""
import logging
import logging.handlers
import time
from pathlib import Path

import libstb as stb

DATA = Path(__file__).resolve().parents[2] / "data"        # the sample pictures and fonts (see data/README.md)

# 1. libstb's own messages arrive in Python's logging: the logger is "libstb", with children such as
#    "libstb.simd". It is silent unless asked; at DEBUG it says what it decided on each call.
handler = logging.handlers.BufferingHandler(10_000)        # keeps the records it gets in handler.buffer
records = handler.buffer

logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
logging.getLogger("libstb").setLevel(logging.DEBUG)
logging.getLogger("libstb").addHandler(handler)

# 2. What this machine runs.
print("simd   :", stb.simd_name(), "of", stb.simd_backends())
print("threads:", stb.thread_count())

big = stb.Image.open(DATA / "images" / "astronaut.png", format=stb.PixelFormat.RGBA).resize(3000, 2000)
overlay = big.resize(2500, 1800)


def timed(what):
    """One blend (it has SIMD kernels) and one resize (it can use several threads), timed."""
    t0 = time.perf_counter()
    big.blend(overlay, 10, 10)
    blend_ms = (time.perf_counter() - t0) * 1000
    t0 = time.perf_counter()
    big.resize(750, 500)
    resize_ms = (time.perf_counter() - t0) * 1000
    print(f"{what:17s}: blend {blend_ms:6.1f} ms, resize {resize_ms:6.1f} ms | simd {stb.simd_name()}")


# 3. A block changes this thread only; the old values come back when it ends. They nest.
timed("automatic")
with stb.threads(1), stb.simd("scalar"):                 # no worker threads, the portable code
    timed("1 thread, scalar")
timed("automatic again")

# 4. The same records are plain logging records: filter them, send them to a file, count them.
print(f"libstb logged {len(records)} debug record(s) during the three rounds, for example:")
for r in records[:3]:
    print("   ", r.name, r.levelname, r.getMessage())

# 5. Quiet again: stop the bridge (the C++ side then does not even format the messages).
stb.logging_bridge(False)
before = len(records)
big.resize(75, 50)
print("with the bridge off, no new records:", len(records) == before)
stb.logging_bridge(True)
