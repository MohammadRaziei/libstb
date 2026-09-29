"""Image (native: it is stb::image) and the header-only ImageInfo."""

from __future__ import annotations

import os
from typing import NamedTuple, Union

from . import libstb_py as _native
from .libstb_py import DEFAULT_MAX_BYTES, Image

# Path, or the encoded image itself.
Source = Union[str, "os.PathLike[str]", bytes, bytearray, memoryview]


def _read(source: Source) -> bytes:
    """bytes-like -> the data itself; str / os.PathLike -> file contents.

    ponytail: used by ImageInfo and Font, which decode from memory. (Image
    opens paths natively; a missing file is a FileNotFoundError either way.)
    """
    if isinstance(source, bytes):
        return source
    if isinstance(source, (bytearray, memoryview)):
        return bytes(source)
    with open(source, "rb") as f:
        return f.read()


class ImageInfo(NamedTuple):
    """Header-only facts about an image file."""

    width: int
    height: int
    channels: int

    @classmethod
    def read(cls, source: Source) -> "ImageInfo":
        """Read just the header (no pixel decoding)."""
        return cls(*_native.info_bytes(_read(source)))


__all__ = ["DEFAULT_MAX_BYTES", "Image", "ImageInfo", "Source"]
