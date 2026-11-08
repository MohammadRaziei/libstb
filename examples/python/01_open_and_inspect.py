"""Open an image, read its header, and look at its pixels."""
from pathlib import Path

import numpy as np
import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

src = DATA / "images" / "astronaut.png"

# Header only: no pixels are decoded. The pictures of data/images come as gray, RGB, RGBA and a real JPEG.
for name in ("camera.png", "astronaut.png", "badge.png", "rocket.jpg"):
    info = stb.iminfo(DATA / "images" / name)
    print(f"header {name:14s}:", info.width, "x", info.height, "|", info.channels, "channel(s)")

# Just the pixels, as a numpy array.
pixels = stb.imread(src)
print("imread :", pixels.shape, pixels.dtype)

# Or an Image object, which also resizes and encodes.
img = stb.Image.open(src)
print("image  :", img.width, img.height, img.channels)

# A numpy view of the pixels (H, W, C), no copy.
pixels = img.numpy()
print("numpy  :", pixels.shape, pixels.dtype)
print("top-left     :", pixels[0, 0])
print("bottom-right :", pixels[-1, -1])

# Ask for a different channel count, or flip vertically while decoding.
rgba = stb.Image.open(src, format=stb.PixelFormat.RGBA)
print("rgba   :", rgba.channels, "channels, alpha =", rgba.numpy()[0, 0, 3])

gray = stb.Image.open(src, format=stb.PixelFormat.GRAY)
print("gray   :", gray.channels, "channel, top-left =", gray.numpy()[0, 0])

flipped = stb.Image.open(src, flip=True)
print("flipped: top-left is now", flipped.numpy()[0, 0])

# Open from bytes works too (an upload, a socket, a database column), and the format is found from the bytes.
again = stb.Image.open((DATA / "images" / "rocket.jpg").read_bytes())
print("bytes  :", again.width, again.height, again.channels, "(a JPEG)")

# Write a copy: the format follows the extension.
img.write(OUT / "astronaut_copy.jpg")
print("wrote", OUT / "astronaut_copy.jpg")
