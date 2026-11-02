"""Regenerates libstb-curves-test.ttf, libstb-cff-test.otf and libstb-test.ttc (needs `pip install fonttools`).

Run libstb's make_test_font.py first: the .ttc holds libstb-test.ttf and libstb-curves-test.ttf.

libstb-curves-test.ttf: TrueType outlines, unitsPerEm=2048, ascent 1600, descent -400, lineGap 100.

  glyph    codepoint  advance  outline (font units)
  .notdef  -          1000     rectangle 100..900 x 0..1400
  space    U+0020      600     (empty)
  A        U+0041     1200     rectangle 200..1000 x 0..1400
  B        U+0042     1000     rectangle 200..800 x 0..700
  Q        U+0051     1100     on (0,0) on (1000,0) OFF (1000,1000) on (0,1000): lines and one quadratic
  O        U+004F     1100     four off-curve points (0,0) (1000,0) (1000,1000) (0,1000): no on-curve point
  V        U+0056     1200     a composite: A moved by (50, 20)
  pair A B: legacy `kern` table -100 AND GPOS pair adjustment -50 (so the kerning modes differ)

libstb-cff-test.otf: CFF outlines, unitsPerEm=1000, ascent 800, descent -200, lineGap 0.

  .notdef  -           500     rectangle 50..450 x 0..700
  space    U+0020      300     (empty)
  C        U+0043      900     (0,0) -> (1000,0) -> cubic (1000,500) (500,1000) (0,1000), closed
"""
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTCollection, TTFont, newTable
from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0


def tt_rect(x0, y0, x1, y1):
    pen = TTGlyphPen(None)
    pen.moveTo((x0, y0)); pen.lineTo((x0, y1)); pen.lineTo((x1, y1)); pen.lineTo((x1, y0)); pen.closePath()
    return pen.glyph()


def tt_empty():
    return TTGlyphPen(None).glyph()


def tt_bowl():
    pen = TTGlyphPen(None)
    pen.moveTo((0, 0)); pen.lineTo((1000, 0)); pen.qCurveTo((1000, 1000), (0, 1000)); pen.closePath()
    return pen.glyph()


def tt_all_off():
    pen = TTGlyphPen(None)
    pen.qCurveTo((0, 0), (1000, 0), (1000, 1000), (0, 1000), None)
    pen.closePath()
    return pen.glyph()


# ------------------------------------------------------------------ libstb-curves-test.ttf
a = tt_rect(200, 0, 1000, 1400)
pen = TTGlyphPen({"A": a})
pen.addComponent("A", (1, 0, 0, 1, 50, 20))
v = pen.glyph()
glyphs = {
    ".notdef": (1000, tt_rect(100, 0, 900, 1400), 100),
    "space": (600, tt_empty(), 0),
    "A": (1200, a, 200),
    "B": (1000, tt_rect(200, 0, 800, 700), 200),
    "Q": (1100, tt_bowl(), 0),
    "O": (1100, tt_all_off(), 0),
    "V": (1200, v, 250),
}
fb = FontBuilder(2048, isTTF=True)
fb.setupGlyphOrder(list(glyphs))
fb.setupCharacterMap({0x20: "space", 0x41: "A", 0x42: "B", 0x51: "Q", 0x4F: "O", 0x56: "V"})
fb.setupGlyf({n: g for n, (_, g, _) in glyphs.items()})
fb.setupHorizontalMetrics({n: (adv, lsb) for n, (adv, _, lsb) in glyphs.items()})
fb.setupHorizontalHeader(ascent=1600, descent=-400, lineGap=100)
fb.setupNameTable({"familyName": "LibstbCurvesTest", "styleName": "Regular"})
fb.setupOS2(sTypoAscender=1600, sTypoDescender=-400, sTypoLineGap=100, usWinAscent=1600, usWinDescent=400)
fb.setupPost()
kern = newTable("kern")
kern.version = 0
sub = KernTable_format_0()
sub.version, sub.coverage, sub.format = 0, 1, 0
sub.kernTable = {("A", "B"): -100}
kern.kernTables = [sub]
fb.font["kern"] = kern
addOpenTypeFeaturesFromString(fb.font, "languagesystem DFLT dflt; feature kern { pos A B -50; } kern;")
fb.save("libstb-curves-test.ttf")


# -------------------------------------------------------------------- libstb-cff-test.otf
def cff(width, draw=None):
    pen = T2CharStringPen(width, None)
    if draw:
        draw(pen)
    return pen.getCharString()


def cff_rect(x0, y0, x1, y1):
    def draw(pen):
        pen.moveTo((x0, y0)); pen.lineTo((x0, y1)); pen.lineTo((x1, y1)); pen.lineTo((x1, y0)); pen.closePath()
    return draw


def cff_cubic(pen):
    pen.moveTo((0, 0)); pen.lineTo((1000, 0)); pen.curveTo((1000, 500), (500, 1000), (0, 1000)); pen.closePath()


cff_glyphs = {
    ".notdef": (500, cff(500, cff_rect(50, 0, 450, 700)), 50),
    "space": (300, cff(300), 0),
    "C": (900, cff(900, cff_cubic), 0),
}
fb = FontBuilder(1000, isTTF=False)
fb.setupGlyphOrder(list(cff_glyphs))
fb.setupCharacterMap({0x20: "space", 0x43: "C"})
fb.setupCFF("LibstbCffTest-Regular", {"FullName": "LibstbCffTest Regular"}, {n: c for n, (_, c, _) in cff_glyphs.items()}, {})
fb.setupHorizontalMetrics({n: (adv, lsb) for n, (adv, _, lsb) in cff_glyphs.items()})
fb.setupHorizontalHeader(ascent=800, descent=-200, lineGap=0)
fb.setupNameTable({"familyName": "LibstbCffTest", "styleName": "Regular"})
fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0, usWinAscent=800, usWinDescent=200)
fb.setupPost()
fb.save("libstb-cff-test.otf")

# ------------------------------------------------------------------------ libstb-test.ttc
collection = TTCollection()
collection.fonts = [TTFont("libstb-test.ttf"), TTFont("libstb-curves-test.ttf")]
collection.save("libstb-test.ttc")
