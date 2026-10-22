"""Builds two small subsets of Vazirmatn Regular (SIL OFL 1.1) for the right-to-left tests.

Usage:  python tests/data/make_persian_font.py /path/to/Vazirmatn-Regular.ttf

libstb-fa-test.ttf       the Persian and Arabic letters, their Arabic Presentation Forms (the shaper
                         takes letter shapes from the cmap), Persian digits, ASCII, the zero width
                         non-joiner and a few marks. No GSUB: this is the presentation-form path.
libstb-fa-gsub-test.ttf  the same letters but no presentation forms in the cmap, and the GSUB
                         features of Arabic script (ccmp, isol, fina, medi, init, rlig, calt, liga)
                         with GDEF: letter shapes can only come from GSUB.

GPOS is dropped in both because libstb does not read it. The font's name records (copyright and
license) are kept, and the license text is tests/data/OFL-Vazirmatn.txt.
"""
import pathlib
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

HERE = pathlib.Path(__file__).parent

UNICODES = (
    list(range(0x20, 0x7F))                 # ASCII
    + [0x200C, 0x200D]                      # ZWNJ, ZWJ
    + list(range(0x0621, 0x063B))           # Arabic letters
    + list(range(0x0640, 0x0656))           # tatweel, letters, marks
    + [0x067E, 0x0686, 0x0698, 0x06A9, 0x06AF, 0x06CC]   # Persian letters
    + list(range(0x06F0, 0x06FA))           # Persian digits
    + list(range(0x0660, 0x066A))           # Arabic-Indic digits
    + [0x060C, 0x061B, 0x061F]              # comma, semicolon, question mark
    + [0xFB56, 0xFB57, 0xFB58, 0xFB59, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D, 0xFB8A, 0xFB8B,
       0xFB8E, 0xFB8F, 0xFB90, 0xFB91, 0xFB92, 0xFB93, 0xFB94, 0xFB95,
       0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF]      # peh, cheh, jeh, keheh, gaf, farsi yeh forms
    + list(range(0xFE70, 0xFEFD))           # Arabic presentation forms B
)


LETTERS = [u for u in UNICODES if not (0xFB50 <= u <= 0xFDFF or 0xFE70 <= u <= 0xFEFC)]


def build(src: str, name: str, unicodes, features) -> None:
    opts = subset.Options()
    opts.layout_features = features
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    opts.hinting = False
    opts.drop_tables += ["GPOS", "STAT", "fvar", "gvar", "avar", "HVAR", "MVAR"]
    if not features:
        opts.drop_tables += ["GSUB", "GDEF"]
    font = TTFont(src, recalcTimestamp=False)  # reproducible: the head table keeps its dates
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=unicodes)
    sub.subset(font)
    out = HERE / name
    font.save(out)
    print(out, out.stat().st_size, "bytes")


def main(src: str) -> None:
    build(src, "libstb-fa-test.ttf", UNICODES, [])
    build(src, "libstb-fa-gsub-test.ttf", LETTERS,
          ["ccmp", "locl", "isol", "fina", "medi", "init", "rlig", "calt", "liga"])


if __name__ == "__main__":
    main(sys.argv[1])
