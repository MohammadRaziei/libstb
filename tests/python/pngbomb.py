"""A genuine PNG "decompression bomb" for the tests: a valid file of about half a megabyte whose
header declares an image of more than half a gigabyte once decoded.

Nothing is committed: the file is built on demand. The pixel data is all zeros, which deflate
shrinks by roughly 1000 to 1, so the pixel stream is real and complete (zlib can inflate it to
exactly the size the header promises), it is just very repetitive. That is the shape of a real
attack: a small upload that asks the decoder for gigabytes.
"""

import struct
import zlib

WIDTH = HEIGHT = 12000
CHANNELS = 4                                    # RGBA, colour type 6
DECODED_BYTES = WIDTH * HEIGHT * CHANNELS       # 576,000,000: above the 512 MiB default limit


def _chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def scanline_stream_size(width, height, channels):
    """Bytes in the raw PNG stream: one filter byte plus the pixels of every row."""
    return height * (1 + width * channels)


def make_png(width, height, channels=4):
    """A valid 8-bit PNG of `width` x `height` (zeros), cheap to build even when it is huge."""
    color_type = {1: 0, 2: 4, 3: 2, 4: 6}[channels]
    zeros = bytes(1 << 20)                      # 1 MiB of zeros, reused for the whole stream
    left = scanline_stream_size(width, height, channels)
    comp = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_RLE)
    parts = []
    while left > 0:
        n = min(left, len(zeros))
        parts.append(comp.compress(zeros if n == len(zeros) else zeros[:n]))
        left -= n
    parts.append(comp.flush())
    return (b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
            + _chunk(b"IDAT", b"".join(parts))
            + _chunk(b"IEND", b""))


def make_bomb():
    return make_png(WIDTH, HEIGHT, CHANNELS)
