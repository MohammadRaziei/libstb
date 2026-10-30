"""imread / imwrite / iminfo: the functional API, numpy in and numpy out."""
from pathlib import Path

import numpy as np
import libstb as stb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)

y, x = np.mgrid[0:200, 0:320]
pixels = np.dstack([
    (x * 255 // 319).astype(np.uint8),
    (y * 255 // 199).astype(np.uint8),
    ((x // 16 + y // 16) % 2 * 255).astype(np.uint8),
])

# The format comes from the extension; options go to the formats that have them.
stb.imwrite(OUT / "pattern.png", pixels)
stb.imwrite(OUT / "pattern_q30.jpg", pixels, quality=30)
stb.imwrite(OUT / "pattern_c9.png", pixels, compression=9)
stb.imwrite(OUT / "pattern.tga", pixels, rle=False)

# An option the format does not have is an error, never silently ignored.
try:
    stb.imwrite(OUT / "bad.png", pixels, quality=30)
except ValueError as e:
    print("ValueError:", e)

# Header only, then the pixels.
print("iminfo :", stb.iminfo(OUT / "pattern.png"))
back = stb.imread(OUT / "pattern.png")
print("imread :", back.shape, back.dtype)
print("exact  :", np.array_equal(back, pixels))

# Same options as Image.open.
print("rgba   :", stb.imread(OUT / "pattern.png", format=stb.PixelFormat.RGBA).shape)
print("flipped:", np.array_equal(stb.imread(OUT / "pattern.png", flip=True), pixels[::-1]))
