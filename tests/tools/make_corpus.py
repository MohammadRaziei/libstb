"""Development check, NOT a test: builds a directory of image files that exercise every decoder
branch (progressive and subsampled JPEGs, palette / 16-bit / interlaced PNG, RLE TGA, BMP depths,
GIF, PSD, PNM, HDR, ...) plus whatever real images the machine has. Pair it with decode_snapshot.py:

    python tests/tools/make_corpus.py /tmp/corpus
"""
import glob
import os
import random
import shutil
import struct
import sys
import zlib

from PIL import Image

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rnd = random.Random(5)


def noise_image(w, h, mode):
    bands = len(Image.new(mode, (1, 1)).getbands())
    data = bytes(rnd.randrange(256) if rnd.random() < 0.3 else (x * 7 + y * 3) & 255
                 for y in range(h) for x in range(w) for _ in range(bands))
    return Image.frombytes(mode, (w, h), data)


def smooth_image(w, h, mode="RGB"):
    im = Image.new(mode, (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            v = (x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), (x + y) * 255 // max(w + h - 2, 1))
            px[x, y] = v if mode == "RGB" else (v + (200,) if mode == "RGBA" else v[0])
    return im


n = 0
def save(im, name, **kw):
    global n
    try:
        im.save(os.path.join(out, name), **kw)
        n += 1
    except Exception as e:
        print("skip", name, e)


sizes = [(1, 1), (7, 5), (16, 16), (33, 17), (64, 48), (100, 75)]
for w, h in sizes:
    for mode in ("L", "LA", "RGB", "RGBA", "P"):
        im = noise_image(w, h, "L" if mode == "P" else mode)
        if mode == "P":
            im = smooth_image(w, h).quantize(16)
        save(im, f"p_{mode}_{w}x{h}.png")
        save(im, f"p_{mode}_{w}x{h}_opt.png", optimize=True, compress_level=9)
        if mode in ("L", "RGB", "RGBA", "P"):
            save(im, f"p_{mode}_{w}x{h}.bmp")
            save(im.convert("RGB") if mode in ("LA", "P") else im, f"p_{mode}_{w}x{h}.tga")
            save(im.convert("RGB") if mode in ("LA", "P") else im, f"p_{mode}_{w}x{h}_rle.tga", compression="tga_rle")
        if mode in ("L", "RGB"):
            save(im, f"p_{mode}_{w}x{h}.ppm" if mode == "RGB" else f"p_{mode}_{w}x{h}.pgm")
    im = smooth_image(w, h)
    for q in (10, 50, 75, 95):
        for sub in (0, 1, 2):
            save(im, f"j_{w}x{h}_q{q}_s{sub}.jpg", quality=q, subsampling=sub)
        save(im, f"j_{w}x{h}_q{q}_prog.jpg", quality=q, progressive=True)
        save(im, f"j_{w}x{h}_q{q}_opt.jpg", quality=q, optimize=True)
        save(im.convert("L"), f"j_{w}x{h}_q{q}_gray.jpg", quality=q)
        save(im.convert("L"), f"j_{w}x{h}_q{q}_gray_prog.jpg", quality=q, progressive=True)
    save(im.convert("CMYK"), f"j_{w}x{h}_cmyk.jpg")
    save(im, f"j_{w}x{h}_restart.jpg", quality=80, restart_marker_blocks=3) if False else None
    save(im.quantize(32), f"g_{w}x{h}.gif")
    frames = [smooth_image(w, h).quantize(16), noise_image(w, h, "RGB").quantize(16)]
    save(frames[0], f"g_{w}x{h}_anim.gif", save_all=True, append_images=frames[1:], duration=50, loop=0)
    save(frames[0], f"g_{w}x{h}_transp.gif", transparency=0)
    save(frames[0], f"g_{w}x{h}_interlace.gif", interlace=True)

# 16 bit PNG, interlaced PNG and a tRNS palette PNG written by hand (Pillow cannot write all of them)
def png_chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

def write_png(path, w, h, depth, ctype, rows, interlace=0, plte=None, trns=None):
    raw = b"".join(b"\x00" + r for r in rows)
    data = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if plte: data += png_chunk(b"PLTE", plte)
    if trns: data += png_chunk(b"tRNS", trns)
    data += png_chunk(b"IDAT", zlib.compress(raw)) + png_chunk(b"IEND", b"")
    open(path, "wb").write(data); global n; n += 1

for ctype, bands in ((0, 1), (2, 3), (4, 2), (6, 4)):
    for depth in (8, 16):
        w, h = 13, 9
        rows = [bytes(rnd.randrange(256) for _ in range(w * bands * depth // 8)) for _ in range(h)]
        write_png(f"{out}/h_{ctype}_{depth}.png", w, h, depth, ctype, rows)
for depth in (1, 2, 4, 8):
    w, h = 11, 7
    bits = w * depth
    rows = [bytes(rnd.randrange(256) for _ in range((bits + 7) // 8)) for _ in range(h)]
    write_png(f"{out}/h_pal_{depth}.png", w, h, depth, 3, rows, plte=bytes(rnd.randrange(256) for _ in range(3 * (1 << depth))),
              trns=bytes(rnd.randrange(256) for _ in range(min(5, 1 << depth))))
    write_png(f"{out}/h_gray_{depth}.png", w, h, depth, 0, rows)

# Adam7 interlaced: pass sizes
def adam7(w, h, bytes_per_pixel_bits, rnd):
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    out_rows = []
    for x0, y0, dx, dy in passes:
        pw = (w - x0 + dx - 1) // dx
        ph = (h - y0 + dy - 1) // dy
        if pw <= 0 or ph <= 0:
            continue
        for _ in range(ph):
            out_rows.append(bytes(rnd.randrange(256) for _ in range((pw * bytes_per_pixel_bits + 7) // 8)))
    return out_rows
for ctype, bands in ((0, 1), (2, 3), (6, 4)):
    for w, h in ((9, 7), (16, 16), (3, 3)):
        rows = adam7(w, h, 8 * bands, rnd)
        write_png(f"{out}/i_{ctype}_{w}x{h}.png", w, h, 8, ctype, rows, interlace=1)

# BMP variants by hand: 1/4/8 bit palette, 16 bit, bitfields 32, top-down, RLE8
def write_bmp(path, w, h, bpp, palette, pixel_data, top_down=False, comp=0, masks=None):
    hdr_size = 40 if not masks else 56
    off = 14 + hdr_size + len(palette) + 0
    size = off + len(pixel_data)
    d = b"BM" + struct.pack("<IHHI", size, 0, 0, off)
    d += struct.pack("<IiiHHIIiiII", hdr_size, w, -h if top_down else h, 1, bpp, comp, len(pixel_data), 2835, 2835,
                     len(palette) // 4, 0)
    if masks: d += struct.pack("<IIII", *masks)
    d += palette + pixel_data
    open(path, "wb").write(d); global n; n += 1
for bpp in (1, 4, 8):
    w, h = 21, 6
    stride = ((w * bpp + 31) // 32) * 4
    pal = b"".join(bytes([rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), 0]) for _ in range(1 << bpp))
    px = bytes(rnd.randrange(256) for _ in range(stride * h))
    write_bmp(f"{out}/b_pal{bpp}.bmp", w, h, bpp, pal, px)
    write_bmp(f"{out}/b_pal{bpp}_td.bmp", w, h, bpp, pal, px, top_down=True)
stride = ((17 * 16 + 31) // 32) * 4
write_bmp(f"{out}/b_16.bmp", 17, 5, 16, b"", bytes(rnd.randrange(256) for _ in range(stride * 5)))
write_bmp(f"{out}/b_32bf.bmp", 9, 4, 32, b"", bytes(rnd.randrange(256) for _ in range(9 * 4 * 4)), comp=3, masks=(0xff0000, 0xff00, 0xff, 0xff000000))
rle = bytes([5, 7, 3, 9, 0, 3, 1, 2, 3, 0, 1, 0, 0, 4, 2, 1, 0, 0, 0, 1])  # a little RLE8 stream
write_bmp(f"{out}/b_rle8.bmp", 8, 2, 8, b"".join(bytes([i, i, i, 0]) for i in range(256)), rle, comp=1)

# PNM family, ASCII and binary, 16 bit
for magic, bands, maxv in ((b"P2", 1, 255), (b"P3", 3, 255), (b"P5", 1, 255), (b"P6", 3, 255), (b"P5", 1, 65535), (b"P6", 3, 65535)):
    w, h = 7, 4
    hdr = magic + b"\n%d %d\n%d\n" % (w, h, maxv)
    if magic in (b"P2", b"P3"):
        body = b" ".join(b"%d" % rnd.randrange(maxv + 1) for _ in range(w * h * bands)) + b"\n"
    else:
        body = b"".join(struct.pack(">H" if maxv > 255 else "B", rnd.randrange(maxv + 1)) for _ in range(w * h * bands))
    open(f"{out}/n_{magic.decode()}_{maxv}.pnm", "wb").write(hdr + body); n += 1

# Radiance HDR (flat and RLE-less rgbe), PSD (raw and RLE), TGA 16 bit / palette
def write_hdr(path, w, h, rle):
    d = b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (h, w)
    for y in range(h):
        row = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), 120 + rnd.randrange(20)) for _ in range(w)]
        if rle and 8 <= w < 32768:
            d += bytes([2, 2, w >> 8, w & 255])
            for c in range(4):
                d += bytes(len(row) and [])  # placeholder
                i = 0
                chan = [p[c] for p in row]
                while i < w:
                    run = min(w - i, 127)
                    d += bytes([run]) + bytes(chan[i:i + run]); i += run
        else:
            d += b"".join(bytes(p) for p in row)
    open(path, "wb").write(d); global n; n += 1
write_hdr(f"{out}/hdr_flat.hdr", 6, 4, False)
write_hdr(f"{out}/hdr_rle.hdr", 24, 5, True)

def write_psd(path, w, h, channels, rle):
    d = b"8BPS" + struct.pack(">H", 1) + b"\0" * 6 + struct.pack(">HIIHH", channels, h, w, 8, 3 if channels >= 3 else 1)
    d += struct.pack(">I", 0) * 3
    if rle:
        d += struct.pack(">H", 1)
        rows = []
        for _ in range(h * channels):
            rows.append(bytes([w - 1]) + bytes(rnd.randrange(256) for _ in range(w)))
        d += b"".join(struct.pack(">H", len(r)) for r in rows) + b"".join(rows)
    else:
        d += struct.pack(">H", 0) + bytes(rnd.randrange(256) for _ in range(w * h * channels))
    open(path, "wb").write(d); global n; n += 1
for ch in (1, 3, 4):
    write_psd(f"{out}/psd_{ch}_raw.psd", 10, 6, ch, False)
    write_psd(f"{out}/psd_{ch}_rle.psd", 10, 6, ch, True)

# the real images of the machine (at most 250)
real = []
for pat in ("png", "jpg", "jpeg", "gif"):
    real += [f for f in glob.glob(f"/usr/**/*.{pat}", recursive=True) + glob.glob(f"/opt/**/*.{pat}", recursive=True) if os.path.isfile(f)]
rnd.shuffle(real)
for i, f in enumerate(real[:250]):
    shutil.copy(f, os.path.join(out, f"real_{i:03d}_" + os.path.basename(f))); n += 1
print(n, "files in", out)
