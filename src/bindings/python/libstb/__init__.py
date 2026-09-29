"""libstb: Python bindings for the stb single-header libraries.

No system dependencies: `pip install libstb` is all you need.

    img = libstb.Image.open("photo.png")   # object API
    img.save("photo.jpg", libstb.JpegEncoder(quality=80))
    small = img.resize(320, 240)
    libstb.Font.open("font.ttf").render("Hi", 32).bitmap.save("hi.png")
    pixels = libstb.load("photo.png")      # or: just the ndarray
"""

import os as _os

try:
    # Version is derived from include/stb.h at build time - never
    # hardcoded here, so it cannot drift from the real release.
    from importlib.metadata import version as _pkg_version

    __version__ = _pkg_version("libstb")
except Exception:  # pragma: no cover
    __version__ = "0.0.0+unknown"

from .image import DEFAULT_MAX_BYTES, Image, ImageInfo
from .font import Atlas, AtlasGlyph, Font, FontMetrics, Glyph, RenderedText, TextSize
from .libstb_py import (
    BmpEncoder,
    DecodeError,
    EncodeError,
    Encoder,
    Error,
    JpegEncoder,
    LimitError,
    PngEncoder,
    Resizer,
    TgaEncoder,
)

_pkg_dir = _os.path.dirname(__file__)


def info(source):
    """Shortcut for ImageInfo.read(source)."""
    return ImageInfo.read(source)


def load(source, **kwargs):
    """Shortcut for Image.open(source, **kwargs).array (a uint8 ndarray)."""
    return Image.open(source, **kwargs).array


def get_include_dir():
    """Directory containing stb.h and stb/*.hpp."""
    return _os.path.join(_pkg_dir, "include")


def get_lib_dir():
    """Directory containing the static C++ library (libstb_core)."""
    return _os.path.join(_pkg_dir, "lib")


def get_cmake_dir():
    """Directory containing libstbConfig.cmake, for find_package(libstb CONFIG)."""
    return _os.path.join(_pkg_dir, "cmake")


__all__ = [
    "Image", "ImageInfo", "DEFAULT_MAX_BYTES", "info", "load",
    "Encoder", "PngEncoder", "JpegEncoder", "BmpEncoder", "TgaEncoder",
    "Resizer",
    "Font", "FontMetrics", "Glyph", "TextSize", "RenderedText", "Atlas", "AtlasGlyph",
    "Error", "DecodeError", "EncodeError", "LimitError",
    "get_include_dir", "get_lib_dir", "get_cmake_dir",
]
