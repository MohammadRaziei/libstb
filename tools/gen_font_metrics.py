#!/usr/bin/env python3
"""Regenerates the font-free text estimate tables in src/font/font.cpp.

Usage:  python tools/gen_font_metrics.py [FONT_DIR]

Reads Liberation Sans, Serif and Mono (SIL OFL 1.1; metric compatible with Arial, Times New
Roman and Courier New) from FONT_DIR (default /usr/share/fonts/truetype/liberation) and
rewrites the block between the BEGIN/END GENERATED markers. Only numbers (advance widths and
vertical metrics) are copied; no font file is added to the repository. Needs fontTools.
"""
import pathlib
import sys

from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
TARGET = ROOT / "src" / "font" / "font.cpp"
BEGIN = "// BEGIN GENERATED (tools/gen_font_metrics.py), do not edit by hand"
END = "// END GENERATED"

FAMILIES = [("sans", "LiberationSans"), ("serif", "LiberationSerif"), ("mono", "LiberationMono")]
LOW, HIGH = 0x20, 0xFF  # table covers Basic Latin + Latin-1 (control characters get 0)
EXTRA = [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x20AC, 0x2122]


def load(font_dir, stem):
    f = TTFont(font_dir / f"{stem}-Regular.ttf")
    assert f["head"].unitsPerEm == 2048, "tables are stored in 1/2048 em"
    cmap, hmtx = f.getBestCmap(), f["hmtx"]

    def adv(cp):
        return hmtx[cmap[cp]][0] if cp in cmap else 0

    letters = [adv(c) for c in list(range(0x41, 0x5B)) + list(range(0x61, 0x7B))]
    hhea = f["hhea"]
    return {
        "table": [adv(c) if not 0x7F <= c <= 0x9F else 0 for c in range(LOW, HIGH + 1)],
        "extra": [adv(c) for c in EXTRA],
        "fallback": round(sum(letters) / len(letters)),
        "asc": hhea.ascent, "desc": hhea.descent, "gap": hhea.lineGap,
    }


def rows(values, per_line=16):
    return "\n".join("    " + ", ".join(str(v) for v in values[i:i + per_line]) + ","
                     for i in range(0, len(values), per_line))


def main():
    font_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/liberation")
    out = [BEGIN,
           f"// All values are in 1/2048 em. Latin table: U+{LOW:04X}..U+{HIGH:04X}; extras: " +
           ", ".join(f"U+{c:04X}" for c in EXTRA) + ".",
           "constexpr int estimate_low = 0x%X, estimate_high = 0x%X;" % (LOW, HIGH),
           "constexpr char32_t estimate_extra_cps[%d] = {%s};" % (len(EXTRA), ", ".join("0x%X" % c for c in EXTRA)),
           "struct estimate_family {",
           "    std::uint16_t latin[estimate_high - estimate_low + 1];",
           "    std::uint16_t extra[%d];" % len(EXTRA),
           "    std::uint16_t fallback;  // any other narrow character",
           "    int ascent, descent, line_gap;",
           "};",
           "const estimate_family estimate_families[3] = {"]
    for name, stem in FAMILIES:
        d = load(font_dir, stem)
        out += ["    {  // " + name + " (" + stem + ")", "    {", rows(d["table"]), "    },",
                "    {" + ", ".join(str(v) for v in d["extra"]) + "},",
                f"    {d['fallback']},", f"    {d['asc']}, {d['desc']}, {d['gap']},", "    },"]
    out += ["};", END]
    text = TARGET.read_text()
    a, b = text.index(BEGIN), text.index(END) + len(END)
    TARGET.write_text(text[:a] + "\n".join(out) + text[b:])
    print("rewrote", TARGET)


if __name__ == "__main__":
    main()
