"""Channel conversion, split and merge, and alpha compositing."""
from pathlib import Path

import numpy as np
import libstb as stb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)

# A round, semi-transparent red badge (RGBA) ...
size = 120
y, x = np.mgrid[0:size, 0:size]
inside = (x - size / 2) ** 2 + (y - size / 2) ** 2 < (size / 2 - 2) ** 2
badge_px = np.zeros((size, size, 4), np.uint8)
badge_px[inside] = (220, 30, 30, 200)
badge = stb.Image(badge_px)

# ... and an opaque RGB background.
yy, xx = np.mgrid[0:200, 0:300]
background = stb.Image(
    np.dstack([xx * 255 // 299, yy * 255 // 199, np.full_like(xx, 140)]).astype(np.uint8)
)

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
clipped = background.blend(badge, 250, 150)  # only part of it lands inside
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
