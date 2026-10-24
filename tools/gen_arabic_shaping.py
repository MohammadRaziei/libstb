#!/usr/bin/env python3
"""Regenerates the Arabic joining tables in src/font/shaper/arabic.cpp.

Usage:  python tools/gen_arabic_shaping.py

Reads Python's unicodedata (the Unicode Character Database: the <isolated>, <initial>,
<medial> and <final> decompositions of the Arabic Presentation Forms blocks, and the nonspacing
marks of the Arabic blocks) and rewrites the block between the BEGIN/END GENERATED markers.
Only numbers are copied. The joining type of a letter follows from its forms (src/font/shaper/arabic.cpp):
it joins on both sides if it has an initial or medial form, only to the previous letter if it has a
final form and nothing else, and not at all if it has just an isolated form (hamza).
"""
import pathlib
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent
TARGET = ROOT / "src" / "font" / "shaper" / "arabic.cpp"
BEGIN = "// BEGIN GENERATED (tools/gen_arabic_shaping.py), do not edit by hand"
END = "// END GENERATED"

FORM_INDEX = {"<isolated>": 0, "<final>": 1, "<initial>": 2, "<medial>": 3}
PRESENTATION = list(range(0xFB50, 0xFE00)) + list(range(0xFE70, 0xFF00))
LAM = 0x0644
ALEFS = (0x0622, 0x0623, 0x0625, 0x0627)
MARK_BLOCKS = [(0x0600, 0x06FF), (0x0750, 0x077F), (0x08A0, 0x08FF)]


def collect():
    forms, lam_alef = {}, {}
    for cp in PRESENTATION:
        dec = unicodedata.decomposition(chr(cp)).split()
        if len(dec) < 2 or dec[0] not in FORM_INDEX:
            continue
        base = [int(x, 16) for x in dec[1:]]
        kind = FORM_INDEX[dec[0]]
        if len(base) == 1:
            forms.setdefault(base[0], [0, 0, 0, 0])[kind] = cp
        elif len(base) == 2 and base[0] == LAM and base[1] in ALEFS:
            lam_alef.setdefault(base[1], [0, 0])[kind] = cp
    return forms, lam_alef


def marks():
    out = []
    for lo, hi in MARK_BLOCKS:
        start = None
        for cp in range(lo, hi + 2):
            m = cp <= hi and unicodedata.category(chr(cp)) == "Mn"
            if m and start is None:
                start = cp
            if not m and start is not None:
                out.append((start, cp - 1))
                start = None
    return out


def render():
    forms, lam_alef = collect()
    lines = [BEGIN, f"// Unicode {unicodedata.unidata_version}", ""]
    lines.append("// base letter -> isolated, final, initial, medial presentation form (0 = none)")
    lines.append("const form_entry joining_forms[] = {")
    for base in sorted(forms):
        iso, fin, ini, med = forms[base]
        lines.append(f"    {{0x{base:04X}, 0x{iso:04X}, 0x{fin:04X}, 0x{ini:04X}, 0x{med:04X}}},")
    lines.append("};")
    lines.append("")
    lines.append("// lam + this alef -> isolated, final ligature")
    lines.append("const lam_alef_entry lam_alef_forms[] = {")
    for alef in sorted(lam_alef):
        iso, fin = lam_alef[alef]
        lines.append(f"    {{0x{alef:04X}, 0x{iso:04X}, 0x{fin:04X}}},")
    lines.append("};")
    lines.append("")
    lines.append("// nonspacing marks of the Arabic blocks: they do not take part in joining")
    lines.append("const code_range mark_ranges[] = {")
    for lo, hi in marks():
        lines.append(f"    {{0x{lo:04X}, 0x{hi:04X}}},")
    lines.append("};")
    lines.append(END)
    return "\n".join(lines)


def main():
    src = TARGET.read_text(encoding="utf-8")
    a, b = src.index(BEGIN), src.index(END) + len(END)
    TARGET.write_text(src[:a] + render() + src[b:], encoding="utf-8")
    print("rewrote", TARGET)


if __name__ == "__main__":
    main()
