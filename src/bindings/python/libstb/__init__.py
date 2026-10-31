"""libstb: Python bindings for the stb single-header libraries.

No system dependencies: `pip install libstb` is all you need.

    img = libstb.Image.open("photo.png")   # object API
    img.write("photo.jpg")                  # format from the extension
    img.write_jpg("photo.jpg", quality=80) # or pick the format and its settings
    small = img.resize(320, 240)
    libstb.Font.open("font.ttf").render("Hi", 32).bitmap.write("hi.png")
    pixels = libstb.imread("photo.png")    # or: just the ndarray
    libstb.imwrite("out.jpg", pixels, quality=80)
"""

import os as _os

# The native core is a shared library in libstb/lib (libstb.so, libstb.dylib: found through the
# extension's rpath). Windows has no rpath and does not search PATH for the DLLs of an extension
# module, so the directory is added by hand, before the first import of the extension below.
# os.add_dll_directory exists only there: asking for the function, not the platform, is the check.
_lib_dir = _os.path.join(_os.path.dirname(__file__), "lib")
if hasattr(_os, "add_dll_directory") and _os.path.isdir(_lib_dir):  # pragma: no cover (Windows only)
    _dll_directory = _os.add_dll_directory(_lib_dir)  # kept: it stays registered for the process

try:
    # Version is derived from include/stb.h at build time - never
    # hardcoded here, so it cannot drift from the real release.
    from importlib.metadata import version as _pkg_version

    __version__ = _pkg_version("libstb")
except Exception:  # pragma: no cover
    __version__ = "0.0.0+unknown"

from .image import DEFAULT_MAX_BYTES, Image, ImageInfo, PixelFormat
from .font import (Atlas, AtlasGlyph, Font, FontMetrics, Glyph, RenderedText, TextSize,
                   estimate_metrics, estimate_text)
from .compress import DEFAULT_ZLIB_LEVEL, DEFAULT_ZLIB_MAX_BYTES, zlib_compress, zlib_decompress
from .libstb_py import Align, BlendMode, Canvas, CanvasImage, CanvasShape, CanvasText, DecodeError, EncodeError, Error, LimitError, Resizer
from .libstb_py import exif_orientation_bytes as _native_exif_orientation
from .libstb_py import _SimdContext, set_simd, simd_backends, simd_name

_pkg_dir = _os.path.dirname(__file__)


def simd(name):
    """Use one SIMD backend for the calling thread inside a `with` block:

        with libstb.simd("scalar"):
            out = img.convert("gray")  # runs the scalar kernels, on this thread only

    Unlike set_simd (the whole process), other threads are not affected and the previous
    backend comes back on exit; blocks nest. `name` is one of simd_backends() or "auto"
    (the best one); ValueError for anything else. Enter and leave the block on the same
    thread.
    """
    return _SimdContext(name)


def iminfo(source):
    """Shortcut for ImageInfo.read(source): (width, height, format), header only."""
    return ImageInfo.read(source)


def exif_orientation(source):
    """The EXIF orientation (1..8) stored in a JPEG or PNG, or 1 (upright) if there is
    none. `source` is a path or the encoded bytes. Apply it with Image.orient(), or let
    Image.open(..., orient=True) / imread(..., orient=True) do it while decoding.
    """
    from .image import _read

    return _native_exif_orientation(_read(source))


def imread(source, *, format=None, flip=False, max_bytes=DEFAULT_MAX_BYTES, orient=False):
    """Decode an image file (path) or encoded bytes straight to a uint8 ndarray
    of shape (height, width, channels). Needs numpy; use Image.open without it.

    Same options as Image.open: format (None keeps the file's), flip, max_bytes, orient
    (apply the EXIF orientation).
    """
    return Image.open(
        source, format=format, flip=flip, max_bytes=max_bytes, orient=orient
    ).numpy()


def imresize(array, width, height, resizer=None):
    """Resize a uint8 array of shape (H, W) or (H, W, 1..4) (or an Image) to `width` x `height`
    and return a new ndarray, for code that works on arrays and never touches Image.

    `resizer` is whatever Image.resize takes: None (the default Resizer), a Resizer, a
    Resizer.Filter or a filter name such as "box" or "mitchell". The result has the input's
    number of dimensions: (height, width) for a 2-D array, (height, width, channels) else.
    Needs numpy.
    """
    out = (array if isinstance(array, Image) else Image(array)).resize(width, height, resizer).numpy()
    return out[..., 0] if getattr(array, "ndim", None) == 2 else out


# extension -> (Image method, the options that format understands)
_WRITERS = {
    ".png": ("write_png", ("compression",)),
    ".jpg": ("write_jpg", ("quality",)),
    ".jpeg": ("write_jpg", ("quality",)),
    ".bmp": ("write_bmp", ()),
    ".tga": ("write_tga", ("rle",)),
}


def imwrite(path, image, *, quality=None, compression=None, rle=None):
    """Write an Image, or a uint8 array of shape (H, W) / (H, W, 1..4), to `path`.

    The format comes from the extension (.png .jpg .jpeg .bmp .tga, case-insensitive).
    Options apply to the format that has them and are a ValueError for the others:
    quality (jpg, 1..100), compression (png, 1..9), rle (tga, bool).
    """
    img = image if isinstance(image, Image) else Image(image)
    ext = _os.path.splitext(_os.fspath(path))[1].lower()
    if ext not in _WRITERS:
        raise ValueError(f"unsupported extension {ext!r}: use one of {', '.join(_WRITERS)}")
    method, allowed = _WRITERS[ext]
    given = {k: v for k, v in (("quality", quality), ("compression", compression), ("rle", rle))
             if v is not None}
    bad = [k for k in given if k not in allowed]
    if bad:
        raise ValueError(
            f"{bad[0]} is not an option for {ext} files"
            + (f" (it takes: {', '.join(allowed)})" if allowed else " (it takes no options)")
        )
    getattr(img, method)(path, **given)


def get_include_dir():
    """Directory containing stb.h and stb/*.hpp."""
    return _os.path.join(_pkg_dir, "include")


def get_lib_dir():
    """Directory containing the static C++ library (stb_core)."""
    return _os.path.join(_pkg_dir, "lib")


def get_cmake_dir():
    """Directory containing libstbConfig.cmake, for find_package(libstb CONFIG)."""
    return _os.path.join(_pkg_dir, "cmake")


__all__ = [
    "Image", "ImageInfo", "PixelFormat", "DEFAULT_MAX_BYTES", "imread", "imwrite", "iminfo", "exif_orientation", "simd_name", "simd_backends", "set_simd", "simd", "imresize",
    "Resizer",
    "Font", "Canvas", "CanvasText", "CanvasShape", "CanvasImage", "BlendMode", "Align", "FontMetrics", "Glyph", "TextSize", "RenderedText", "Atlas", "AtlasGlyph",
    "estimate_text", "estimate_metrics",
    "zlib_compress", "zlib_decompress", "DEFAULT_ZLIB_LEVEL", "DEFAULT_ZLIB_MAX_BYTES",
    "Error", "DecodeError", "EncodeError", "LimitError",
    "get_include_dir", "get_lib_dir", "get_cmake_dir",
]
