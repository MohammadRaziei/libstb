"""Encode to PNG, JPEG, BMP and TGA, in memory and to disk."""
from pathlib import Path

import numpy as np
import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

img = stb.Image.open(DATA / "images" / "chelsea.png")
print("source:", img.width, "x", img.height, "-", img.width * img.height * img.channels, "bytes of pixels")

# to_* returns the file's bytes, each format with its own settings.
print("to_png(compression=1):", len(img.to_png(compression=1)), "bytes")
print("to_png(compression=9):", len(img.to_png(compression=9)), "bytes")
for quality in (20, 60, 90):
    print(f"to_jpg(quality={quality}):", len(img.to_jpg(quality=quality)), "bytes")
print("to_bmp():", len(img.to_bmp()), "bytes")
print("to_tga(rle=True):", len(img.to_tga(rle=True)), "bytes")
print("to_tga(rle=False):", len(img.to_tga(rle=False)), "bytes")

# write_* encodes and writes the file (only if encoding succeeded).
img.write_png(OUT / "chelsea_best.png", compression=9)
img.write_jpg(OUT / "chelsea_q40.jpg", quality=40)
img.write_bmp(OUT / "chelsea.bmp")
img.write_tga(OUT / "chelsea.tga", rle=True)

# write() picks the format from the extension and uses default settings.
img.write(OUT / "chelsea_default.jpeg")

# An unknown extension is a ValueError.
try:
    img.write(OUT / "chelsea.webp")
except ValueError as e:
    print("ValueError:", e)

# PNG is lossless: decoding it gives back exactly the same pixels.
back = stb.Image.open(img.to_png())
print("png round trip exact:", np.array_equal(back.numpy(), img.numpy()))

# JPEG is not: the difference is what the quality setting trades for size.
for quality in (20, 90):
    jpeg = stb.Image.open(img.to_jpg(quality=quality))
    worst = int(np.abs(jpeg.numpy().astype(int) - img.numpy().astype(int)).max())
    print(f"jpeg quality {quality}: the worst channel is off by {worst}")
