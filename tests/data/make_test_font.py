"""Regenerates libstb-test.ttf (needs `pip install fonttools`).

A tiny synthetic font whose glyphs are solid rectangles, so tests can assert
exact pixel values. unitsPerEm=1000, ascent=800, descent=-200, lineGap=0:
at pixel_height=100 one font unit is exactly 0.1 px.

  glyph  codepoint  advance  ink box (x0..x1, y0..y1) in font units
  .notdef  -          500     50..450   x 0..700
  space    U+0020     300     (empty)
  A        U+0041     600     100..500  x 0..700
  B        U+0042     500     100..400  x 0..350
  eacute   U+00E9     400     0..300    x 0..300     (2-byte UTF-8)
  smile    U+1F600    700     0..500    x 0..500     (4-byte UTF-8)
  legacy 'kern' table: (A, B) = -100
"""
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0

GLYPHS = {  # name: (advance, box or None)
    ".notdef": (500, (50, 0, 450, 700)),
    "space": (300, None),
    "A": (600, (100, 0, 500, 700)),
    "B": (500, (100, 0, 400, 350)),
    "eacute": (400, (0, 0, 300, 300)),
    "smile": (700, (0, 0, 500, 500)),
}
CMAP = {0x20: "space", 0x41: "A", 0x42: "B", 0xE9: "eacute", 0x1F600: "smile"}


def glyph(box):
    pen = TTGlyphPen(None)
    if box:
        x0, y0, x1, y1 = box
        pen.moveTo((x0, y0)); pen.lineTo((x0, y1)); pen.lineTo((x1, y1)); pen.lineTo((x1, y0)); pen.closePath()
    return pen.glyph()


fb = FontBuilder(1000, isTTF=True)
fb.setupGlyphOrder(list(GLYPHS))
fb.setupCharacterMap(CMAP)
fb.setupGlyf({n: glyph(b) for n, (_, b) in GLYPHS.items()})
fb.setupHorizontalMetrics({n: (a, b[0] if b else 0) for n, (a, b) in GLYPHS.items()})
fb.setupHorizontalHeader(ascent=800, descent=-200, lineGap=0)
fb.setupNameTable({"familyName": "LibstbTest", "styleName": "Regular"})
fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0, usWinAscent=800, usWinDescent=200)
fb.setupPost()

kern = newTable("kern")
kern.version = 0
sub = KernTable_format_0()
sub.version, sub.coverage, sub.format = 0, 1, 0
sub.kernTable = {("A", "B"): -100}
kern.kernTables = [sub]
fb.font["kern"] = kern

fb.save("libstb-test.ttf")
