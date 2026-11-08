"""The sample pictures and fonts of data/ (see data/README.md), for the tests that want real-world input.

tests/data/ holds the synthetic fonts whose every glyph is known to the unit (exact expectations); this is the
other kind of input: photographs, an RGBA picture and a real font with GSUB and GPOS.
"""
from pathlib import Path

DATA = Path(__file__).resolve().parents[2] / "data"
IMAGES = DATA / "images"
FONTS = DATA / "fonts"

# file name -> (width, height, channels)
PICTURES = {
    "camera.png": (512, 512, 1),
    "astronaut.png": (512, 512, 3),
    "chelsea.png": (451, 300, 3),
    "rocket.jpg": (640, 427, 3),
    "badge.png": (160, 160, 4),
}
PNGS = [name for name in PICTURES if name.endswith(".png")]

REGULAR = FONTS / "Vazirmatn-Regular.ttf"
BOLD = FONTS / "Vazirmatn-Bold.ttf"
