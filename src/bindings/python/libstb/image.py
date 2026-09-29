"""Object-oriented API on top of the native module."""

from __future__ import annotations

import os
from typing import NamedTuple, Union

import numpy as np

from . import libstb_py as _native
from .libstb_py import DEFAULT_JPG_QUALITY, DEFAULT_PNG_COMPRESSION, Resizer

# Path, or the encoded image itself.
Source = Union[str, "os.PathLike[str]", bytes, bytearray, memoryview]

DEFAULT_MAX_BYTES = 1 << 29  # 512 MiB, mirrors stb::load_options

# Extension -> the format save() writes (mirrors stb::image::save).
_EXTENSIONS = {".png": "png", ".jpg": "jpg", ".jpeg": "jpg", ".bmp": "bmp", ".tga": "tga"}


def _write(path, data: bytes) -> None:
    with open(os.fsdecode(path), "wb") as f:
        f.write(data)


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

    def resize(self, width: int, height: int, resizer: "Resizer | Resizer.Filter | str | None" = None) -> "Image":
        """A new Image of the given size (this one is not modified).

        resizer : None (default Resizer()), a Resizer, a Resizer.Filter, or a
                  filter name: "nearest", "linear", "cubic", "bspline",
                  "mitchell", "box", "auto". A name or Filter is shorthand for
                  Resizer(name); use a Resizer to also set edge/srgb/max_bytes.

        Raises ValueError (bad size or unknown filter name), LimitError
        (result too large).
        """
        if not isinstance(resizer, Resizer):
            resizer = Resizer() if resizer is None else Resizer(resizer)
        return Image(resizer.resize(self._a, width, height))

    # --- encoding -----------------------------------------------------
    # to_*: the bytes of a whole file. write_*: the same, written to `path`
    # (only after encoding succeeded, so a failure never leaves a truncated
    # file). Each format has its own settings, all defaulted. ValueError for
    # a setting out of range, EncodeError / LimitError from the encoding itself.

    def to_png(self, compression: int = DEFAULT_PNG_COMPRESSION) -> bytes:
        """PNG bytes. compression 1..9: higher is smaller and slower."""
        return _native.to_png(self._a, compression)

    def to_jpg(self, quality: int = DEFAULT_JPG_QUALITY) -> bytes:
        """JPEG bytes. quality 1..100; alpha, if any, is dropped."""
        return _native.to_jpg(self._a, quality)

    def to_bmp(self) -> bytes:
        """BMP bytes."""
        return _native.to_bmp(self._a)

    def to_tga(self, rle: bool = True) -> bytes:
        """TGA bytes, run-length encoded unless rle=False."""
        return _native.to_tga(self._a, rle)

    def write_png(self, path, compression: int = DEFAULT_PNG_COMPRESSION) -> None:
        _write(path, self.to_png(compression))

    def write_jpg(self, path, quality: int = DEFAULT_JPG_QUALITY) -> None:
        _write(path, self.to_jpg(quality))

    def write_bmp(self, path) -> None:
        _write(path, self.to_bmp())

    def write_tga(self, path, rle: bool = True) -> None:
        _write(path, self.to_tga(rle))

    def save(self, path) -> None:
        """Write to `path` in the format its extension names (.png .jpg
        .jpeg .bmp .tga, case-insensitive; ValueError otherwise), with
        default settings. For other settings call write_* directly."""
        ext = os.path.splitext(os.fsdecode(path))[1].lower()
        fmt = _EXTENSIONS.get(ext)
        if fmt is None:
            raise ValueError(f"unsupported image extension {ext!r} (supported: .png .jpg .jpeg .bmp .tga)")
        getattr(self, "write_" + fmt)(path)

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
