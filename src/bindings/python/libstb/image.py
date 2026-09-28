"""Object-oriented API on top of the native module."""

from __future__ import annotations

import os
from typing import NamedTuple, Union

import numpy as np

from . import libstb_py as _native
from .libstb_py import Encoder, Resizer

# Path, or the encoded image itself.
Source = Union[str, "os.PathLike[str]", bytes, bytearray, memoryview]

DEFAULT_MAX_BYTES = 1 << 29  # 512 MiB, mirrors libstb::load_options


def _read(source: Source) -> bytes:
    """bytes-like -> the image data itself; str / os.PathLike -> file contents.

    ponytail: files are read with plain open(), so a missing or unreadable
    file raises the usual FileNotFoundError/PermissionError for free; the
    native side only ever decodes memory.
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


class Image:
    """An 8-bit image: a thin wrapper over a uint8 array of shape (H, W, C).

    >>> img = Image.open("photo.png", channels=4)
    >>> img.width, img.height, img.channels
    >>> img.numpy()              # the pixels as an ndarray, no copy
    """

    __slots__ = ("_a",)

    def __init__(self, array):
        """Wrap a uint8 array of shape (H, W) or (H, W, 1..4)."""
        a = np.asarray(array)
        if a.dtype != np.uint8:
            raise TypeError(f"Image needs a uint8 array, got {a.dtype}")
        if a.ndim == 2:
            a = a[:, :, None]
        if a.ndim != 3 or not 1 <= a.shape[2] <= 4:
            raise ValueError(f"expected shape (H, W) or (H, W, 1..4), got {a.shape}")
        self._a = a

    @classmethod
    def open(
        cls,
        source: Source,
        *,
        channels: int = 0,
        flip: bool = False,
        max_bytes: int = DEFAULT_MAX_BYTES,
    ) -> "Image":
        """Decode an image from a path or from bytes.

        channels  : 0 keeps the file's channel count; 1..4 converts.
        flip      : flip vertically while decoding.
        max_bytes : refuse images whose decoded size would exceed this
                    (checked from the header, before any pixel allocation).

        Raises ValueError for bad arguments, DecodeError for undecodable
        input, LimitError for oversized images, OSError for unreadable files.
        (DecodeError and LimitError are libstb.Error, a RuntimeError.)
        """
        return cls(_native.load_bytes(_read(source), channels, flip, max_bytes))

    def resize(self, width: int, height: int, resizer: "Resizer | None" = None) -> "Image":
        """A new Image of the given size (this one is not modified).

        Uses a default Resizer() unless one is given. Raises ValueError (bad
        size), LimitError (result too large).
        """
        return Image((resizer if resizer is not None else Resizer()).resize(self._a, width, height))

    def encode(self, encoder: Encoder) -> bytes:
        """Encode to bytes with the given encoder (PngEncoder, JpegEncoder, ...)."""
        if not isinstance(encoder, Encoder):  # else "png".encode(...) would "work" by duck typing
            raise TypeError(f"expected a libstb.Encoder, got {type(encoder).__name__}")
        return encoder.encode(self._a)

    def save(self, path, encoder: "Encoder | None" = None) -> None:
        """Encode and write to `path`.

        Without an encoder, one is chosen from the extension (.png .jpg .jpeg
        .bmp .tga; ValueError otherwise). The file is written only after
        encoding succeeded, so a failure never leaves a truncated file.
        """
        path = os.fsdecode(path)
        data = self.encode(encoder if encoder is not None else Encoder.for_path(path))
        with open(path, "wb") as f:
            f.write(data)

    @property
    def array(self) -> np.ndarray:
        """The pixels as a uint8 ndarray, shape (height, width, channels)."""
        return self._a

    @property
    def width(self) -> int:
        return self._a.shape[1]

    @property
    def height(self) -> int:
        return self._a.shape[0]

    @property
    def channels(self) -> int:
        return self._a.shape[2]

    @property
    def shape(self) -> tuple:
        return self._a.shape

    def numpy(self, *, dtype=None, copy: bool = False) -> np.ndarray:
        """The pixels as an ndarray. A view (no copy) unless copy=True or the
        requested dtype differs from uint8."""
        return self.__array__(dtype, copy)

    def __array__(self, dtype=None, copy=None):
        a = self._a
        if dtype is not None and np.dtype(dtype) != a.dtype:
            return a.astype(dtype)  # always a copy
        return a.copy() if copy else a

    def __repr__(self) -> str:
        return f"Image(width={self.width}, height={self.height}, channels={self.channels})"
