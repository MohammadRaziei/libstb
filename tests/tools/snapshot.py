"""Development check, NOT a test: does a build of libstb behave exactly like another one?

It runs a fixed battery of operations (decode, every encoder and level, resize with every
filter, geometry, channels, SIMD backends, zlib, fonts, shaping, canvas) on deterministic
inputs and writes one SHA-256 per result. Run it once on the old build and once on the new
one, then compare the two files:

    PYTHONPATH=<old install> python tests/tools/snapshot.py old.json
    PYTHONPATH=<new install> python tests/tools/snapshot.py new.json
    python tests/tools/snapshot.py --diff old.json new.json

This is for a refactor on one machine with one compiler: JPEG bytes are not a contract across
platforms (see tests/python/tolerances.py), so the test suite never compares them, but the same
compiler on the same code must give the same bytes, and any difference here is worth a look.
"""

from __future__ import annotations

import hashlib
import re
import json
import pathlib
import sys

DATA = pathlib.Path(__file__).resolve().parent.parent / "data"


def _fmt(stb, channels):
    """What a build takes for this channel count: a PixelFormat if it has one, else the number.

    Lets one script run on a build from before PixelFormat and on a newer one, so the two can
    still be compared.
    """
    return stb.PixelFormat(channels) if hasattr(stb, "PixelFormat") else channels


def _info(value):
    """(width, height, channels) whatever the build's ImageInfo holds (a number or a PixelFormat)."""
    return (value.width, value.height, value.channels)


def _digest(value) -> str:
    h = hashlib.sha256()
    if isinstance(value, (bytes, bytearray, memoryview)):
        h.update(bytes(value))
    elif hasattr(value, "tobytes") and hasattr(value, "width"):  # a libstb Image
        h.update(f"{value.width}x{value.height}x{value.channels}".encode())
        h.update(value.tobytes())
    else:
        # An Image inside the repr says "channels=3" on a build from before PixelFormat and
        # "format=rgb" after it: write both the same way so the two stay comparable.
        text = re.sub(r"format=(gray_alpha|gray|rgba|rgb)\b", lambda m: "channels=" + str({"gray": 1, "gray_alpha": 2, "rgb": 3, "rgba": 4}[m.group(1)]), repr(value))
        h.update(text.encode())
    return h.hexdigest()


