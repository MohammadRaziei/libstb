"""imread / imwrite / iminfo: the functional API, numpy in and numpy out."""
from pathlib import Path

import numpy as np
import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

pixels = stb.imread(DATA / "images" / "chelsea.png")             # uint8 ndarray (H, W, C)
print("source :", pixels.shape, pixels.dtype, "| a gray file:", stb.imread(DATA / "images" / "camera.png").shape)

# The format comes from the extension; options go to the formats that have them.
stb.imwrite(OUT / "chelsea.png", pixels)
stb.imwrite(OUT / "chelsea_q30.jpg", pixels, quality=30)
stb.imwrite(OUT / "chelsea_c9.png", pixels, compression=9)
stb.imwrite(OUT / "chelsea.tga", pixels, rle=False)

# An option the format does not have is an error, never silently ignored.
try:
    stb.imwrite(OUT / "bad.png", pixels, quality=30)
except ValueError as e:
    print("ValueError:", e)

# Header only, then the pixels.
print("iminfo :", stb.iminfo(OUT / "chelsea.png"))
back = stb.imread(OUT / "chelsea.png")
print("imread :", back.shape, back.dtype)
print("exact  :", np.array_equal(back, pixels))

# Same options as Image.open.
print("rgba   :", stb.imread(OUT / "chelsea.png", format=stb.PixelFormat.RGBA).shape)
print("flipped:", np.array_equal(stb.imread(OUT / "chelsea.png", flip=True), pixels[::-1]))

# imresize: an ndarray in, an ndarray out.
print("resize :", stb.imresize(pixels, 160, 120).shape)
