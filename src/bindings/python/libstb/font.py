"""Fonts (stb_truetype): object API on top of the native module.

SECURITY: stb_truetype does no bounds checking; its author says not to use it
on untrusted font files. Only load fonts you trust.
"""

from __future__ import annotations

import os
from typing import Dict, NamedTuple, Optional


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
    >>> font.render("Hello", 32).bitmap.write("hello.png")
    """

    __slots__ = ("_f",)

    def __init__(self, native: _native.Font):
        self._f = native

    @classmethod
    def open(cls, source: Source, *, index: int = 0) -> "Font":
        """Load from a path or from bytes. `index` selects a face in a .ttc.

        Raises ValueError (bad index), DecodeError (not a font), OSError.
        """
        if isinstance(source, (str, os.PathLike)):
            return cls(_native.Font.open(source, index))
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
        bitmap, xo, yo, adv = self._f.render_glyph(char, pixel_height)
        return Glyph(bitmap, xo, yo, adv)

    def measure(self, text: str, pixel_height: float) -> TextSize:
        return TextSize(*self._f.measure(text, pixel_height))

    def render(self, text: str, pixel_height: float, *, max_bytes: int = 1 << 28) -> RenderedText:
        """Render (multi-line) text into one 1-channel coverage Image.

        Raises ValueError (empty text, bad pixel_height), LimitError.
        """
        bitmap, ox, oy = self._f.render(text, pixel_height, max_bytes)
        return RenderedText(bitmap, ox, oy)

    def make_atlas(self, chars: str, pixel_height: float, width: int, height: int,
                   *, padding: int = 1) -> Atlas:
        """Pack the characters of `chars` into one width x height sheet.

        Raises LimitError if they do not all fit.
        """
        bitmap, entries = self._f.make_atlas(chars, pixel_height, width, height, padding)
        return Atlas(bitmap, {e[0]: AtlasGlyph(*e) for e in entries})


def _draw_text(self, text: str, font: Font, pixel_height: float, x: int = 0, y: int = 0, *,
               color=0, align: str = "left") -> Image:
    """A copy of this Image with `text` drawn on it (multi-line, UTF-8).

    (x, y) is where the text's layout box starts (its top-left corner). With ``align="center"``
    or ``"right"``, x is instead the horizontal centre or right edge of every line, so
    ``img.draw_text("a\\nlonger line", font, 32, 200, 10, align="center")`` centres both lines on
    x = 200. Text that hangs over the edges is clipped.

    `color` is an int (gray), (r, g, b), (r, g, b, a) or a string such as "red" or "#ff8800"
    (parsed like stb::color does); the alpha scales the glyph coverage, so a < 255 gives
    translucent text. The result has this Image's channel count; on an image with alpha the text
    is blended with the usual "over" operator.

    Raises ValueError (empty text, bad pixel_height, color or align), LimitError.

    >>> img.draw_text("Hello", font, 32, 10, 10, color=(255, 255, 255))
    """
    return font._f.draw_text(self, text, pixel_height, x, y, color, align)


Image.draw_text = _draw_text


class Canvas:
    """Draw several texts on ONE copy of an Image.

    ``Image.draw_text`` copies the image on every call; a Canvas copies it once, remembers the
    font, size, color and alignment, and blends each text into that copy. The Image you pass in is
    never modified. The calls chain::

        out = (img.canvas().font(font, 48).color("white")
                  .text("Title", 20, 20)
                  .text("Subtitle", 20, 90, color="gray")
                  .finish())

    ``finish()`` hands the Image over without a copy and empties the Canvas: using it again
    raises ValueError. ``text()`` before ``font()`` raises ValueError too.
    """

    __slots__ = ("_img", "_font", "_size", "_color", "_align")

    def __init__(self, base: Image, font: Optional[Font] = None, pixel_height: float = 0, *,
                 color=0, align: str = "left"):
        self._img = base.copy()
        self._font = font
        self._size = pixel_height
        self._color = color
        self._align = align

    def _check(self) -> Image:
        if self._img is None:
            raise ValueError("this Canvas was finished")
        return self._img

    def font(self, font: Font, pixel_height: float) -> "Canvas":
        """Use `font` at `pixel_height` for the texts that follow."""
        self._check()
        self._font, self._size = font, pixel_height
        return self

    def color(self, color) -> "Canvas":
        """Use `color` (int, (r, g, b), (r, g, b, a) or a string) for the texts that follow."""
        self._check()
        self._color = color
        return self

    def align(self, align: str) -> "Canvas":
        """``"left"``, ``"center"`` or ``"right"``: how the texts that follow line up with their
        x (see Image.draw_text)."""
        self._check()
        self._align = align
        return self

    def text(self, text: str, x: int = 0, y: int = 0, *, color=None, align: Optional[str] = None) -> "Canvas":
        """Draw `text` at (x, y); `color` and `align` override the current ones for this call only."""
        img = self._check()
        if self._font is None:
            raise ValueError("call font() before text()")
        self._font._f.draw_text_in_place(img, text, self._size, x, y,
                                         self._color if color is None else color,
                                         self._align if align is None else align)
        return self

    def finish(self) -> Image:
        """The finished Image (no copy). The Canvas is empty afterwards."""
        img, self._img = self._check(), None
        return img


Image.canvas = lambda self, font=None, pixel_height=0, *, color=0, align="left": Canvas(
    self, font, pixel_height, color=color, align=align)
Image.canvas.__doc__ = "A Canvas for drawing several texts on one copy of this Image (see Canvas)."


def estimate_text(text: str, pixel_height: float, family: str = "sans") -> TextSize:
    """Rough size of `text` without any font file.

    `family` is "sans" (Arial-like), "serif" (Times-like) or "mono" (Courier-like). Widths come
    from fixed tables, so the result is the same on every machine. It is an estimate: no kerning,
    no bold or italic, and characters outside Latin-1 and common punctuation are approximated
    (CJK and emoji 1 em, anything else the average letter width). For exact numbers use
    Font.measure with a real font. Same line rules as Font.measure.

    Raises ValueError (malformed text, bad pixel_height, unknown family).

    >>> stb.estimate_text("Hello", 16)
    TextSize(width=..., height=..., lines=1)
    """
    return TextSize(*_native.estimate_text(text, pixel_height, family))


def estimate_metrics(pixel_height: float, family: str = "sans") -> FontMetrics:
    """Vertical metrics for `family` at `pixel_height`, without a font file."""
    return FontMetrics(*_native.estimate_metrics(pixel_height, family))
