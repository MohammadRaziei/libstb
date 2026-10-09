"""Channel conversion, split and merge, and alpha compositing."""
from pathlib import Path

import numpy as np
import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)

# A round, semi-transparent red badge (RGBA) ...
size = 120
y, x = np.mgrid[0:size, 0:size]
inside = (x - size / 2) ** 2 + (y - size / 2) ** 2 < (size / 2 - 2) ** 2
badge_px = np.zeros((size, size, 4), np.uint8)
badge_px[inside] = (220, 30, 30, 200)
badge = libstb.Image(badge_px)

# ... and an opaque RGB background.
yy, xx = np.mgrid[0:200, 0:300]
background = libstb.Image(
    np.dstack([xx * 255 // 299, yy * 255 // 199, np.full_like(xx, 140)]).astype(np.uint8)
)

# convert(channels): 1 gray, 2 gray + alpha, 3 RGB, 4 RGBA.
gray = background.convert(1)
print("gray          :", gray.channels, "channel, top-left value", int(gray.numpy()[0, 0, 0]))
print("RGB -> RGBA   :", background.convert(4).numpy()[0, 0].tolist(), "(a missing alpha is 255)")
print("RGBA -> RGB   :", badge.convert(3).channels, "channels (the alpha is simply dropped)")

# split() / merge(): one single-channel Image per channel, and back, in any order.
r, g, b = background.split()
swapped = libstb.Image.merge([b, g, r])
print("split/merge   :", [p.channels for p in (r, g, b)], "->", swapped.channels, "channels, R and B swapped")

# composite(overlay, x, y): the usual "over" operator. The overlay may hang over the edge.
placed = background.composite(badge, 20, 30)
clipped = background.composite(badge, 250, 150)  # only part of it lands inside
print("composite     :", placed.channels, "channels (the base's channel count is kept)")

# flatten(background): blend the alpha channel onto a colour and drop it (white by default).
on_white = badge.flatten()
on_blue = badge.flatten((20, 40, 160))
print("flatten       :", badge.channels, "->", on_white.channels, "channels")

libstb.imwrite(OUT / "gray.png", gray)
libstb.imwrite(OUT / "swapped_rb.png", swapped)
libstb.imwrite(OUT / "composited.png", placed)
libstb.imwrite(OUT / "composited_clipped.png", clipped)
libstb.imwrite(OUT / "badge_on_white.png", on_white)
libstb.imwrite(OUT / "badge_on_blue.png", on_blue)
libstb.imwrite(OUT / "badge_rgba.png", badge)  # PNG keeps the alpha
print("wrote 7 images to", OUT)
