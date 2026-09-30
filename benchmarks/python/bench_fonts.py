"""
bench_fonts.py: stb_truetype (libstb.Font) against Pillow's FreeType
binding, on a real TrueType font (DejaVu Sans, downloaded by
cmake/FetchRealCorpus.cmake).

Three operations, each at several pixel sizes and for a short
single-line string and a multi-line paragraph:

  open      load the font file and get a font object ready to draw
            with. libstb.Font.open(path) vs ImageFont.truetype(path, px).
  measure   how wide/tall would this text be, without drawing it.
            Font.measure vs getlength / multiline_textbbox.
  render    text in, a 1-channel coverage bitmap out, including
            measuring and allocating the canvas (libstb's
            Font.render does all of that in one call; with Pillow that
            is a bounding-box query, a new image and a draw call, and
            all three are inside the timed region).

Fairness notes, which matter more here than anywhere else in the suite:
  - Pillow is forced onto its BASIC layout engine. With libraqm
    installed Pillow would otherwise shape text (ligatures, complex
    scripts), which stb_truetype does not do at all; comparing them
    would charge Pillow for a feature the other side does not have.
  - Neither result is "the same picture". FreeType applies its own
    glyph hinting by default and stb_truetype does not, and the two
    rasterize antialiasing differently. Only speed is compared here,
    so read these numbers as "how fast does each produce a usable text
    bitmap", not "how fast do they produce identical pixels".
  - stb_truetype has NO SECURITY GUARANTEE on untrusted font files (see
    the libstb README). FreeType is hardened and fuzzed. That is a real
    difference between the two that no speed number captures.
"""
import argparse
import gc
import json
import sys
import time

SIZES = [16, 32, 64]
TEXTS = {
    "short": "Hello, world!",
    "paragraph": ("The quick brown fox jumps over the lazy dog.\n"
                  "Pack my box with five dozen liquor jugs.\n"
                  "How vexingly quick daft zebras jump!"),
}
REPEATS = 15


def _best(fn, repeats):
    best = None
    for _ in range(repeats):
        gc.collect()
        t0 = time.perf_counter()
        fn()
        dt = time.perf_counter() - t0
        best = dt if best is None or dt < best else best
    return best


def _cell(build, repeats):
    try:
        fn = build()
        fn()  # untimed warm-up
        return {"available": True, "seconds": _best(fn, repeats)}
    except Exception as e:  # noqa: BLE001
        return {"available": False, "error": f"{type(e).__name__}: {e}"}


def run(font_path, repeats=REPEATS):
    import libstb
    from PIL import Image, ImageDraw, ImageFont

    basic = ImageFont.Layout.BASIC
    libstb_font = libstb.Font.open(font_path)
    pil_fonts = {px: ImageFont.truetype(font_path, px, layout_engine=basic) for px in SIZES}
    measure_canvas = ImageDraw.Draw(Image.new("L", (1, 1)))

    cases = []

    # ---- open (independent of text) ----
    for px in SIZES:
        cases.append({
            "op": "open", "text": None, "chars": None, "px": px,
            "libraries": {
                "libstb": _cell(lambda: (lambda: libstb.Font.open(font_path)), repeats),
                "pillow": _cell(lambda px=px: (lambda: ImageFont.truetype(font_path, px, layout_engine=basic)), repeats),
            },
        })

    for label, text in TEXTS.items():
        multiline = "\n" in text
        for px in SIZES:
            pf = pil_fonts[px]

            def libstb_measure(px=px, text=text):
                return lambda: libstb_font.measure(text, px)

            def pillow_measure(pf=pf, text=text):
                return lambda: measure_canvas.multiline_textbbox((0, 0), text, font=pf)

            def libstb_render(px=px, text=text):
                return lambda: libstb_font.render(text, px)

            def pillow_render(pf=pf, text=text):
                def go():
                    _l, _t, r, b = measure_canvas.multiline_textbbox((0, 0), text, font=pf)
                    im = Image.new("L", (max(1, r), max(1, b)))
                    ImageDraw.Draw(im).multiline_text((0, 0), text, font=pf, fill=255)
                    return im
                return go

            for op, ls, pl in (("measure", libstb_measure, pillow_measure), ("render", libstb_render, pillow_render)):
                cases.append({
                    "op": op, "text": label, "chars": len(text), "lines": text.count("\n") + 1, "px": px,
                    "libraries": {"libstb": _cell(ls, repeats), "pillow": _cell(pl, repeats)},
                })
    return {"font": font_path.rsplit("/", 1)[-1], "repeats": repeats,
            "metric": "best (min) wall-clock seconds over repeats", "cases": cases}


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("font", help="path to a .ttf file")
    p.add_argument("output", help="path to write results JSON")
    p.add_argument("--repeats", type=int, default=REPEATS)
    args = p.parse_args()

    result = run(args.font, repeats=args.repeats)
    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    print(f"bench_fonts: {len(result['cases'])} cases -> {args.output}", file=sys.stderr)
