"""
corpus.py: deterministic synthetic image corpus for the libstb
benchmark suite.

Methodology note (documented here, and in benchmarks/README.md): this
corpus is **synthetic but shaped on purpose**. Image codecs and
resizers do not have one speed: PNG decode time depends on how
compressible the pixels are, JPEG on how much detail survives the DCT,
resize mostly on pixel count. A corpus that cannot control the *shape*
of the data cannot tell those stories, so four genres are generated
(seeded, so every run and every machine sees identical pixels), at
several sizes:

  gradient   smooth sinusoidal colour ramps. Best case for PNG (filters
             predict it almost perfectly) and for JPEG.
  photo      several octaves of smooth value noise, a few hard-edged
             rectangles and mild sensor noise: the closest thing to a
             natural photograph a generator can be, without shipping
             one. (Real photographs are benchmarked too: see
             real_corpus_manifest.py.)
  noise      uniform random bytes. Worst case for every codec: nothing
             to predict, nothing to discard.
  graphics   flat colour blocks and thin lines with a real alpha
             channel (RGBA, 4 channels): screenshot/UI-like content,
             and the only genre that exercises the 4-channel paths.

Every image is written in four formats (png, jpg, bmp, tga) by
**Pillow**, i.e. by a neutral third-party encoder, never by stb, so the
decode benchmarks cannot be biased towards files that one specific
encoder produced. JPEG has no alpha, and neither does BMP in any
portable sense (a 32-bit BI_RGB bitmap's fourth byte is "unused" to
some readers and alpha to others, which verify.py caught: the
libraries disagreed about the channel count), so the graphics genre's
jpg and bmp files are RGB. Its png and tga files keep the alpha channel.

Nothing here is tuned to make libstb look good; the generators were
written once, before any numbers were collected.
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

GENRES = ("gradient", "photo", "noise", "graphics")
SIDES = (64, 256, 1024, 2048)
FORMATS = ("png", "jpg", "bmp", "tga")

# What the corpus files are written with. Fixed here, not left to
# library defaults, so the files are the same on every machine.
PNG_LEVEL = 6
JPEG_QUALITY = 90


# ------------------------------------------------------------ generators --

def gen_gradient(h, w, seed=1):
    rng = np.random.default_rng(seed)
    y = np.linspace(0.0, 1.0, h, dtype=np.float32)[:, None]
    x = np.linspace(0.0, 1.0, w, dtype=np.float32)[None, :]
    out = np.empty((h, w, 3), np.uint8)
    for c in range(3):
        phase = float(rng.uniform(0.0, 6.28))
        v = 0.5 + 0.35 * np.sin(2.0 * np.pi * (x * (1 + c) + y * (2.0 - 0.5 * c)) + phase)
        out[..., c] = np.clip(v * 255.0, 0, 255).astype(np.uint8)
    return out


def _upsample_rows(grid, h, w, r0, r1):
    """Bilinear upsample of a small (g, g, 3) float32 grid to h x w,
    computing only output rows [r0, r1) so big images are built in
    bounded-memory blocks."""
    gh, gw = grid.shape[:2]
    ys = np.linspace(0.0, gh - 1, h, dtype=np.float32)[r0:r1]
    xs = np.linspace(0.0, gw - 1, w, dtype=np.float32)
    y0 = np.floor(ys).astype(np.int32)
    y1 = np.minimum(y0 + 1, gh - 1)
    x0 = np.floor(xs).astype(np.int32)
    x1 = np.minimum(x0 + 1, gw - 1)
    fy = (ys - y0)[:, None, None]
    fx = (xs - x0)[None, :, None]
    top = grid[y0][:, x0] * (1.0 - fx) + grid[y0][:, x1] * fx
    bot = grid[y1][:, x0] * (1.0 - fx) + grid[y1][:, x1] * fx
    return top * (1.0 - fy) + bot * fy


def gen_photo(h, w, seed=2):
    rng = np.random.default_rng(seed)
    octaves = [(4, 0.50), (8, 0.25), (16, 0.13), (32, 0.07), (64, 0.04)]
    grids = [(rng.random((n + 1, n + 1, 3), dtype=np.float32), a) for n, a in octaves]
    total = sum(a for _, a in grids)
    out = np.empty((h, w, 3), np.uint8)
    block = 256
    for r0 in range(0, h, block):
        r1 = min(h, r0 + block)
        acc = np.zeros((r1 - r0, w, 3), np.float32)
        for grid, amp in grids:
            acc += amp * _upsample_rows(grid, h, w, r0, r1)
        acc = 30.0 + 195.0 * (acc / total)
        acc += rng.normal(0.0, 3.0, acc.shape).astype(np.float32)
        out[r0:r1] = np.clip(acc, 0, 255).astype(np.uint8)
    # a few hard edges, like objects in a scene
    for _ in range(max(4, h // 64)):
        rh = int(rng.integers(h // 16 + 1, h // 4 + 2))
        rw = int(rng.integers(w // 16 + 1, w // 4 + 2))
        y = int(rng.integers(0, max(1, h - rh)))
        x = int(rng.integers(0, max(1, w - rw)))
        out[y:y + rh, x:x + rw] = rng.integers(0, 256, 3, dtype=np.uint8)
    return out


def gen_noise(h, w, seed=3):
    rng = np.random.default_rng(seed)
    return rng.integers(0, 256, (h, w, 3), dtype=np.uint8)


def gen_graphics(h, w, seed=4):
    rng = np.random.default_rng(seed)
    out = np.empty((h, w, 4), np.uint8)
    out[...] = (245, 245, 250, 255)
    n = max(8, (h * w) // (96 * 96))
    for _ in range(n):
        rh = int(rng.integers(h // 32 + 1, h // 4 + 2))
        rw = int(rng.integers(w // 32 + 1, w // 4 + 2))
        y = int(rng.integers(0, max(1, h - rh)))
        x = int(rng.integers(0, max(1, w - rw)))
        color = rng.integers(0, 256, 4, dtype=np.uint8)
        color[3] = rng.choice([255, 255, 200, 128])
        out[y:y + rh, x:x + rw] = color
    step = max(8, h // 16)
    out[::step, :, :3] = (60, 60, 60)
    out[:, ::step, :3] = (60, 60, 60)
    return out


GENERATORS = {
    "gradient": gen_gradient,
    "photo": gen_photo,
    "noise": gen_noise,
    "graphics": gen_graphics,
}


# --------------------------------------------------------------- writing --

def write_variants(arr, base, formats=FORMATS):
    """Write `arr` (HxWxC uint8, C in {3, 4}) as every requested format
    next to `base` (no extension). Returns {format: (path, file_channels)}."""
    im = Image.fromarray(arr)
    out = {}
    for fmt in formats:
        path = f"{base}.{fmt}"
        if fmt == "png":
            im.save(path, compress_level=PNG_LEVEL)
            ch = arr.shape[2]
        elif fmt == "jpg":
            im.convert("RGB").save(path, quality=JPEG_QUALITY)
            ch = 3
        elif fmt == "bmp":
            im.convert("RGB").save(path)
            ch = 3
        elif fmt == "tga":
            im.save(path)
            ch = arr.shape[2]
        else:
            raise ValueError(f"unknown format {fmt!r}")
        out[fmt] = (path, ch)
    return out


def manifest_entries(genre, size_label, arr, base, formats=FORMATS):
    """Write every format of `arr` and return one manifest entry per file.

    Only the png entry is `canonical`: it is the one the encode/resize
    operations run on (they start from raw pixels, not from a file, so
    running them four times per image, once per file format, would just
    repeat the same measurement). `raw` is the .npy those operations
    load their pixels from, so no decoder is involved in setting them up.
    """
    h, w = arr.shape[:2]
    raw_path = f"{base}.npy"
    np.save(raw_path, arr)
    entries = []
    for fmt, (path, ch) in write_variants(arr, base, formats).items():
        entries.append({
            "genre": genre,
            "size": size_label,
            "format": fmt,
            "path": path,
            "bytes": os.path.getsize(path),
            "width": w,
            "height": h,
            "channels": ch,
            "pixels": w * h,
            "canonical": fmt == "png",
            "raw": raw_path if fmt == "png" else None,
            "raw_channels": arr.shape[2],
        })
    return entries


def generate(out_dir, sides=SIDES, genres=GENRES):
    os.makedirs(out_dir, exist_ok=True)
    manifest = []
    for genre in genres:
        for side in sides:
            arr = GENERATORS[genre](side, side)
            base = os.path.join(out_dir, f"{genre}_{side}")
            manifest.extend(manifest_entries(genre, f"{side}x{side}", arr, base))
            del arr
    return manifest


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out_dir", help="directory to write the image files + manifest.json into")
    p.add_argument("--sides", default=",".join(str(s) for s in SIDES),
                   help="comma-separated image side lengths in pixels (all images are square)")
    args = p.parse_args()

    sides = [int(s) for s in args.sides.split(",") if s.strip()]
    manifest = generate(args.out_dir, sides=sides)

    manifest_path = os.path.join(args.out_dir, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    total = sum(e["bytes"] for e in manifest)
    print(f"corpus: {len(manifest)} files ({total / 1e6:.1f} MB) -> {manifest_path}", file=sys.stderr)
