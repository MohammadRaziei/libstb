"""Command line: `libstb ...` or `python -m libstb ...`.

    libstb --version | --sdk-dir | --cmake-dir | --include-dir | --lib-dir
    libstb info FILE...  [--json]
    libstb convert SRC DST [options]
    libstb zlib compress|decompress SRC DST      ("-" is stdin / stdout)

Exit status: 0 on success, 1 when a file could not be read, converted or written, 2 for a usage error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from typing import List, Optional

from . import (
    DEFAULT_ZLIB_LEVEL,
    Image,
    __version__,
    exif_orientation,
    get_cmake_dir,
    get_include_dir,
    get_lib_dir,
    get_sdk_dir,
    iminfo,
    imwrite,
    zlib_compress,
    zlib_decompress,
)

_FORMATS = ("gray", "gray_alpha", "rgb", "rgba")
_SIZE = re.compile(r"^(\d*)x(\d*)$")


def _size(text: str):
    """'320x240', '320x' or 'x240' -> (width or None, height or None)."""
    m = _SIZE.match(text)
    if not m or not (m.group(1) or m.group(2)):
        raise argparse.ArgumentTypeError(f"{text!r} is not WxH, Wx or xH")
    w = int(m.group(1)) if m.group(1) else None
    h = int(m.group(2)) if m.group(2) else None
    if w == 0 or h == 0:
        raise argparse.ArgumentTypeError("a size cannot be 0")
    return w, h


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="libstb",
        description="Inspect, convert and compress with libstb.",
    )
    p.add_argument("--version", action="version", version=f"libstb {__version__}")
    p.add_argument("--sdk-dir", action="store_true", help="print the root of the C++ SDK (include/, lib/)")
    p.add_argument("--cmake-dir", action="store_true", help="print the directory of libstbConfig.cmake")
    p.add_argument("--include-dir", action="store_true", help="print the directory of stb.h and stb/*.hpp")
    p.add_argument("--lib-dir", action="store_true", help="print the directory of the shared library")

    sub = p.add_subparsers(dest="command", metavar="command")

    info = sub.add_parser("info", help="print width, height, channels and EXIF orientation (header only)")
    info.add_argument("files", nargs="+", metavar="FILE")
    info.add_argument("--json", action="store_true", help="one JSON object per line")

    conv = sub.add_parser(
        "convert",
        help="re-encode an image; the output format comes from the DST extension (.png .jpg .jpeg .bmp .tga)",
    )
    conv.add_argument("src", metavar="SRC")
    conv.add_argument("dst", metavar="DST")
    conv.add_argument("--resize", type=_size, metavar="WxH|Wx|xH",
                      help="new size; a missing side keeps the aspect ratio")
    conv.add_argument("--filter", metavar="NAME", help="resize filter name, e.g. box, cubic, mitchell")
    conv.add_argument("--pixel-format", choices=_FORMATS, help="convert the channels")
    conv.add_argument("--orient", action="store_true", help="apply the EXIF orientation")
    conv.add_argument("--quality", type=int, metavar="1-100", help="JPEG quality")
    conv.add_argument("--compression", type=int, metavar="1-9", help="PNG compression level")
    conv.add_argument("--no-rle", dest="rle", action="store_false", default=None,
                      help="TGA: do not run-length encode")

    z = sub.add_parser("zlib", help="compress or decompress a zlib stream (RFC 1950)")
    zsub = z.add_subparsers(dest="direction", metavar="compress|decompress", required=True)
    zc = zsub.add_parser("compress")
    zc.add_argument("src", metavar="SRC")
    zc.add_argument("dst", metavar="DST")
    zc.add_argument("-l", "--level", type=int, default=DEFAULT_ZLIB_LEVEL, metavar="-1..9")
    zd = zsub.add_parser("decompress")
    zd.add_argument("src", metavar="SRC")
    zd.add_argument("dst", metavar="DST")
    return p


def _info_one(path: str, as_json: bool) -> None:
    i = iminfo(path)
    fmt = i.format.name.lower()
    orientation = exif_orientation(path)
    size = os.path.getsize(path)
    if as_json:
        print(json.dumps({
            "file": path, "width": i.width, "height": i.height, "channels": i.channels,
            "pixel_format": fmt, "exif_orientation": orientation, "bytes": size,
        }))
    else:
        extra = f", EXIF orientation {orientation}" if orientation != 1 else ""
        print(f"{path}: {i.width}x{i.height}, {i.channels} channels ({fmt}){extra}, {size} bytes")


def _convert(a: argparse.Namespace) -> None:
    img = Image.open(a.src, format=a.pixel_format, orient=a.orient)
    if a.resize is not None:
        w, h = a.resize
        if w is None:
            w = max(1, round(img.width * h / img.height))
        elif h is None:
            h = max(1, round(img.height * w / img.width))
        img = img.resize(w, h, a.filter)
    elif a.filter is not None:
        raise ValueError("--filter needs --resize")
    imwrite(a.dst, img, quality=a.quality, compression=a.compression, rle=a.rle)


def _read(path: str) -> bytes:
    if path == "-":
        return sys.stdin.buffer.read()
    with open(path, "rb") as f:
        return f.read()


def _write(path: str, data: bytes) -> None:
    if path == "-":
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
    else:
        with open(path, "wb") as f:
            f.write(data)


def _zlib(a: argparse.Namespace) -> None:
    data = _read(a.src)
    out = zlib_compress(data, a.level) if a.direction == "compress" else zlib_decompress(data)
    _write(a.dst, out)


def main(argv: Optional[List[str]] = None) -> int:
    parser = _build_parser()
    a = parser.parse_args(argv)

    paths = (("sdk_dir", get_sdk_dir), ("cmake_dir", get_cmake_dir), ("include_dir", get_include_dir), ("lib_dir", get_lib_dir))
    asked = [fn for name, fn in paths if getattr(a, name)]
    if asked:
        for fn in asked:
            print(fn())
        return 0
    if a.command is None:
        parser.print_help()
        return 2

    failed = False
    try:
        if a.command == "info":
            for path in a.files:
                try:
                    _info_one(path, a.json)
                except Exception as e:  # keep going: one bad file must not hide the others
                    print(f"libstb: error: {path}: {e}", file=sys.stderr)
                    failed = True
        elif a.command == "convert":
            _convert(a)
        else:
            _zlib(a)
    except Exception as e:
        print(f"libstb: error: {e}", file=sys.stderr)
        return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
