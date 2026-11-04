"""Vector outlines: fill, stroke with caps, joins and dashes, a canvas, and a coverage mask.

    python 12_vector_outlines.py out.png
"""
import math
import sys

import libstb as stb

out = sys.argv[1] if len(sys.argv) > 1 else "outlines.png"

import numpy as np

img = stb.Image(np.full((240, 360, 4), (250, 248, 240, 255), np.uint8))  # a cream background

heart = stb.Outline().move_to(60, 190).cubic_to(-20, 120, 30, 40, 60, 90).cubic_to(90, 40, 140, 120, 60, 190).close()
img = img.fill_outline(heart, "#e63946").stroke_outline(heart, "#1d3557", 3, cap="round", join="round", dashes=[8, 6])

star = stb.Outline()
for i in range(5):
    a = 2 * math.pi * (i * 2) / 5
    (star.move_to if i == 0 else star.line_to)(50 * math.sin(a), -50 * math.cos(a))
star.close()
place = stb.Matrix().translate(200, 90).rotate(0.2)
img = img.fill_outline(star, "#f4a261", rule="even_odd", matrix=place).stroke_outline(star, "#264653", 2, matrix=place)

zigzag = stb.Outline().move_to(0, 40).line_to(25, 0).line_to(50, 40).line_to(75, 0)
canvas = stb.Canvas(img)
for i, (cap, join) in enumerate([("butt", "miter"), ("round", "round"), ("square", "bevel")]):
    canvas.add_outline(zigzag).no_fill().stroke(10, "#2a9d8f").cap(cap).join(join).transform(
        stb.Matrix.translation(255, 20 + 62 * i)
    ).opacity(0.9)
canvas.finish().write(out)

# The coverage of a shape as a gray image: use it as a mask for your own colours.
mask = heart.fill_mask(120, 200)
print("wrote", out, "- mask coverage at the centre:", mask.numpy().reshape(200, 120)[100, 60])
