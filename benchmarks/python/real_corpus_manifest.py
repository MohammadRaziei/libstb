"""
real_corpus_manifest.py: turn a folder of real photographs (downloaded
by cmake/FetchRealCorpus.cmake) into the same manifest shape corpus.py
produces, so every benchmark script treats real and synthetic entries
identically.

Each photograph is decoded once with Pillow, flattened to RGB, and
re-written as png, jpg, bmp and tga (same fixed settings as corpus.py),
so the decode benchmarks run on real image content in every format
without any of it having been produced by stb.
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image

from corpus import manifest_entries

EXTENSIONS = (".jpg", ".jpeg", ".png")


def build(corpus_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    manifest = []
    for name in sorted(os.listdir(corpus_dir)):
        stem, ext = os.path.splitext(name)
        if ext.lower() not in EXTENSIONS:
            continue
        with Image.open(os.path.join(corpus_dir, name)) as im:
            arr = np.ascontiguousarray(np.asarray(im.convert("RGB")))
        h, w = arr.shape[:2]
        manifest.extend(manifest_entries(f"real-{stem}", f"{w}x{h}", arr, os.path.join(out_dir, f"real_{stem}")))
    return manifest


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("corpus_dir", help="folder holding the downloaded real photographs")
    p.add_argument("out_dir", help="folder to write the per-format image files into")
    p.add_argument("manifest", help="path to write the manifest JSON to")
    args = p.parse_args()

    manifest = build(args.corpus_dir, args.out_dir)
    with open(args.manifest, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    print(f"real_corpus_manifest: {len(manifest)} files from {args.corpus_dir} -> {args.manifest}", file=sys.stderr)
