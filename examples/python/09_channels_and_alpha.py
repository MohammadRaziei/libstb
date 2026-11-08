"""Channel conversion, split and merge, and alpha compositing."""
from pathlib import Path

import numpy as np
import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

# A round badge with a feathered, transparent rim (RGBA) ...
badge = stb.Image.open(DATA / "images" / "badge.png")
print("badge         :", badge.width, "x", badge.height, badge.channels, "channels")

# ... and an opaque RGB photo.
background = stb.Image.open(DATA / "images" / "chelsea.png")

# convert(format): PixelFormat.GRAY, GRAY_ALPHA, RGB or RGBA (or the name, "gray" ...).
gray = background.convert(stb.PixelFormat.GRAY)
print("gray          :", gray.channels, "channel, top-left value", int(gray.numpy()[0, 0, 0]))
print("RGB -> RGBA   :", background.convert(stb.PixelFormat.RGBA).numpy()[0, 0].tolist(), "(a missing alpha is 255)")
print("RGBA -> RGB   :", badge.convert(stb.PixelFormat.RGB).channels, "channels (the alpha is simply dropped)")

# split() / merge(): one single-channel Image per channel, and back, in any order.
r, g, b = background.split()
swapped = stb.Image.merge([b, g, r])
print("split/merge   :", [p.channels for p in (r, g, b)], "->", swapped.channels, "channels, R and B swapped")

# blend(overlay, x, y): the usual "over" operator. The overlay may hang over the edge.
# mode= ("multiply", "screen", ...) and opacity= change how it mixes; it returns a new Image.
placed = background.blend(badge, 20, 30)
clipped = background.blend(badge, 360, 200)  # only part of it lands inside
print("blend         :", placed.channels, "channels (the base's channel count is kept)")

# flatten(background): blend the alpha channel onto a colour and drop it (white by default).
on_white = badge.flatten()
on_blue = badge.flatten((20, 40, 160))
print("flatten       :", badge.channels, "->", on_white.channels, "channels")

stb.imwrite(OUT / "gray.png", gray)
stb.imwrite(OUT / "swapped_rb.png", swapped)
stb.imwrite(OUT / "composited.png", placed)
stb.imwrite(OUT / "composited_clipped.png", clipped)
stb.imwrite(OUT / "badge_on_white.png", on_white)
stb.imwrite(OUT / "badge_on_blue.png", on_blue)
stb.imwrite(OUT / "badge_rgba.png", badge)  # PNG keeps the alpha
print("wrote 7 images to", OUT)
