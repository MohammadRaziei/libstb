"""Regenerates libstb-fa-gpos-test.ttf (needs `pip install fonttools`).

A synthetic Arabic font for the GPOS mark attachment tests: solid rectangles, unitsPerEm=1000, no
GSUB, GDEF glyph classes, and GPOS features `mark` and `mkmk` (script arab) whose anchors are
chosen so that the expected positions can be worked out by hand.

  glyph    codepoint  advance  ink box (x0 y0 x1 y1)   GDEF
  .notdef  -          500      50 0 450 700            base
  beh      U+0628     600      50 0 550 300            base
  lam      U+0644     300      100 0 200 800           base
  alef     U+0627     300      100 0 200 800           base
  lamalef  U+FEFB     900      50 0 850 800            ligature
  fatha    U+064E     0        0 0 200 80              mark
  kasra    U+0650     0        0 0 200 80              mark
  shadda   U+0651     0        0 0 300 120             mark

Anchors (x y):
  mark to base      beh above (300 700), beh below (300 -100)
  mark to ligature  lamalef above, first component (200 900), second component (700 900)
  marks             fatha above (100 0), shadda above (150 0), kasra below (100 80)
  mark to mark      shadda above (150 300): a fatha on a shadda goes there

So on beh (advance 600): fatha is drawn at (300-100, 700-0) = (200, 700) from the origin of beh,
a kasra at (300-100, -100-80) = (200, -180), a shadda at (150, 700) and a fatha on that shadda at
(150+150-100, 700+300-0) = (200, 1000).
"""
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable

GLYPHS = {  # name: (advance, box)
    ".notdef": (500, (50, 0, 450, 700)),
    "beh": (600, (50, 0, 550, 300)),
    "lam": (300, (100, 0, 200, 800)),
    "alef": (300, (100, 0, 200, 800)),
    "lamalef": (900, (50, 0, 850, 800)),
    "fatha": (0, (0, 0, 200, 80)),
    "kasra": (0, (0, 0, 200, 80)),
    "shadda": (0, (0, 0, 300, 120)),
}
CMAP = {0x0628: "beh", 0x0644: "lam", 0x0627: "alef", 0xFEFB: "lamalef",
        0x064E: "fatha", 0x0650: "kasra", 0x0651: "shadda"}

FEATURES = """
languagesystem DFLT dflt;
languagesystem arab dflt;

markClass fatha <anchor 100 0> @ABOVE;
markClass shadda <anchor 150 0> @ABOVE;
markClass kasra <anchor 100 80> @BELOW;

feature mark {
    pos base beh <anchor 300 700> mark @ABOVE <anchor 300 -100> mark @BELOW;
    pos ligature lamalef <anchor 200 900> mark @ABOVE ligComponent <anchor 700 900> mark @ABOVE;
} mark;

feature mkmk {
    pos mark shadda <anchor 150 300> mark @ABOVE;
} mkmk;
"""


def glyph(box):
    pen = TTGlyphPen(None)
    x0, y0, x1, y1 = box
    pen.moveTo((x0, y0)); pen.lineTo((x0, y1)); pen.lineTo((x1, y1)); pen.lineTo((x1, y0)); pen.closePath()
    return pen.glyph()


fb = FontBuilder(1000, isTTF=True)
fb.setupGlyphOrder(list(GLYPHS))
fb.setupCharacterMap(CMAP)
fb.setupGlyf({n: glyph(b) for n, (_, b) in GLYPHS.items()})
fb.setupHorizontalMetrics({n: (a, b[0]) for n, (a, b) in GLYPHS.items()})
fb.setupHorizontalHeader(ascent=800, descent=-200, lineGap=0)
fb.setupNameTable({"familyName": "LibstbMarkTest", "styleName": "Regular"})
fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, sTypoLineGap=0, usWinAscent=800, usWinDescent=200)
fb.setupPost()

gdef = newTable("GDEF")
gdef.table = __import__("fontTools.ttLib.tables.otTables", fromlist=["x"]).GDEF()
gdef.table.Version = 0x00010000
classes = {"beh": 1, "lam": 1, "alef": 1, ".notdef": 1, "lamalef": 2, "fatha": 3, "kasra": 3, "shadda": 3}
ot = __import__("fontTools.ttLib.tables.otTables", fromlist=["x"])
gdef.table.GlyphClassDef = ot.GlyphClassDef()
gdef.table.GlyphClassDef.classDefs = classes
gdef.table.AttachList = None
gdef.table.LigCaretList = None
gdef.table.MarkAttachClassDef = None
fb.font["GDEF"] = gdef

addOpenTypeFeaturesFromString(fb.font, FEATURES)
fb.save("libstb-fa-gpos-test.ttf")
