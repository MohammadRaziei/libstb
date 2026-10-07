"""Open an image, read its header, and look at its pixels."""
from pathlib import Path

import numpy as np
import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)


def make_gradient(width=320, height=200):
    """A red/green gradient, uint8, shape (height, width, 3)."""
    y, x = np.mgrid[0:height, 0:width]
    r = (x * 255 // (width - 1)).astype(np.uint8)
    g = (y * 255 // (height - 1)).astype(np.uint8)
    b = np.full_like(r, 128)
    return np.dstack([r, g, b])


# Make a file to work with.
src = OUT / "gradient.png"
libstb.imwrite(src, make_gradient())

# Header only: no pixels are decoded.
info = libstb.iminfo(src)
print("header :", info.width, info.height, info.channels)

# Just the pixels, as a numpy array.
pixels = libstb.imread(src)
print("imread :", pixels.shape, pixels.dtype)

# Or an Image object, which also resizes and encodes.
img = libstb.Image.open(src)
print("image  :", img.width, img.height, img.channels)

# A numpy view of the pixels (H, W, C), no copy.
pixels = img.numpy()
print("numpy  :", pixels.shape, pixels.dtype)
print("top-left     :", pixels[0, 0])
print("bottom-right :", pixels[-1, -1])

# Ask for a different channel count, or flip vertically while decoding.
rgba = libstb.Image.open(src, channels=4)
print("rgba   :", rgba.channels, "channels, alpha =", rgba.numpy()[0, 0, 3])

flipped = libstb.Image.open(src, flip=True)
print("flipped: top-left is now", flipped.numpy()[0, 0])

# Open from bytes works too.
again = libstb.Image.open(src.read_bytes())
print("bytes  :", again.width, again.height, again.channels)
