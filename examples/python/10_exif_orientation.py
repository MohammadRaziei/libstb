"""EXIF orientation: phone photos are stored sideways and carry a tag saying how to turn them.

No camera needed: this builds the files itself, writing the tag the way a phone does.
"""
import struct
import zlib
from pathlib import Path

import numpy as np
import libstb

OUT = Path(__file__).parent / "output"
OUT.mkdir(exist_ok=True)


def exif_tiff(orientation):
    """The EXIF payload (a little TIFF structure) with just the Orientation tag."""
    header = b"II" + struct.pack("<HI", 42, 8)
    entry = struct.pack("<HHIHH", 0x0112, 3, 1, orientation, 0)  # tag, SHORT, count 1, value
    return header + struct.pack("<H", 1) + entry + struct.pack("<I", 0)


def png_with_orientation(png, orientation):
    """Insert an eXIf chunk right after the IHDR chunk of a PNG."""
    data = exif_tiff(orientation)
    chunk = struct.pack(">I", len(data)) + b"eXIf" + data + struct.pack(">I", zlib.crc32(b"eXIf" + data))
    return png[:33] + chunk + png[33:]


def jpeg_with_orientation(jpg, orientation):
    """Insert an Exif APP1 segment right after the SOI marker of a JPEG."""
    data = b"Exif\0\0" + exif_tiff(orientation)
    return jpg[:2] + b"\xff\xe1" + struct.pack(">H", len(data) + 2) + data + jpg[2:]


# A recognisable picture: a gradient with a white 'F' in the top-left corner.
y, x = np.mgrid[0:60, 0:90]
px = np.dstack([x * 255 // 89, y * 255 // 59, np.full_like(x, 90)]).astype(np.uint8)
px[6:34, 6:11] = 255
px[6:11, 6:24] = 255
px[16:21, 6:19] = 255
upright = libstb.Image(px)

# A phone held sideways stores the picture turned and writes orientation 6 ("turn 90 degrees
# clockwise to see it upright"). Store it turned the opposite way and tag it.
sideways = upright.rotate90(-1)
photo = png_with_orientation(sideways.to_png(), 6)
(OUT / "photo_orientation6.png").write_bytes(photo)
(OUT / "photo_orientation6.jpg").write_bytes(jpeg_with_orientation(sideways.to_jpg(quality=95), 6))

# 1. Read the tag.
print("exif_orientation :", libstb.exif_orientation(OUT / "photo_orientation6.png"))
print("no tag           :", libstb.exif_orientation(upright.to_png()), "(1 means upright)")

# 2. By default decoding gives the stored pixels, sideways ...
raw = libstb.imread(photo)
print("stored size      :", raw.shape[1], "x", raw.shape[0])

# ... and orient=True gives the picture the way it was meant to be seen.
fixed = libstb.imread(photo, orient=True)
print("orient=True      :", fixed.shape[1], "x", fixed.shape[0], "| upright again:",
      np.array_equal(fixed, upright.numpy()))

# 3. Or do it in two steps, to look at the tag first. Works on any Image.
turn = libstb.exif_orientation(photo)
img = libstb.Image.open(photo).orient(turn)
print("open + orient    :", img.width, "x", img.height)

# 4. orient=True works for JPEG too, and combines with flip=True (the flip comes last).
jpg = libstb.Image.open(OUT / "photo_orientation6.jpg", orient=True)
print("jpeg             :", jpg.width, "x", jpg.height)
flipped = libstb.imread(photo, orient=True, flip=True)
print("orient + flip    :", np.array_equal(flipped, fixed[::-1]))

# 5. All eight values: the same stored picture seen with each tag.
tiles = [libstb.Image(upright.numpy()).pad(6, 6, 6, 6, fill=255).orient(o) for o in range(1, 9)]
width = max(t.width for t in tiles)
height = max(t.height for t in tiles)
row = [t.pad(0, 0, width - t.width, height - t.height, fill=255).numpy() for t in tiles]
libstb.imwrite(OUT / "orientations_1_to_8.png", np.hstack(row))
print("wrote", OUT / "orientations_1_to_8.png", "(orient(1) .. orient(8) of one picture)")
