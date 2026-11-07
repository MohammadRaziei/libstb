"""Image (native: it is stb::image) and the header-only ImageInfo."""

from __future__ import annotations

import os
from typing import NamedTuple, Union

from . import libstb_py as _native
from .libstb_py import DEFAULT_MAX_BYTES, Image, PixelFormat

# Path, or the encoded image itself.
Source = Union[str, "os.PathLike[str]", bytes, bytearray, memoryview]


def _read(source: Source) -> bytes:
    """bytes-like -> the data itself; str / os.PathLike -> file contents.

    used by ImageInfo and Font, which decode from memory. (Image
    opens paths natively; a missing file is a FileNotFoundError either way.)
    """
    if isinstance(source, bytes):
        return source
    if isinstance(source, (bytearray, memoryview)):
        return bytes(source)
    with open(source, "rb") as f:
        return f.read()


# --- Pillow interop. Pillow is not a dependency: it is imported when one of these runs
# (like numpy for Image.numpy()); install it with `pip install "libstb[pillow]"`.

_PIL_MODES = {
    PixelFormat.GRAY: "L",
    PixelFormat.GRAY_ALPHA: "LA",
    PixelFormat.RGB: "RGB",
    PixelFormat.RGBA: "RGBA",
}

# Pillow modes that are not 8-bit L / LA / RGB / RGBA but convert to one without guessing.
_PIL_CONVERT = {"1": "L", "La": "LA", "PA": "RGBA", "RGBa": "RGBA", "RGBX": "RGB", "CMYK": "RGB", "YCbCr": "RGB"}


def _pillow():
    try:
        from PIL import Image as PILImage
    except ImportError as e:
        raise ImportError('Pillow is needed for this: pip install "libstb[pillow]"') from e
    return PILImage


# Pillow modes whose rows are plain bytes in the order libstb stores them, so a Pillow image can look
# at the pixels of an Image as they are. Pillow keeps RGB and LA at 4 bytes per pixel inside, so those
# two cannot: they are copied.
_PIL_SHARES_MEMORY = frozenset({"L", "RGBA"})


def _to_pil(self):
    """A Pillow image with these pixels: mode L, LA, RGB or RGBA by pixel format.

    L and RGBA are NOT copied: the Pillow image looks at this image's pixels (it keeps the image
    alive), like ``img.numpy()`` does. Pillow marks such an image read-only and copies it the first
    time you change it, so Pillow never writes into this image; but a change to this image's pixels
    (through ``img.numpy()`` or the array it wraps) shows in the Pillow image. ``img.to_pil().copy()``
    is an independent Pillow image (one copy, as before). LA and RGB are always copied, because
    Pillow stores them at 4 bytes per pixel.

    Needs Pillow, imported only here.
    """
    PILImage = _pillow()
    mode = _PIL_MODES[self.format]
    size = (self.width, self.height)
    if mode in _PIL_SHARES_MEMORY:
        return PILImage.frombuffer(mode, size, memoryview(self), "raw", mode, 0, 1)
    return PILImage.frombytes(mode, size, memoryview(self))


def _from_pil(cls, pil):
    """An Image from a Pillow image. Needs Pillow, imported only here.

    L, LA, RGB and RGBA are taken as they are. Modes that hold 8-bit colour in another form
    are converted first: 1 -> L, P -> RGB (RGBA if the palette has transparency), PA / RGBa
    -> RGBA, La -> LA, RGBX / CMYK / YCbCr -> RGB. Other modes (I, I;16, F, LAB, HSV, ...)
    have no 8-bit colour to take and raise TypeError: convert them yourself.
    Image(pil) takes only L, LA, RGB and RGBA and raises TypeError for the rest, so a
    palette or CMYK image is never read as if it were colours.
    """
    PILImage = _pillow()
    if not isinstance(pil, PILImage.Image):
        raise TypeError(f"from_pil needs a PIL.Image.Image, not {type(pil).__name__}")
    mode = pil.mode
    if mode == "P":
        pil = pil.convert("RGBA" if "transparency" in pil.info else "RGB")
    elif mode in _PIL_CONVERT:
        pil = pil.convert(_PIL_CONVERT[mode])
    elif mode not in _PIL_MODES.values():
        raise TypeError(
            f"Pillow mode {mode!r} has no 8-bit colour libstb can take: use L, LA, RGB or RGBA, "
            "or convert it first, e.g. pil.convert('RGB')"
        )
    return cls(pil)


Image.to_pil = _to_pil
Image.from_pil = classmethod(_from_pil)


class ImageInfo(NamedTuple):
    """Header-only facts about an image file."""

    width: int
    height: int
    format: PixelFormat

    @property
    def channels(self) -> int:
        """Number of channels in the file: 1 to 4."""
        return int(self.format.value)

    @classmethod
    def read(cls, source: Source) -> "ImageInfo":
        """Read just the header (no pixel decoding)."""
        return cls(*_native.info_bytes(_read(source)))


__all__ = ["DEFAULT_MAX_BYTES", "Image", "ImageInfo", "PixelFormat", "Source"]
