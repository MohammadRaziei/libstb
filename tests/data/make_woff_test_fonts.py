"""Regenerates libstb-test.woff, libstb-curves-test.woff and libstb-cff-test.woff (needs `pip install fonttools`).

Run make_test_font.py and make_outline_test_fonts.py first: each .woff holds the font of the same name
(libstb-test.ttf, libstb-curves-test.ttf, libstb-cff-test.otf) as a WOFF 1.0 file, whose tables fontTools
compresses one by one with zlib (a table that does not get smaller is stored as it is).

The script checks its own output: every table of the WOFF, unpacked here with zlib, must equal the raw table of the
original font byte for byte, and the three files together must hold both kinds of table (stored and compressed), since
libstb's tests expect both.
"""
import pathlib
import struct
import sys
import zlib

from fontTools.ttLib import TTFont

HERE = pathlib.Path(__file__).parent
FONTS = [
    ("libstb-test.ttf", "libstb-test.woff"),
    ("libstb-curves-test.ttf", "libstb-curves-test.woff"),
    ("libstb-cff-test.otf", "libstb-cff-test.woff"),
]


def make(src, dst):
    font = TTFont(HERE / src, recalcBBoxes=False, recalcTimestamp=False)
    font.flavor = "woff"
    font.save(HERE / dst)


def tables(woff_bytes):
    """(tag, comp_length, orig_length, unpacked bytes) of every table of a WOFF 1.0 file."""
    sig, flavor, length, count = struct.unpack(">4sIIH", woff_bytes[:14])
    assert sig == b"wOFF" and length == len(woff_bytes)
    out = []
    for i in range(count):
        tag, offset, comp, orig, _checksum = struct.unpack(">4sIIII", woff_bytes[44 + 20 * i:64 + 20 * i])
        raw = woff_bytes[offset:offset + comp]
        out.append((tag.decode("latin-1"), comp, orig, zlib.decompress(raw) if comp < orig else raw))
    return out


def main():
    stored = compressed = 0
    for src, dst in FONTS:
        make(src, dst)
        reader = TTFont(HERE / src, lazy=True).reader
        for tag, comp, orig, data in tables((HERE / dst).read_bytes()):
            assert data == reader[tag], f"{dst}: table {tag!r} differs from {src}"
            assert len(data) == orig
            stored += comp == orig
            compressed += comp < orig
        print(f"{dst}: {(HERE / dst).stat().st_size} bytes")
    assert stored and compressed, "the test fonts should hold stored and compressed tables"
    print(f"{compressed} compressed and {stored} stored tables, all equal to the original")


if __name__ == "__main__":
    sys.exit(main())
