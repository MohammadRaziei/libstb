"""Fonts at the glyph level: font-unit metrics, glyph outlines, kerning, shaping, and a text renderer of your own.

    python 13_font_glyphs_and_shape.py [path/to/font.ttf]

The default font is data/fonts/Vazirmatn-Regular.ttf (Persian, Arabic and Latin; SIL OFL 1.1).

draw_text and Canvas.add_text do all of this for you, in pixels. The glyph-level API hands you the pieces in
FONT UNITS (the font's own design grid) with y pointing UP, so you can draw text at any size, angle or shear,
filled or stroked, through a Matrix. Only use fonts you trust: the font reader checks its bounds but has not
been fuzzed. A font without Arabic letters draws .notdef boxes for the Persian lines.
"""
import sys
from pathlib import Path

import numpy as np

import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

path = Path(sys.argv[1]) if len(sys.argv) > 1 else DATA / "fonts" / "Vazirmatn-Regular.ttf"
font = stb.Font.open(path)

# 1. The font's grid and what it holds. Font.face_count(path_or_bytes) says how many faces a .ttc has.
d = font.design_metrics
print("units per em :", d.units_per_em, "| ascent", d.ascent, "descent", d.descent, "line gap", d.line_gap)
print("glyphs       :", font.glyph_count, "| faces in the file:", stb.Font.face_count(path))

# 2. A character is a glyph index (0 is .notdef, "no glyph"); metrics and outline are in font units.
a, v = font.glyph_index("A"), font.glyph_index("V")
print("glyph of A   :", a, font.glyph_info(a))                       # GlyphMetrics(advance, lsb, x_min, ...)
print("has U+1F600  :", font.has_glyph("\U0001F600"))
commands = font.outline(a)                                         # [("M", x, y), ("L", ...), ("Q", ...), ("Z",)]
print("outline of A :", len(commands), "commands, the first two:", commands[:2])
outline = stb.Outline(commands)                                    # the same commands as any stb.Outline

# 3. Kerning between two glyphs: from the kern table, from GPOS, or none.
for mode in ("none", "kern_table", "automatic"):
    print(f"kerning A,V {mode:>10s}:", font.glyph_kerning(a, v, mode))

# 4. Shaping: what draw_text decides, as data. Visual order (left to right on the page), one entry per glyph.
latin = font.shape("AV")
print("shape 'AV'   :", latin)                                      # ShapedGlyph(glyph, cluster, x_advance, ...)
persian = "سلام دنیا"
shaped = font.shape(persian)
print("shape persian:", [(g.glyph, g.cluster) for g in shaped])     # the clusters run backwards: right to left

# 5. Your own text renderer on top of it.
def draw_text(img, font, text, size, x, baseline, color, *, angle=0.0, stroke=None):
    """Draws `text` (lines split on '\\n') as vector outlines; its first pen position is (x, baseline).

    `angle` is in radians, clockwise on the screen. `stroke` is (color, width_in_pixels) for an outline.
    """
    d = font.design_metrics
    scale = size / (d.ascent - d.descent)                  # font units -> pixels: the scale draw_text uses for its `size`
    pitch = (d.ascent - d.descent + d.line_gap) * scale    # from one baseline to the next
    pen, line = 0.0, 0
    for g in font.shape(text):
        if g.glyph == 0:                                   # a line break: no glyph, no advance
            line, pen = line + 1, 0.0
            continue
        # Each call applies to a point FIRST, so read this from the bottom up: scale and flip y (font units are
        # y up, pixels y down), move the glyph to its place on the line, turn the line, put it on the image.
        m = (
            stb.Matrix()
            .translate(x, baseline)
            .rotate(angle)
            .translate((pen + g.x_offset) * scale, line * pitch - g.y_offset * scale)
            .scale(scale, -scale)
        )
        glyph = stb.Outline(font.outline(g.glyph))
        img = img.fill_outline(glyph, color, matrix=m)
        if stroke:
            img = img.stroke_outline(glyph, stroke[0], stroke[1] / scale, join="round", matrix=m)   # width in font units
        pen += g.x_advance
    return img


img = stb.Image(np.full((330, 560, 3), (245, 243, 238), np.uint8))
img = draw_text(img, font, "Hello, AV kerning", 44, 20, 60, "#1e1e1e")
img = draw_text(img, font, persian, 44, 20, 120, "#1d3557")
img = draw_text(img, font, "one\ntwo lines", 30, 20, 175, "#e63946")
img = draw_text(img, font, "outlined", 56, 250, 160, "#ffd166", stroke=("#073b4c", 2))
img = draw_text(img, font, "tilted text", 40, 250, 300, "#2a9d8f", angle=-0.35)
img.write(OUT / "vector_text.png")

# The same text through draw_text is in pixels, with every glyph on a whole pixel; yours is an outline.
size = 44
mine = sum(g.x_advance for g in font.shape("Hello, AV kerning")) * size / (d.ascent - d.descent)
print(f"width of the line: {mine:.1f} px from the glyphs, {font.measure('Hello, AV kerning', size).width:.1f} px from measure()")
print("wrote", OUT / "vector_text.png")
