"""Encode to PNG, JPEG, BMP and TGA, in memory and to disk."""
from pathlib import Path

import numpy as np
import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)


def make_image(width=320, height=200):
    y, x = np.mgrid[0:height, 0:width]
    r = (x * 255 // (width - 1)).astype(np.uint8)
    g = (y * 255 // (height - 1)).astype(np.uint8)
    b = ((x // 16 + y // 16) % 2 * 255).astype(np.uint8)
    return libstb.Image(np.dstack([r, g, b]))


img = make_image()

# to_* returns the file's bytes, each format with its own settings.
print("to_png(compression=1):", len(img.to_png(compression=1)), "bytes")
print("to_png(compression=9):", len(img.to_png(compression=9)), "bytes")
for quality in (20, 60, 90):
    print(f"to_jpg(quality={quality}):", len(img.to_jpg(quality=quality)), "bytes")
print("to_bmp():", len(img.to_bmp()), "bytes")
print("to_tga(rle=True):", len(img.to_tga(rle=True)), "bytes")
print("to_tga(rle=False):", len(img.to_tga(rle=False)), "bytes")

# write_* encodes and writes the file (only if encoding succeeded).
img.write_png(OUT / "pattern_best.png", compression=9)
img.write_jpg(OUT / "pattern_q40.jpg", quality=40)
img.write_bmp(OUT / "pattern.bmp")
img.write_tga(OUT / "pattern.tga", rle=True)

# write() picks the format from the extension and uses default settings.
img.write(OUT / "pattern_default.jpeg")

# An unknown extension is a ValueError.
try:
    img.write(OUT / "pattern.webp")
except ValueError as e:
    print("ValueError:", e)

# PNG is lossless: decoding it gives back exactly the same pixels.
back = libstb.Image.open(img.to_png())
print("png round trip exact:", np.array_equal(back.numpy(), img.numpy()))
