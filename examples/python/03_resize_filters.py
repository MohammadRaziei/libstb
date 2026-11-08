"""Resize with every filter, by name or with a Resizer object."""
from pathlib import Path

import numpy as np
import libstb as stb

HERE = Path(__file__).resolve().parent
DATA = HERE.parents[1] / "data"        # the sample pictures and fonts of the repository (see data/README.md)
OUT = HERE / "output"
OUT.mkdir(exist_ok=True)

img = stb.Image.open(DATA / "images" / "astronaut.png")
print("source:", img.width, img.height)

# Filter by name (case-insensitive; "-" and " " count as "_").
for name in ("nearest", "linear", "cubic", "bspline", "mitchell", "box", "auto"):
    small = img.resize(128, 128, name)
    big = img.resize(768, 768, name)
    small.write(OUT / f"resize_{name}_small.png")
    big.write(OUT / f"resize_{name}_big.png")
    print(f"{name:8s} -> {small.width}x{small.height} and {big.width}x{big.height}")

# Default resizer (Catmull-Rom when enlarging, Mitchell when shrinking).
img.resize(64, 64).write(OUT / "resize_default.png")

# Other options are keywords of Resizer(...).
wrap = stb.Resizer(
    stb.Resizer.Filter.MITCHELL, stb.Resizer.Edge.WRAP, srgb=False
)
img.resize(64, 64, wrap).write(OUT / "resize_wrap_linear_light.png")

# A name plus other options.
img.resize(64, 64, stb.Resizer("linear", srgb=False)).write(
    OUT / "resize_linear_no_srgb.png"
)

# Unknown names raise ValueError and list the valid ones.
try:
    img.resize(64, 64, "lanczos")
except ValueError as e:
    print("ValueError:", e)
