"""libstb: Python bindings for the stb single-header libraries.

No system dependencies: `pip install libstb` is all you need.
"""

import os as _os
from collections import namedtuple as _namedtuple

try:
    # Version is derived from include/libstb.h at build time - never
    # hardcoded here, so it cannot drift from the real release.
    from importlib.metadata import version as _pkg_version

    __version__ = _pkg_version("libstb")
except Exception:  # pragma: no cover
    __version__ = "0.0.0+unknown"

from . import libstb_py as _native

_pkg_dir = _os.path.dirname(__file__)

ImageInfo = _namedtuple("ImageInfo", "width height channels")

_DEFAULT_MAX_BYTES = 1 << 29  # 512 MiB, mirrors libstb::load_options


def _read(source):
    """bytes-like -> bytes (image data); str / os.PathLike -> file contents.

    ponytail: files are read here with plain open(), so missing/unreadable
    files raise the usual FileNotFoundError/PermissionError for free; the
    native side only ever decodes memory.
    """
    if isinstance(source, bytes):
        return source
    if isinstance(source, (bytearray, memoryview)):
        return bytes(source)
    with open(source, "rb") as f:
        return f.read()


def info(source):
    """Read only the header. Returns ImageInfo(width, height, channels)."""
    return ImageInfo(*_native.info_bytes(_read(source)))


def load(source, *, channels=0, flip=False, max_bytes=_DEFAULT_MAX_BYTES):
    """Decode an image into a uint8 numpy array of shape (height, width, channels).

    source    : path (str / os.PathLike) or the encoded image as bytes-like.
    channels  : 0 keeps the file's channel count; 1..4 converts.
    flip      : flip vertically.
    max_bytes : refuse images whose decoded size would exceed this (checked
                from the header, before any pixel memory is allocated).

    Raises ValueError for bad arguments, RuntimeError for undecodable or
    oversized images.
    """
    return _native.load_bytes(_read(source), channels, flip, max_bytes)


def get_include_dir():
    """Directory containing libstb.h and libstb/*.hpp."""
    return _os.path.join(_pkg_dir, "include")


def get_lib_dir():
    """Directory containing the static C++ library (libstb_core)."""
    return _os.path.join(_pkg_dir, "lib")


def get_cmake_dir():
    """Directory containing libstbConfig.cmake, for find_package(libstb CONFIG)."""
    return _os.path.join(_pkg_dir, "cmake")


__all__ = ["ImageInfo", "info", "load", "get_include_dir", "get_lib_dir", "get_cmake_dir"]
