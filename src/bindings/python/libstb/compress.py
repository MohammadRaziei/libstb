"""zlib streams (RFC 1950), with libstb's compressor and stb_image's inflate.

The compressor is the one libstb uses for PNG output; both directions interoperate with python's
own `zlib` module and with any other zlib implementation.
"""

from __future__ import annotations

from typing import Union

from . import libstb_py as _native
from .libstb_py import DEFAULT_ZLIB_LEVEL, DEFAULT_ZLIB_MAX_BYTES

Buffer = Union[bytes, bytearray, memoryview]  # any contiguous bytes-like object works


def zlib_compress(data: Buffer, level: int = DEFAULT_ZLIB_LEVEL) -> bytes:
    """Compress `data` into a zlib stream.

    `level` uses zlib's numbering: 0 stores the data without compressing it, 1 is the fastest
    compression and 9 the smallest and slowest, -1 means the default (6). The numbers match zlib's
    but it is not the same algorithm: the default was 1.2x to 1.9x faster than zlib's level 6 on
    the data we measured (source code, a binary, image pixels) with output 0.4% to 3.3% larger;
    measure on your own.
    Empty input gives a valid stream. Releases the GIL while it runs.

    Raises ValueError (level outside -1..9), TypeError (not a bytes-like object),
    LimitError (input over 2 GiB).

    >>> stb.zlib_decompress(stb.zlib_compress(b"hello hello hello"))
    b'hello hello hello'
    """
    return _native.zlib_compress(data, level)


def zlib_decompress(data: Buffer, max_bytes: int = DEFAULT_ZLIB_MAX_BYTES) -> bytes:
    """Inflate one complete zlib stream.

    `max_bytes` (default 512 MiB) bounds the OUTPUT: a tiny stream can expand a thousand-fold, so
    memory is grown in steps and never past the limit, and a stream that would produce more raises
    LimitError. Nothing is reserved up front. The input must be exactly one stream: a bad header,
    corrupt or truncated data, a wrong Adler-32 checksum or trailing bytes raise DecodeError.
    Releases the GIL while it runs.

    This is stb_image's inflate: correct and bounded, but not faster than python's zlib (about the
    same on binary data, about 2.5x slower on very compressible text in our measurement).

    Raises DecodeError, LimitError, TypeError (not a bytes-like object).
    """
    return _native.zlib_decompress(data, max_bytes)
