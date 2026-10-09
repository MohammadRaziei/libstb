"""Crop, flip, rotate, transpose, pad and thumbnail. Every call returns a new Image."""
from pathlib import Path

import numpy as np
import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)


def make_test_image(width=160, height=100):
    """A gradient with a white 'F' in the top-left corner, so every flip and turn shows."""
    y, x = np.mgrid[0:height, 0:width]
    px = np.dstack([x * 255 // (width - 1), y * 255 // (height - 1), np.full_like(x, 80)]).astype(np.uint8)
    px[8:48, 8:16] = 255
    px[8:16, 8:36] = 255
    px[24:32, 8:28] = 255
    return libstb.Image(px)


def contact_sheet(images, gap=10):
    """Put images side by side on white, each padded to the same size."""
    h = max(i.height for i in images)
    w = max(i.width for i in images)
    cells = [i.pad(0, 0, w - i.width, h - i.height, fill=255).pad(gap, gap, gap, gap, fill=255) for i in images]
    return libstb.Image(np.hstack([c.numpy() for c in cells]))


img = make_test_image()
print("source        :", img.width, "x", img.height)

# Crop: the rectangle must lie inside the image (ValueError otherwise).
face = img.crop(8, 8, 40, 40)
print("crop          :", face.width, "x", face.height)

# Flips and quarter turns. rotate90 turns clockwise; a negative count turns the other way.
flipped_h = img.flip_horizontal()
flipped_v = img.flip_vertical()
turned = img.rotate90()            # clockwise: 160x100 becomes 100x160
turned_back = turned.rotate90(-1)  # and back
print("rotate90      :", turned.width, "x", turned.height, "| back to the original:",
      np.array_equal(turned_back.numpy(), img.numpy()))
transposed = img.transpose()       # swap rows and columns (a mirror across the diagonal)

# Pad: a border of the given widths. fill is one int for every channel, or one value per channel.
framed = img.pad(10, 10, 10, 10, fill=(255, 255, 255))
print("pad           :", framed.width, "x", framed.height)

# Thumbnail: fit inside a box and keep the aspect ratio. It never enlarges.
thumb = img.thumbnail(64, 64)
same = img.thumbnail(1000, 1000)
print("thumbnail     :", thumb.width, "x", thumb.height, "| already small enough:", same.width, "x", same.height)
print("with a filter :", img.thumbnail(64, 64, "cubic").width, "(any resize() filter works)")

# Nothing above touched the source.
print("source intact :", img.width, "x", img.height)

libstb.imwrite(OUT / "geometry_overview.png", contact_sheet([
    img, flipped_h, flipped_v, turned, transposed, framed, thumb,
]))
print("wrote", OUT / "geometry_overview.png")
