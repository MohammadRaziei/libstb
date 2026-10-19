"""Render text with a TrueType font: metrics, a text bitmap, and a glyph atlas.

Usage: python 05_font_render.py [path/to/font.ttf]

Only use fonts you trust: stb_truetype does no bounds checking.
"""
import sys
from pathlib import Path

import numpy as np

import libstb as stb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)

CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/Library/Fonts/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/arial.ttf",
]


def find_font():
    if len(sys.argv) > 1:
        return Path(sys.argv[1])
    for c in CANDIDATES:
        if Path(c).exists():
            return Path(c)
    sys.exit("No font found: pass one as an argument.")


font = stb.Font.open(find_font())
size = 32

print("metrics :", font.metrics(size))
print("advance :", font.advance("A", size))
print("kerning :", font.kerning("A", "V", size))
print("measure :", font.measure("Hello", size))

# A whole (multi-line, UTF-8) text as a 1-channel coverage bitmap.
text = font.render("Hello\nworld", size)
print("text    :", text.bitmap.width, "x", text.bitmap.height,
      "origin", (text.origin_x, text.origin_y))
text.bitmap.write(OUT / "hello.png")

# Draw it straight onto an image: draw_text returns a new Image. (x, y) is the top-left
# of the text; color is an int, (r, g, b) or (r, g, b, a).
ramp = (40 + np.arange(480) * 3 // 4).astype(np.uint8)            # a plain gradient (needs numpy)
background = stb.Image(np.stack([np.tile(ramp, (120, 1))] * 2 + [np.full((120, 480), 160, np.uint8)], axis=-1))
labeled = background.draw_text("Hello\nworld", font, size, 20, 16, color=(255, 255, 255))
labeled.write(OUT / "labeled.png")

# One glyph.
glyph = font.render_glyph("A", size)
print("glyph A :", glyph.bitmap.width, "x", glyph.bitmap.height,
      "offset", (glyph.x_offset, glyph.y_offset), "advance", glyph.advance)

# A packed glyph sheet plus a lookup table.
atlas = font.make_atlas("ABCabc123", size, 256, 256)
atlas.image.write(OUT / "atlas.png")
g = atlas["A"]
print("atlas A :", (g.x0, g.y0, g.x1, g.y1))