def _pattern(w: int, h: int, c: int, seed: int):
    """Deterministic test pixels with smooth areas, hard edges and noise (no numpy needed)."""
    out = bytearray(w * h * c)
    state = seed & 0xFFFFFFFF
    i = 0
    for y in range(h):
        for x in range(w):
            for k in range(c):
                state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
                noise = (state >> 24) & 0x1F
                smooth = (x * 255 // max(w - 1, 1) + k * 60) & 0xFF
                edge = 200 if ((x // 7 + y // 5) & 1) else 30
                out[i] = (smooth if (x + y) % 3 else edge) ^ noise if k < 3 or c < 4 else (x * 5 + y * 3) & 0xFF
                i += 1
    return bytes(out)


def _image(stb, w, h, c, seed):
    import array

    return stb.Image(_Raw(array.array("B", _pattern(w, h, c, seed)), w, h, c))


class _Raw:
    """__array_interface__ over a bytes-like object, so no numpy is needed."""

    def __init__(self, buf, w, h, c):
        self._buf = buf
        shape = (h, w) if c == 1 else (h, w, c)
        self.__array_interface__ = {"shape": shape, "typestr": "|u1", "data": (buf.buffer_info()[0], False),
                                    "version": 3}


def _open(stb, data, channels, **options):
    """Image.open with a channel count (0 keeps the file's), for a build with or without PixelFormat."""
    if hasattr(stb, "PixelFormat"):
        return stb.Image.open(data, format=None if channels == 0 else stb.PixelFormat(channels), **options)
    return stb.Image.open(data, channels=channels, **options)


def _blend(base, overlay, x, y):
    """Image.blend, or Image.composite in a build from before the rename."""
    return base.blend(overlay, x, y) if hasattr(base, "blend") else base.composite(overlay, x, y)


def run(stb) -> dict:
    r: dict = {}

    def put(name, fn):
        try:
            r[name] = _digest(fn())
        except Exception as e:  # an error is also behaviour: its type and message are compared
            r[name] = f"raises {type(e).__name__}: {e}"

    imgs = {c: _image(stb, 97, 61, c, 7 + c) for c in (1, 2, 3, 4)}

    # ---- encoders and the decoder, every channel count
    for c, im in imgs.items():
        for level in (0, 1, 4, 8, 9, 10):
            put(f"png/c{c}/level{level}", lambda im=im, level=level: im.to_png(level))
        for q in (1, 50, 90, 100):
            put(f"jpg/c{c}/q{q}", lambda im=im, q=q: im.to_jpg(q))
        put(f"bmp/c{c}", im.to_bmp)
        for rle in (False, True):
            put(f"tga/c{c}/rle{rle}", lambda im=im, rle=rle: im.to_tga(rle))
        for fmt in ("png", "bmp", "tga"):
            data = getattr(im, f"to_{fmt}")()
            put(f"roundtrip/{fmt}/c{c}", lambda data=data: stb.Image.open(data))
            put(f"info/{fmt}/c{c}", lambda data=data: _info(stb.iminfo(data)))
        jdata = im.to_jpg(95)
        put(f"decode/jpg/c{c}", lambda jdata=jdata: stb.Image.open(jdata))
        for want in (1, 3, 4):
            put(f"decode/png/c{c}->{want}", lambda im=im, want=want: _open(stb, im.to_png(), want))
        put(f"decode/flip/c{c}", lambda im=im: stb.Image.open(im.to_png(), flip=True))

    big = _image(stb, 640, 480, 3, 99)
    put("png/big", big.to_png)
    put("jpg/big", lambda: big.to_jpg(85))

    # ---- resize
    rgba, rgb = imgs[4], imgs[3]
    for f in stb.Resizer.Filter.__members__:
        for edge in stb.Resizer.Edge.__members__:
            for srgb in (True, False):
                def go(f=f, edge=edge, srgb=srgb):
                    rs = stb.Resizer(stb.Resizer.Filter.__members__[f], stb.Resizer.Edge.__members__[edge], srgb)
                    return [rgba.resize(40, 23, rs), rgb.resize(200, 130, rs)]

                put(f"resize/{f}/{edge}/srgb{srgb}", lambda go=go: b"".join(i.tobytes() for i in go()))
    for name in ("cubic", "linear", "nearest", "box", "lanczos", "bogus"):
        put(f"resize/name/{name}", lambda name=name: rgb.resize(50, 40, stb.Resizer(name)))
    put("resize/default", lambda: rgb.resize(33, 21))
    put("thumbnail/default", lambda: rgb.thumbnail(40, 40))
    put("thumbnail/noop", lambda: rgb.thumbnail(500, 500))
    put("thumbnail/cubic", lambda: rgb.thumbnail(30, 50, stb.Resizer("cubic")))
    put("thumbnail/bad", lambda: rgb.thumbnail(0, 4))

    # ---- geometry
    for c, im in imgs.items():
        put(f"crop/c{c}", lambda im=im: im.crop(5, 3, 40, 30))
        put(f"crop/out/c{c}", lambda im=im: im.crop(90, 50, 40, 30))
        put(f"flip_h/c{c}", im.flip_horizontal)
        put(f"flip_v/c{c}", im.flip_vertical)
        put(f"transpose/c{c}", im.transpose)
        for t in (-1, 1, 2, 3, 4):
            put(f"rotate90/c{c}/{t}", lambda im=im, t=t: im.rotate90(t))
        put(f"pad/c{c}", lambda im=im: im.pad(3, 4, 5, 6, fill=77))
        put(f"pad/c{c}/tuple", lambda im=im: im.pad(2, 2, 2, 2, fill=tuple(range(10, 10 + c))))
        for o in range(0, 10):
            put(f"orient/c{c}/{o}", lambda im=im, o=o: im.orient(o))
        put(f"split_merge/c{c}", lambda im=im: stb.Image.merge(im.split()))
        put(f"copy/c{c}", im.copy)

    # ---- channels, alpha, SIMD backends
    backends = list(stb.simd_backends())
    for b in backends:
        with stb.simd(b):
            for src in (1, 2, 3, 4):
                for dst in (1, 2, 3, 4):
                    put(f"convert/{b}/{src}->{dst}", lambda s=src, d=dst: imgs[s].convert(_fmt(stb, d)))
            put(f"composite/{b}/rgba", lambda: _blend(imgs[3], imgs[4], 7, 3))
            put(f"composite/{b}/la", lambda: _blend(imgs[1], imgs[2], -5, 9))
            put(f"composite/{b}/rgba-on-rgba", lambda: _blend(imgs[4], _image(stb, 30, 30, 4, 5), 20, 20))
            put(f"flatten/{b}/rgba", lambda: imgs[4].flatten((10, 120, 200)))
            put(f"flatten/{b}/la", lambda: imgs[2].flatten(40))
    put("simd/name", stb.simd_name)
    put("simd/backends", lambda: backends)

    # ---- zlib
    blobs = {"empty": b"", "one": b"a", "text": b"hello world, " * 500,
             "noise": _pattern(300, 200, 1, 3), "zeros": bytes(100000)}
    for name, blob in blobs.items():
        for level in (-1, 0, 1, 3, 6, 9):
            def zc(blob=blob, level=level):
                z = stb.zlib_compress(blob, level)
                assert stb.zlib_decompress(z) == blob
                return z

            put(f"zlib/{name}/level{level}", zc)
    put("zlib/bad-level", lambda: stb.zlib_compress(b"x", 42))
    put("zlib/corrupt", lambda: stb.zlib_decompress(b"\x78\x9c\x01\x02\x03"))

    # ---- fonts, shaping, drawing
    latin = stb.Font.open(DATA / "libstb-test.ttf")
    fa = stb.Font.open(DATA / "libstb-fa-test.ttf")
    gsub = stb.Font.open(DATA / "libstb-fa-gsub-test.ttf")
    persian = "\u0633\u0644\u0627\u0645 \u062f\u0646\u06cc\u0627 123 ABC \u0644\u0627 \u0643\u062a\u0627\u0628"
    for fname, font in (("latin", latin), ("fa", fa), ("gsub", gsub)):
        for text in ("A", "AAA\nA", "", persian, "\u0627\u0644\u0644\u0647", "\u05e9\u05dc\u05d5\u05dd"):
            put(f"font/{fname}/shape/{text!r}", lambda f=font, t=text: f.shape_glyphs(t))
            put(f"font/{fname}/measure/{text!r}", lambda f=font, t=text: f.measure(t, 24))
            put(f"font/{fname}/render/{text!r}", lambda f=font, t=text: tuple(f.render(t, 24)))
        put(f"font/{fname}/metrics", lambda f=font: f.metrics(30))
        put(f"font/{fname}/glyph", lambda f=font: f.render_glyph("A", 30))
        put(f"font/{fname}/advance", lambda f=font: (f.advance("A", 17), f.kerning("A", "A", 17), f.has_glyph("A")))
        put(f"font/{fname}/atlas", lambda f=font: (lambda a: (a.image, sorted(a.glyphs.items())))(
            f.make_atlas("ABCabc", 20, 128, 128)))
    for fam in ("sans", "serif", "mono"):
        put(f"estimate/{fam}", lambda fam=fam: (stb.estimate_text("Hello, World\nxx", 16, fam),
                                                 stb.estimate_metrics(16, fam)))
    base = _image(stb, 220, 140, 3, 21)
    put("draw_text/latin", lambda: base.draw_text("AAA\nA", latin, 28, 10, 10, color=(255, 0, 0), align="center"))
    put("draw_text/rtl", lambda: base.draw_text(persian, gsub, 24, 5, 40, color="#ff8800", align="right"))
    put("draw_text/alpha", lambda: imgs[4].draw_text("A", latin, 40, 3, 3, color=(0, 0, 255, 128)))

    def canvas():
        c = stb.Canvas(base).set_font(latin, 26).set_color("white")
        c.add_rect(10, 10, 90, 50).fill("#224488").radius(12).stroke(3, "white")
        c.add_ellipse(110, 10, 90, 50).fill((255, 200, 0, 160)).stroke(2, (0, 0, 0, 200))
        c.add_line(5, 120, 215, 90).stroke(3, "red")
        c.add_rect(60, 75, 70, 36).no_fill().stroke(2, "black")
        c.add_text("AA\nA", 120, 70).align("center").color("#ffcc00").bg_color("#222").padding(6)
        c.add_text(persian, 215, 110).font(gsub, 18).align("right").color((255, 255, 255, 200))
        return c.finish()

    put("canvas/scene", canvas)

    # ---- real fonts, when the machine has any: TrueType and CFF outlines, collections, GPOS kerning
    import glob
    import os

    real = sorted({f for pat in ("/usr/share/fonts/**/*.ttf", "/usr/share/fonts/**/*.otf", "/usr/share/fonts/**/*.ttc")
                   for f in glob.glob(pat, recursive=True) if os.path.isfile(f) and not os.path.islink(f)})
    sample = [chr(c) for c in list(range(33, 127)) + [0xE9, 0xFC, 0x20AC, 0x0627, 0x0645, 0x0651, 0x4E2D, 0x3042]]
    for path in real:
        def real_font(path=path):
            f = stb.Font.open(path)
            acc = []
            for px in (8.5, 24, 57.3):
                acc.append(f.metrics(px))
                for ch in sample:
                    g = f.render_glyph(ch, px)
                    acc.append((ch, g.x_offset, g.y_offset, g.advance, g.bitmap.tobytes() if g.bitmap is not None else None))
                acc.append([f.kerning(a, b, px) for a in "AVTo" for b in "AVyo."])
            atlas = f.make_atlas("".join(sample[:60]), 20, 256, 256, 2)
            acc.append((atlas.image.tobytes(), sorted(atlas.glyphs.items())))
            return acc

        put("realfont/" + path, real_font)

    # ---- errors are behaviour too
    put("err/empty", lambda: stb.Image.open(b""))
    put("err/garbage", lambda: stb.Image.open(b"not an image at all"))
    put("err/crop", lambda: imgs[3].crop(0, 0, 0, 5))
    put("err/convert", lambda: imgs[3].convert(_fmt(stb, 7)))  # the message differs between old and new builds
    put("err/jpg-quality", lambda: imgs[3].to_jpg(0))
    put("err/png-level", lambda: imgs[3].to_png(99))
    return r


def diff(a_path: str, b_path: str) -> int:
    a = json.loads(pathlib.Path(a_path).read_text())
    b = json.loads(pathlib.Path(b_path).read_text())
    bad = [k for k in sorted(a.keys() | b.keys()) if a.get(k) != b.get(k)]
    for k in bad:
        print(f"DIFF {k}\n   old: {a.get(k)}\n   new: {b.get(k)}")
    print(f"{len(a)} cases, {len(bad)} differ")
    return 1 if bad else 0


def main(argv: list[str]) -> int:
    if len(argv) == 4 and argv[1] == "--diff":
        return diff(argv[2], argv[3])
    if len(argv) != 2:
        print(__doc__)
        return 2
    import libstb as stb

    result = run(stb)
    pathlib.Path(argv[1]).write_text(json.dumps(result, indent=1, sort_keys=True))
    print(f"{len(result)} cases written to {argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
