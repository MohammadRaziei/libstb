"""Fonts: object API on top of the native module (libstb's own TrueType / CFF engine).

SECURITY: the font reader checks its bounds but has not been fuzzed. Only load
fonts you trust.
"""

from __future__ import annotations

import os
from typing import Dict, List, NamedTuple, Optional

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


class GlyphMetrics(NamedTuple):
    """One glyph's metrics in font units (y up). `advance` is how far the pen moves after the
    glyph, `lsb` the distance from the pen to the glyph's left edge, (x_min, y_min, x_max, y_max)
    the ink box (all 0 when `has_outline` is False, as for a space)."""

    advance: float
    lsb: float
    x_min: float
    y_min: float
    x_max: float
    y_max: float
    has_outline: bool


class DesignMetrics(NamedTuple):
    """A font's vertical metrics and bounding box in font units (descent is negative)."""

    units_per_em: int
    ascent: int
    descent: int
    line_gap: int
    x_min: int
    y_min: int
    x_max: int
    y_max: int


class ShapedGlyph(NamedTuple):
    """One glyph of Font.shape(), in font units with y up. The glyph is drawn at
    (pen_x + x_offset, pen_y + y_offset) and the pen then moves right by `x_advance`.
    `glyph` is 0 for a line break; `cluster` is the byte offset in the UTF-8 text of the first
    character the glyph stands for."""

    glyph: int
    cluster: int
    x_advance: float
    x_offset: float
    y_offset: float


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

    @staticmethod
    def face_count(source: Source) -> int:
        """How many fonts a file holds: 1 for a .ttf / .otf / .woff, the number of faces of a .ttc
        (`Font.open(..., index=i)` takes an index below it). `source` is a path or bytes.

        It does not check that every face is usable. Raises DecodeError (not a font, or WOFF2),
        OSError.
        """
        if isinstance(source, (str, os.PathLike)):
            return _native.Font.face_count(source)
        return _native.Font.face_count_bytes(_read(source))

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

    def shape_glyphs(self, text: str) -> list[int]:
        """Glyph indices of the font that `text` is drawn with, in visual order ('\\n' is 0).

        This is the result of shaping: joined letter shapes, ligatures, mirrored brackets and the
        right-to-left order. A letter's marks follow it. For diagnostics and tests.
        """
        return self._f.shape_glyphs(text)

    # ---- glyph-index API: outlines and metrics in font units (y up), for vector renderers ----

    def glyph_index(self, char: str) -> int:
        """The font's glyph for one character; 0 (".notdef") if it has none (see has_glyph)."""
        return self._f.glyph_index(char)

    @property
    def glyph_count(self) -> int:
        """How many glyphs the font has. An index at or past it is not a glyph: glyph_info,
        outline and glyph_kerning answer "nothing" for it instead of raising."""
        return self._f.glyph_count

    def glyph_info(self, glyph: int) -> GlyphMetrics:
        """The metrics of a glyph in font units. Zeros for an index that is not a glyph.

        Raises ValueError for a negative index or one above 2**32 - 1.
        """
        return GlyphMetrics(*self._f.glyph_info(glyph))

    @property
    def design_metrics(self) -> DesignMetrics:
        """The vertical metrics and bounding box in font units (Font.metrics gives the vertical
        numbers in pixels). The `size` of draw_text is the height from the descent to the ascent, so
        to draw outlines at the same size scale them by ``size / (ascent - descent)``."""
        return DesignMetrics(*self._f.design_metrics())

    def outline(self, glyph: int) -> list:
        """The outline of a glyph in font units, y up, as the commands Outline takes:
        ("M", x, y), ("L", x, y), ("Q", cx, cy, x, y) (TrueType), ("C", c1x, c1y, c2x, c2y, x, y)
        (CFF) and ("Z",). Empty for a glyph with no outline or an index that is not a glyph.

        Outline's y axis points down, like pixels, so draw it through a Matrix that scales by
        ``(s, -s)`` and moves the baseline::

            o = stb.Outline(font.outline(font.glyph_index("A")))
            img = img.fill_outline(o, "black", matrix=stb.Matrix.translation(x, baseline).scale(s, -s))

        Raises ValueError for a negative index or one above 2**32 - 1.
        """
        return self._f.outline(glyph)

    def glyph_kerning(self, left: int, right: int, mode: str = "automatic") -> float:
        """Kerning between two glyphs in font units (usually <= 0). `mode` is "automatic" (GPOS pair
        adjustment if the font has GPOS, else the `kern` table; what text drawing uses),
        "kern_table" (the legacy `kern` table only) or "none"."""
        return self._f.glyph_kerning(left, right, mode)

    def shape(self, text: str, *, direction: str = "auto", kerning: str = "automatic",
              ligatures: bool = True) -> List[ShapedGlyph]:
        """Shape `text` into positioned glyphs in VISUAL order (left to right on the page), in font
        units with y up. It makes the same decisions as draw_text (joined letter shapes, GSUB
        features, mirrored brackets, right-to-left order, kerning) without rounding anything to
        pixels, so a vector renderer can place the glyphs itself.

        `direction` is "auto" (that of the first strong letter, as draw_text does), "ltr" or "rtl".
        `kerning` is as in glyph_kerning; it is added to the advance of the first glyph of a pair.
        `ligatures=False` stops the optional ligature features (liga, clig); the required ones
        (lam + alef) and the joining shapes always run. A "\\n" is a glyph 0 with no advance:
        start the next line (its pitch is design_metrics.ascent - descent + line_gap) and move the
        pen back to 0. Marks take no advance and carry an x_offset / y_offset.

        Only right-to-left scripts get the GSUB treatment so far: other text is one glyph per
        character. Raises ValueError (malformed text, unknown direction or kerning), LimitError.

        Place the glyphs by walking the pen along the line::

            pen = 0.0
            for g in font.shape("text"):
                x = pen + g.x_offset   # draw font.outline(g.glyph) with its origin at x
                pen += g.x_advance
        """
        return [ShapedGlyph(*t) for t in self._f.shape(text, direction, kerning, ligatures)]

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
