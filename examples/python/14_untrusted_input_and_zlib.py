"""Untrusted input: size limits decided from the header, typed errors, and zlib with the same bound.

    python 14_untrusted_input_and_zlib.py

The hostile files are built here (fake_png); the good one is a picture of data/.
"""
import struct
import zlib
from pathlib import Path

import libstb as stb

DATA = Path(__file__).resolve().parents[2] / "data"        # the sample pictures and fonts (see data/README.md)


def fake_png(width, height):
    """A PNG whose header says width x height and which has no pixels behind it: a malicious upload."""

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)     # 8 bits, RGB, no interlace
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(b"")) + chunk(b"IEND", b"")


def try_open(what, data, **options):
    """Image.open of a file's bytes: what happened is an exception you can name."""
    try:
        img = stb.Image.open(data, **options)
        print(f"{what}: decoded {img.width}x{img.height}")
    except stb.LimitError as e:
        print(f"{what}: LimitError ({e})")
    except stb.DecodeError as e:
        print(f"{what}: DecodeError ({e})")


# 1. A whole file in memory. The format is found from the bytes, not from a name.
png = (DATA / "images" / "chelsea.png").read_bytes()
try_open("a real png", png)

# 2. Header only: the size and format, with no pixel memory at all.
bomb = fake_png(15000, 15000)                                         # would be 675 MB of RGB
print("the header of the bomb says:", stb.iminfo(bomb), "(read without decoding anything)")

# 3. Limits are checked from that header, BEFORE any pixel memory exists. The default is stb.DEFAULT_MAX_BYTES.
print("default limit:", stb.DEFAULT_MAX_BYTES >> 20, "MiB")
try_open("the bomb, default limit", bomb)
try_open("a real png, 1 MiB limit", png, max_bytes=1 << 20)           # 451 * 300 * 3 bytes: fits
try_open("a real png, 1 KiB limit", png, max_bytes=1 << 10)           # does not

# 4. Typed errors for what is not an image, or not all of one. They all derive from stb.Error.
try_open("garbage", bytes(100))
try_open("half a png", png[: len(png) // 2])
try:
    stb.Image.open("no/such/file.png")
except OSError as e:                                                  # FileNotFoundError is an OSError
    print("missing file:", type(e).__name__)
print("stb.LimitError is an stb.Error:", issubclass(stb.LimitError, stb.Error), "| so is DecodeError:", issubclass(stb.DecodeError, stb.Error))

# 5. imread, the numpy-in numpy-out function, takes the same limit.
try:
    stb.imread(bomb, max_bytes=64 << 20)
except stb.LimitError:
    print("imread of the bomb: LimitError")

# 6. zlib, with the same bound: a zip bomb is a LimitError, not an out-of-memory.
text = b"a" * 100_000
packed = stb.zlib_compress(text, level=9)
print("zlib:", len(text), "->", len(packed), "->", len(stb.zlib_decompress(packed, max_bytes=1 << 20)), "bytes")
zip_bomb = stb.zlib_compress(bytes(64 << 20), level=9)                # 64 MiB of zeros packs to about 64 KiB
try:
    stb.zlib_decompress(zip_bomb, max_bytes=1 << 20)                  # says no after 1 MiB
except stb.LimitError as e:
    print(f"zip bomb of {len(zip_bomb)} bytes: LimitError ({e})")
