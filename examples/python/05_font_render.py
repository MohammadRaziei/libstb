"""Render text with a TrueType font: metrics, a text bitmap, a picture with labels (Persian too), and a glyph atlas.

Usage: python 05_font_render.py [path/to/font.ttf]

The default font is data/fonts/Vazirmatn-Regular.ttf (Persian, Arabic and Latin; SIL OFL 1.1).
Only use fonts you trust: the font reader checks its bounds but has not been fuzzed.

For the glyph-level API (outlines, shaping, your own renderer), see 13_font_glyphs_and_shape.py.
"""
import sys
from pathlib import Path

import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

font = stb.Font.open(sys.argv[1] if len(sys.argv) > 1 else DATA / "fonts" / "Vazirmatn-Regular.ttf")
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
# of the text; color is an int, (r, g, b) or (r, g, b, a), or a name like "white".
photo = stb.Image.open(DATA / "images" / "astronaut.png")
labeled = photo.draw_text("Hello\nworld", font, size, 20, 16, color=(255, 255, 255))

# Persian and Arabic: write the text as you would type it. Joining forms, ligatures and the
# right-to-left order are done for you, and mixed text lays out in the right order.
labeled = labeled.draw_text("سلام دنیا", font, 48, 20, 110, color="#ffd166")
labeled = labeled.draw_text("libstb یعنی کتابخانه", font, 30, 20, 190, color="#9be7ff")

# Another face is just another Font.
bold = stb.Font.open(DATA / "fonts" / "Vazirmatn-Bold.ttf")
labeled = labeled.draw_text("Bold", bold, 44, 20, 400, color="white")
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
