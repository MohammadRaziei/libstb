"""libstb <-> Pillow. Pillow is only imported when to_pil / from_pil run (libstb[pillow])."""
from pathlib import Path

import numpy as np
from PIL import Image as PILImage

import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)

y, x = np.mgrid[0:60, 0:90]
img = libstb.Image(np.dstack([x * 255 // 89, y * 255 // 59, np.full_like(x, 120)]).astype(np.uint8))

# libstb -> Pillow: the mode follows the channel count (L, LA, RGB, RGBA). It is a copy.
pil = img.to_pil()
print("to_pil        :", pil.mode, pil.size)
print("gray and alpha:", img.convert(1).to_pil().mode, img.convert(4).to_pil().mode)

# Pillow -> libstb.
back = libstb.Image.from_pil(pil)
print("from_pil      :", back, "| same pixels:", np.array_equal(back.numpy(), img.numpy()))

# Both also work through NumPy's __array_interface__, which Pillow speaks: no methods needed.
print("fromarray(img):", PILImage.fromarray(img).mode, "| Image(pil):", libstb.Image(pil).channels, "channels")

# Use each library for what it is good at: libstb for the fast decode / resize / encode,
# Pillow for the rest of its toolbox (filters, drawing, text, many more formats).
from PIL import ImageDraw, ImageFilter

small = img.thumbnail(64, 64)
canvas = small.to_pil().filter(ImageFilter.GaussianBlur(1.2))
ImageDraw.Draw(canvas).rectangle((4, 4, 20, 14), outline=(255, 255, 255))
libstb.imwrite(OUT / "pillow_round_trip.png", libstb.Image.from_pil(canvas))
print("wrote", OUT / "pillow_round_trip.png", "(libstb -> Pillow filter and drawing -> libstb)")

# from_pil converts the modes that hold 8-bit colour another way (P, 1, CMYK, YCbCr, ...) ...
for mode in ("P", "1", "CMYK", "PA"):
    print(f"from_pil {mode:>5s}:", libstb.Image.from_pil(PILImage.new(mode, (4, 3))).channels, "channels")

# ... and refuses the rest, rather than guess how to scale them. Image(pil) is stricter still:
# it takes only L, LA, RGB, RGBA, so a palette image is never read as if its indices were colours.
try:
    libstb.Image.from_pil(PILImage.new("F", (4, 3)))
except TypeError as e:
    print("float image   : TypeError:", e)
