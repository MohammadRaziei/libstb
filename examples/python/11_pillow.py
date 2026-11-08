"""libstb <-> Pillow. Pillow is only imported when to_pil / from_pil run (libstb[pillow])."""
from pathlib import Path

import numpy as np
from PIL import Image as PILImage

import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

img = stb.Image.open(DATA / "images" / "chelsea.png")

# libstb -> Pillow: the mode follows the channel count (L, LA, RGB, RGBA). L and RGBA are not copied:
# the Pillow image looks at the pixels of `img` (read-only; Pillow copies on its first write). LA and RGB
# are copied, and `img.to_pil().copy()` is independent of `img`.
pil = img.to_pil()
print("to_pil        :", pil.mode, pil.size)
print("gray and alpha:", img.convert(stb.PixelFormat.GRAY).to_pil().mode, img.convert(stb.PixelFormat.RGBA).to_pil().mode)

# The sharing, seen from here: a change to the Image shows in the Pillow image for L and RGBA, not for RGB.
rgba = stb.Image.open(DATA / "images" / "badge.png")
shared = rgba.to_pil()
snapshot = shared.copy()                                   # to_pil().copy() is independent of `rgba`
rgba.numpy()[0, 0] = (255, 0, 0, 255)
print("RGBA shares    :", shared.getpixel((0, 0)) == (255, 0, 0, 255), "| the .copy() does not:", snapshot.getpixel((0, 0)) != (255, 0, 0, 255))
scratch = img.copy()
scratch_pil = scratch.to_pil()
scratch.numpy()[0, 0] = (1, 2, 3)
print("RGB is a copy  :", scratch_pil.getpixel((0, 0)) != (1, 2, 3))

# Pillow -> libstb.
back = stb.Image.from_pil(pil)
print("from_pil      :", back, "| same pixels:", np.array_equal(back.numpy(), img.numpy()))

# Both also work through NumPy's __array_interface__, which Pillow speaks: no methods needed.
print("fromarray(img):", PILImage.fromarray(img).mode, "| Image(pil):", stb.Image(pil).channels, "channels")

# Use each library for what it is good at: libstb for the fast decode / resize / encode,
# Pillow for the rest of its toolbox (filters, drawing, text, many more formats).
from PIL import ImageDraw, ImageFilter

small = img.thumbnail(64, 64)
canvas = small.to_pil().filter(ImageFilter.GaussianBlur(1.2))
ImageDraw.Draw(canvas).rectangle((4, 4, 20, 14), outline=(255, 255, 255))
stb.imwrite(OUT / "pillow_round_trip.png", stb.Image.from_pil(canvas))
print("wrote", OUT / "pillow_round_trip.png", "(libstb -> Pillow filter and drawing -> libstb)")

# from_pil converts the modes that hold 8-bit colour another way (P, 1, CMYK, YCbCr, ...) ...
for mode in ("P", "1", "CMYK", "PA"):
    print(f"from_pil {mode:>5s}:", stb.Image.from_pil(PILImage.new(mode, (4, 3))).channels, "channels")

# ... and refuses the rest, rather than guess how to scale them. Image(pil) is stricter still:
# it takes only L, LA, RGB, RGBA, so a palette image is never read as if its indices were colours.
try:
    stb.Image.from_pil(PILImage.new("F", (4, 3)))
except TypeError as e:
    print("float image   : TypeError:", e)
