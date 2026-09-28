"""Fonts (stb_truetype): object API on top of the native module.

SECURITY: stb_truetype does no bounds checking; its author says not to use it
on untrusted font files. Only load fonts you trust.
"""

from __future__ import annotations

from typing import Dict, NamedTuple, Optional

import numpy as np

from . import libstb_py as _native
from .image import Image, Source, _read


class FontMetrics(NamedTuple):
    """Vertical metrics in pixels (descent is negative)."""

    ascent: float
    descent: float
    line_gap: float

    @property
    def line_height(self) -> float:
        return self.ascent - self.descent + self.line_gap


class Glyph(NamedTuple):
    """One rasterised character. `bitmap` is a 1-channel coverage Image, or
    None for blank glyphs such as space. x_offset / y_offset place the bitmap
    relative to the pen at the baseline (y_offset is negative above it)."""

    bitmap: Optional[Image]
    x_offset: int
    y_offset: int
    advance: float


class TextSize(NamedTuple):
    width: float
    height: float
    lines: int


class RenderedText(NamedTuple):
    """`bitmap` covers the layout box plus any overhanging ink;
    (origin_x, origin_y) is where the layout box's top-left sits in it."""

    bitmap: Image
    origin_x: int
    origin_y: int


class AtlasGlyph(NamedTuple):
    char: str
    x0: int
    y0: int
    x1: int
    y1: int  # pixel rectangle in the atlas
    xoff: float
    yoff: float  # top-left of the quad, relative to the pen at the baseline
    xoff2: float
    yoff2: float  # bottom-right of the quad
    advance: float


class Atlas:
    """A packed glyph sheet: `image` (1 channel) and a per-character lookup."""

    __slots__ = ("image", "glyphs")

    def __init__(self, image: Image, glyphs: Dict[str, AtlasGlyph]):
        self.image = image
        self.glyphs = glyphs

    def __getitem__(self, char: str) -> AtlasGlyph:
        return self.glyphs[char]

    def __contains__(self, char: str) -> bool:
        return char in self.glyphs

    def __len__(self) -> int:
        return len(self.glyphs)

    def __repr__(self) -> str:
        return f"Atlas({self.image.width}x{self.image.height}, {len(self)} glyphs)"


class Font:
    """A TrueType/OpenType font (immutable; safe to share between threads).

    >>> font = Font.open("DejaVuSans.ttf")
    >>> font.render("Hello", 32).bitmap.save("hello.png")
    """

    __slots__ = ("_f",)

    def __init__(self, native: _native.Font):
        self._f = native

    @classmethod
    def open(cls, source: Source, *, index: int = 0) -> "Font":
        """Load from a path or from bytes. `index` selects a face in a .ttc.

        Raises ValueError (bad index), DecodeError (not a font), OSError.
        """
        return cls(_native.Font.from_bytes(_read(source), index))

    def metrics(self, pixel_height: float) -> FontMetrics:
        return FontMetrics(*self._f.metrics(pixel_height))

    def has_glyph(self, char: str) -> bool:
        return self._f.has_glyph(char)

    def advance(self, char: str, pixel_height: float) -> float:
        return self._f.advance(char, pixel_height)

    def kerning(self, left: str, right: str, pixel_height: float) -> float:
        """Kerning between two characters in pixels (usually <= 0)."""
        return self._f.kerning(left, right, pixel_height)

    def render_glyph(self, char: str, pixel_height: float) -> Glyph:
        arr, xo, yo, adv = self._f.render_glyph(char, pixel_height)
        return Glyph(None if arr is None else Image(arr), xo, yo, adv)

    def measure(self, text: str, pixel_height: float) -> TextSize:
        return TextSize(*self._f.measure(text, pixel_height))

    def render(self, text: str, pixel_height: float, *, max_bytes: int = 1 << 28) -> RenderedText:
        """Render (multi-line) text into one 1-channel coverage Image.

        Raises ValueError (empty text, bad pixel_height), LimitError.
        """
        arr, ox, oy = self._f.render(text, pixel_height, max_bytes)
        return RenderedText(Image(arr), ox, oy)

    def make_atlas(self, chars: str, pixel_height: float, width: int, height: int,
                   *, padding: int = 1) -> Atlas:
        """Pack the characters of `chars` into one width x height sheet.

        Raises LimitError if they do not all fit.
        """
        arr, entries = self._f.make_atlas(chars, pixel_height, width, height, padding)
        return Atlas(Image(arr), {e[0]: AtlasGlyph(*e) for e in entries})
