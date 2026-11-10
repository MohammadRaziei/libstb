"""Regenerates libstb-latin-test.ttf (needs `pip install fonttools`).

A synthetic font for the shaping of left-to-right text (issue 8 of the novasvg list): solid rectangles,
unitsPerEm=1000, GSUB and GPOS features per script (latn, grek, cyrl) chosen so that the expected glyphs
and positions can be worked out by hand.

  glyph      codepoint  advance   GDEF
  .notdef    -          500       base
  space      U+0020     300       base
  f          U+0066     400       base
  i          U+0069     300       base
  fi         -          650       ligature   (latn liga: f i)
  A          U+0041     700       base
  V          U+0056     700       base
  e          U+0065     600       base
  eacute     U+00E9     600       base       (latn ccmp: eacute -> e acute)
  acute      U+0301     0         mark
  alpha      U+03B1     500       base
  beta       U+03B2     500       base
  alphabeta  -          800       ligature   (grek liga: alpha beta)
  acy        U+0430     500       base
  kacy       U+043A     500       base
  acykacy    -          850       ligature   (cyrl liga: a ka)

GPOS (script latn): kern A V = -100; mark: acute above e, e anchor (300 700), mark anchor (100 0), so
an acute on e is drawn at (300 - 100, 700 - 0) = (200, 700) from the origin of e.
Each script has only its own ligature, so a script run that is shaped with the wrong script's plan
shows up as a missing or an extra ligature.
"""
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

GLYPHS = {  # name: (advance, box or None)
    ".notdef": (500, (50, 0, 450, 700)),
    "space": (300, None),
    "f": (400, (50, 0, 350, 700)),
    "i": (300, (100, 0, 200, 700)),
    "fi": (650, (50, 0, 600, 700)),
    "A": (700, (50, 0, 650, 700)),
    "V": (700, (50, 0, 650, 700)),
    "e": (600, (50, 0, 550, 500)),
    "eacute": (600, (50, 0, 550, 500)),
    "acute": (0, (0, 0, 200, 80)),
    "alpha": (500, (50, 0, 450, 500)),
    "beta": (500, (50, 0, 450, 700)),
    "alphabeta": (800, (50, 0, 750, 700)),
    "acy": (500, (50, 0, 450, 500)),
    "kacy": (500, (50, 0, 450, 700)),
    "acykacy": (850, (50, 0, 800, 700)),
}
CMAP = {0x20: "space", 0x66: "f", 0x69: "i", 0x41: "A", 0x56: "V", 0x65: "e", 0xE9: "eacute", 0x0301: "acute",
        0x03B1: "alpha", 0x03B2: "beta", 0x0430: "acy", 0x043A: "kacy"}

FEATURES = """
languagesystem DFLT dflt;
languagesystem latn dflt;
languagesystem grek dflt;
languagesystem cyrl dflt;

markClass acute <anchor 100 0> @ABOVE;

feature ccmp {
    script latn;
    lookup ccmp_latn { sub eacute by e acute; } ccmp_latn;
} ccmp;

feature liga {
    script latn;
    lookup liga_latn { sub f i by fi; } liga_latn;
    script grek;
    lookup liga_grek { sub alpha beta by alphabeta; } liga_grek;
    script cyrl;
    lookup liga_cyrl { sub acy kacy by acykacy; } liga_cyrl;
} liga;

feature kern {
    script latn;
    pos A V -100;
} kern;

feature mark {
    script latn;
    pos base e <anchor 300 700> mark @ABOVE;
} mark;

table GDEF {
    GlyphClassDef [.notdef space f i A V e eacute alpha beta acy kacy], [fi alphabeta acykacy], [acute], ;
} GDEF;
"""


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
fb.setupNameTable({"familyName": "LibstbLatinTest", "styleName": "Regular"})
fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0, usWinAscent=800, usWinDescent=200)
fb.setupPost()
addOpenTypeFeaturesFromString(fb.font, FEATURES)
fb.save("libstb-latin-test.ttf")
