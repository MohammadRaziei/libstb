#!/usr/bin/env python3
"""Draws data/images/badge.png: a small RGBA picture with a soft transparent edge, made with libstb itself.

    python data/make_badge.py            # rewrites data/images/badge.png

It is the one image of data/ that is not a photograph, and it has what the photographs lack: an alpha channel
(transparent corners, a feathered rim), for blending, masks and RGBA conversions. The text uses
data/fonts/Vazirmatn-Bold.ttf (SIL OFL 1.1).
"""
from pathlib import Path

import numpy as np

import libstb as stb

HERE = Path(__file__).resolve().parent
SIZE = 160

font = stb.Font.open(HERE / "fonts" / "Vazirmatn-Bold.ttf")
img = stb.Image(np.zeros((SIZE, SIZE, 4), np.uint8))                   # fully transparent

# A feathered disc: ten rings of falling opacity, then the solid disc and a thin white ring on top.
canvas = stb.Canvas(img)
for i in range(10):
    grow = 10 - i
    canvas.add_ellipse(8 - grow, 8 - grow, SIZE - 16 + 2 * grow, SIZE - 16 + 2 * grow).fill((230, 57, 70, 18))
canvas.add_ellipse(8, 8, SIZE - 16, SIZE - 16).fill("#e63946").stroke(3, "white")
canvas.set_font(font, 64).set_color("white")
canvas.add_text("stb", SIZE // 2, SIZE // 2 - 46).align("center")
canvas.finish().write(HERE / "images" / "badge.png")
print("wrote", HERE / "images" / "badge.png")
