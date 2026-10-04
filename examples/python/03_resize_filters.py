"""Resize with every filter, by name or with a Resizer object."""
from pathlib import Path

import numpy as np
import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)


def make_test_image(size=128):
    """Checkerboard over a gradient: sharp edges show off the filters."""
    y, x = np.mgrid[0:size, 0:size]
    grad = (x * 255 // (size - 1)).astype(np.uint8)
    checks = ((x // 8 + y // 8) % 2 * 255).astype(np.uint8)
    return libstb.Image(np.dstack([grad, checks, 255 - grad]))


img = make_test_image()
print("source:", img.width, img.height)

# Filter by name (case-insensitive; "-" and " " count as "_").
for name in ("nearest", "linear", "cubic", "bspline", "mitchell", "box", "auto"):
    small = img.resize(48, 48, name)
    big = img.resize(384, 384, name)
    small.write(OUT / f"resize_{name}_small.png")
    big.write(OUT / f"resize_{name}_big.png")
    print(f"{name:8s} -> {small.width}x{small.height} and {big.width}x{big.height}")

# Default resizer (Catmull-Rom when enlarging, Mitchell when shrinking).
img.resize(64, 64).write(OUT / "resize_default.png")

# Other options are keywords of Resizer(...).
wrap = libstb.Resizer(
    libstb.Resizer.Filter.MITCHELL, libstb.Resizer.Edge.WRAP, srgb=False
)
img.resize(64, 64, wrap).write(OUT / "resize_wrap_linear_light.png")

# A name plus other options.
img.resize(64, 64, libstb.Resizer("linear", srgb=False)).write(
    OUT / "resize_linear_no_srgb.png"
)

# Unknown names raise ValueError and list the valid ones.
try:
    img.resize(64, 64, "lanczos")
except ValueError as e:
    print("ValueError:", e)
