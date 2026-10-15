#!/usr/bin/env python3
"""Render the landing page's live demo with libstb itself.

The hero of the docs site lets a visitor drag an output size and pick a filter. Every frame it
shows is a file this script wrote with libstb (Image.resize with a filter by name, then
Image.write), so the page demonstrates the library rather than imitating it. The frames are tiny
PNGs, shown enlarged with image-rendering: pixelated.

    python docs/make_showcase.py --out <site dir>/showcase [<source photo>]

Run by docs/CreateDocs.cmake while the site is built, so the frames are never committed. With no
source photo it downloads fruits.jpg from OpenCV's sample data (Apache-2.0) into the temp folder.
The source is cropped to its central square first. Needs `pip install libstb`.
"""
import argparse
import json
import tempfile
import urllib.request
from pathlib import Path

import libstb as stb

SIZES = (8, 12, 16, 24, 32, 48, 64, 96, 128)
FILTERS = ("box", "linear", "cubic")
PHOTO_URL = "https://raw.githubusercontent.com/opencv/opencv/4.x/samples/data/fruits.jpg"


def default_photo():
    path = Path(tempfile.gettempdir()) / "libstb-showcase-fruits.jpg"
    if not path.exists():
        urllib.request.urlretrieve(PHOTO_URL, path)
    return str(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", nargs="?", help="a photograph (default: download fruits.jpg)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    img = stb.Image.open(args.source or default_photo(), channels=3)
    side = min(img.width, img.height)
    square = img.crop((img.width - side) // 2, (img.height - side) // 2, side, side)

    frames = {}
    for name in FILTERS:
        for n in SIZES:
            frame = square.resize(n, n, name)
            rel = f"{name}-{n}.png"
            frame.write(str(out / rel))
            frames.setdefault(name, {})[n] = rel
    (out / "demo.json").write_text(json.dumps({"sizes": SIZES, "filters": FILTERS, "source": f"{img.width}x{img.height}",
                                               "libstb": stb.__version__}, indent=1) + "\n")
    print(f"wrote {len(FILTERS) * len(SIZES)} frames to {out}")


if __name__ == "__main__":
    main()
