"""Compares libstb's shaping of random vowelled Persian text with HarfBuzz's, font by font.

    PYTHONPATH=<build>/python/install python3 tools/compare_with_harfbuzz.py FONT.ttf [FONT.ttf ...]
        [--count 300] [--seed 7] [--no-kern]

For every text both shapers must give the same glyphs; then the position of every glyph (the pen
before it plus its offset, in font units) is compared. Prints, per font, how many texts agree to
within one unit and the worst difference. Needs `uharfbuzz`. Marks are typed in any order, so this
also checks the order libstb puts them in.
"""
import argparse
import random
import sys

import uharfbuzz as hb

import libstb

LETTERS = [chr(c) for c in list(range(0x0628, 0x063B)) + list(range(0x0641, 0x064B)) +
           [0x067E, 0x0686, 0x0698, 0x06A9, 0x06AF, 0x06CC]]
MARKS = [chr(c) for c in (0x064B, 0x064C, 0x064D, 0x064E, 0x064F, 0x0650, 0x0651, 0x0652)]


def random_text(rng):
    words = []
    for _ in range(rng.randint(1, 4)):
        word = ""
        for _ in range(rng.randint(1, 5)):
            word += rng.choice(LETTERS) + "".join(rng.choice(MARKS) for _ in range(rng.choice([0, 0, 1, 1, 2])))
        words.append(word)
    return " ".join(words)


def harfbuzz_positions(font, text, features):
    buf = hb.Buffer()
    buf.add_str(text)
    buf.direction, buf.script, buf.language = "rtl", "Arab", "ar"
    hb.shape(font, buf, features)
    out, pen = [], 0
    for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
        out.append((info.codepoint, pen + pos.x_offset, pos.y_offset))
        pen += pos.x_advance
    return out


def libstb_positions(font, text, kerning):
    out, pen = [], 0.0
    for g in font.shape(text, direction="rtl", kerning=kerning):
        out.append((g.glyph, pen + g.x_offset, g.y_offset))
        pen += g.x_advance
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fonts", nargs="+")
    ap.add_argument("--count", type=int, default=300)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--no-kern", action="store_true", help="kerning off on both sides")
    args = ap.parse_args()

    features = {"kern": False, "curs": False} if args.no_kern else {}
    for path in args.fonts:
        ours = libstb.Font.open(path)
        theirs = hb.Font(hb.Face(hb.Blob.from_file_path(path)))
        rng = random.Random(args.seed)
        same_glyphs = close = 0
        worst = 0.0
        example = None
        for _ in range(args.count):
            text = random_text(rng)
            a = harfbuzz_positions(theirs, text, features)
            b = libstb_positions(ours, text, "none" if args.no_kern else "automatic")
            if sorted(g for g, _, _ in a) != sorted(g for g, _, _ in b):
                continue
            same_glyphs += 1
            diff = max(max(abs(x1 - x2), abs(y1 - y2)) for (_, x1, y1), (_, x2, y2) in zip(sorted(a), sorted(b)))
            worst = max(worst, diff)
            if diff <= 1:
                close += 1
            elif example is None:
                example = (text, diff)
        print(f"{path}: same glyphs {same_glyphs}/{args.count}, positions within 1 unit {close}, worst {worst:.0f}"
              + (f", e.g. {example[0]!r} ({example[1]:.0f})" if example else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
