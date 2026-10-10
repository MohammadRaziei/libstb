"""libstb <-> Pillow: both read each other directly (NumPy's __array_interface__)."""
from pathlib import Path

import numpy as np
from PIL import Image as PILImage

import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)

y, x = np.mgrid[0:60, 0:90]
img = libstb.Image(np.dstack([x * 255 // 89, y * 255 // 59, np.full_like(x, 120)]).astype(np.uint8))

# libstb -> Pillow: the mode follows the channel count (L, LA, RGB, RGBA). It is a copy.
pil = PILImage.fromarray(img)
print("fromarray     :", pil.mode, pil.size)
print("gray and alpha:", PILImage.fromarray(img.convert(1)).mode, PILImage.fromarray(img.convert(4)).mode)

# Pillow -> libstb.
back = libstb.Image(pil)
print("Image(pil)    :", back, "| same pixels:", np.array_equal(back.numpy(), img.numpy()))

# Use each library for what it is good at: libstb for the fast decode / resize / encode,
# Pillow for the rest of its toolbox (filters, drawing, text, many more formats).
from PIL import ImageDraw, ImageFilter

small = img.thumbnail(64, 64)
canvas = PILImage.fromarray(small).filter(ImageFilter.GaussianBlur(1.2))
ImageDraw.Draw(canvas).rectangle((4, 4, 20, 14), outline=(255, 255, 255))
libstb.imwrite(OUT / "pillow_round_trip.png", libstb.Image(canvas))
print("wrote", OUT / "pillow_round_trip.png", "(libstb -> Pillow filter and drawing -> libstb)")

# Only 8-bit images convert. A palette image holds indices, not colours: convert it first.
palette = PILImage.new("P", (4, 3))
print("P image       :", libstb.Image(palette.convert("RGB")).channels, "channels after convert('RGB')")
try:
    libstb.Image(PILImage.new("F", (4, 3)))
except TypeError as e:
    print("float image   : TypeError:", e)
