<div align="center">

<img src="https://raw.githubusercontent.com/MohammadRaziei/libstb/master/docs/images/libstb-logo.png" width="160" alt="libstb logo"/>

# libstb

**Decode, encode, resize, edit, draw text and compress, from Python and C++.**<br/>
No system libraries and no Python dependencies.

[![CI](https://github.com/MohammadRaziei/libstb/actions/workflows/cmake.yml/badge.svg)](https://github.com/MohammadRaziei/libstb/actions/workflows/cmake.yml)
[![Python](https://img.shields.io/badge/python-3.9%2B-blue.svg)](pyproject.toml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](CMakeLists.txt)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.txt)

[Quick start](#quick-start) ·
[Command line](#command-line) ·
[C++ with CMake](#c-with-cmake) ·
[What is from stb](#what-is-from-stb-and-what-is-not) ·
[Guide](docs/guide.md) ·
[Changelog](CHANGELOG.md)

</div>

---

```python
import libstb as stb

img = stb.Image.open("photo.jpg")          # PNG, JPEG, BMP, TGA, GIF, ... or bytes
img.resize(320, 240, "cubic").write("small.png")   # the format comes from the extension
```

> [!NOTE]
> **libstb inherits a lot from [stb](https://github.com/nothings/stb), and it is a complete
> refactor.** The decoders, encoders, resizer and font rasterizer descend from Sean Barrett's stb
> libraries, which is where the name comes from. But no stb header is vendored: the code is
> reorganised into classes and one file per topic, parts are redesigned around a modern, typed
> API (explicit ownership, typed errors, no hidden copies), and several hot paths are faster.
> See [what is from stb, and what is not](#what-is-from-stb-and-what-is-not).

## Highlights

- **Python and C++, one library.** The same operations in `stb::image` (C++17) and
  `libstb.Image` (nanobind). The Linux x86-64 wheel is about 0.5 MB.
- **Many formats in, four out.** Decodes PNG, JPEG, BMP, TGA, GIF and more; encodes PNG, JPEG,
  BMP and TGA.
- **Editing, not just I/O.** Crop, pad, flips, rotation, thumbnail, channel conversion, alpha
  blend modes, fill, masks, stacking, EXIF orientation, and a canvas for text and shapes.
- **Text, including Persian and Arabic.** TrueType and CFF fonts, GSUB shaping and bidi
  reordering (no GPOS), plus a text size estimate that needs no font file.
- **zlib as an API.** `zlib_compress` and `zlib_decompress`, with bounded output so a zip bomb
  is a `LimitError`.
- **Safe by default.** Size limits are checked from the header before pixel memory is allocated,
  errors are typed (`DecodeError`, `EncodeError`, `LimitError`), and EXIF parsing checks every
  offset.
- **Fast where it pays.** SIMD kernels (AVX2, SSE2, scalar) picked at run time with identical
  results, decoded buffers adopted instead of copied, the GIL released in Python calls.

## Installation

```sh
pip install libstb
```

| Command | Adds |
| --- | --- |
| `pip install "libstb[numpy]"` | `Image.numpy()`, `imread`, `imresize` (zero-copy views). |
| `pip install "libstb[pillow]"` | `Image.to_pil()` and `Image.from_pil()`. |

C++ projects use CMake: see [C++ with CMake](#c-with-cmake).

## Quick start

```python
import libstb as stb

img = stb.Image.open("photo.jpg")                 # PNG, JPEG, BMP, TGA, GIF, ... (or bytes)
img.width, img.height, img.channels               # 640, 480, 3

small = img.resize(320, 240, "cubic")             # filter by name: nearest, linear, cubic, mitchell, box...
small.write("small.png")                          # format from the extension (png, jpg, bmp, tga)
small.write_jpg("small.jpg", quality=80)          # or choose the format and its settings
data = small.to_png(compression=9)                # -> bytes

thumb = img.thumbnail(256, 256)                   # fits the box, never enlarges
img.crop(10, 10, 200, 100).rotate90().convert(stb.PixelFormat.GRAY)  # every edit returns a new Image

pixels = img.numpy()                              # zero-copy view (needs numpy); DLPack works too
photo = stb.Image.open("IMG_0001.jpg", orient=True)   # apply the EXIF rotation while decoding

font = stb.Font.open("font.ttf")                  # trusted fonts only
labeled = img.draw_text("Hello", font, 32, 10, 10, color=(255, 255, 255))   # text onto a copy
centred = img.draw_text("Hello", font, 32, img.width // 2, 10, align="center")  # x is the centre of the line
font.render("Hello", 32).bitmap.write("hello.png")
# Persian / Arabic text works as typed: GSUB shaping and bidi reordering (no GPOS, see docs/guide.md)
stb.estimate_text("Hello", 16)                    # text size with no font file at all

packed = stb.zlib_compress(data)                  # zlib streams: the compressor that writes PNG
data = stb.zlib_decompress(packed, max_bytes=64 << 20)   # output is bounded: a zip bomb is a LimitError
```

## Command line

`libstb` (or `python -m libstb`) comes with the Python package:

```sh
libstb --version
libstb info photo.jpg --json
libstb convert photo.jpg small.png --resize 320x           # a missing side keeps the aspect ratio
libstb convert in.png out.jpg --quality 85 --pixel-format gray --orient
libstb zlib compress data.bin data.bin.z -l 9
libstb --cmake-dir                                         # also --include-dir, --lib-dir
```

The output format of `convert` comes from the extension of the destination. Exit status is 0 on
success, 1 when a file could not be read or written, and 2 for a usage error.

## C++ with CMake

```cmake
include(FetchContent)
FetchContent_Declare(libstb GIT_REPOSITORY https://github.com/MohammadRaziei/libstb.git GIT_TAG master)
FetchContent_MakeAvailable(libstb)

target_link_libraries(my_app PRIVATE libstb::libstb)    # CMake 3.21+, C++17
```

Or use the prebuilt library from `pip install libstb`:

```cmake
execute_process(COMMAND libstb --cmake-dir
                OUTPUT_VARIABLE libstb_DIR OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
find_package(libstb CONFIG REQUIRED)

target_link_libraries(my_app PRIVATE libstb::libstb)
```

`libstb` must be on the `PATH` (the Python environment where you ran `pip install`); otherwise
use `python -m libstb --cmake-dir`. `find_package(libstb CONFIG)` also works after
`cmake --install`.

```cpp
#include <stb.h>

stb::image img = stb::image::open("photo.jpg");          // throws stb::error subclasses
stb::image small = img.resize(320, 240, "cubic");        // or stb::resize_filter::mitchell
stb::image gray = small.convert(stb::pixel_format::gray);  // gray, gray_alpha, rgb or rgba
small.write("small.png");                                // format from the extension
small.write_jpg("small.jpg", 80);
std::vector<std::uint8_t> bytes = small.to_png(9);

stb::font f = stb::font::open("font.ttf");               // cheap to copy, thread-safe
stb::image labeled = f.draw_text(img, "Hello", 32, 10, 10, stb::color("white"));
stb::image centred = f.draw_text(img, "Hello", 32, img.width() / 2, 10, stb::color(), stb::text_align::center);
stb::text_bitmap t = f.render("Hello", 32);
stb::text_size est = stb::estimate_text("Hello", 16);    // no font file needed

std::vector<std::uint8_t> z = stb::zlib_compress(bytes);                 // <stb/zlib.hpp>, level 0..9 like zlib
std::vector<std::uint8_t> back = stb::zlib_decompress(z, 64u << 20);     // throws limit_error past max_bytes
```

The namespace and headers are `stb` (`<stb.h>`, `<stb/*.hpp>`); the CMake target
(`libstb::libstb`) and the Python package (`libstb`) keep the project name. The core is a shared
library. No stb header is included from a public header, and stb symbols do not leak from the
library, so it links next to your own copy of stb.

## What is from stb, and what is not

libstb is not a wrapper around stb. Its image decoders, encoders, resizer and font reader are
**derived from stb's algorithms and rewritten as libstb's own code**; everything around them is
new. Each derived file says so in its header, and [`NOTICE.md`](NOTICE.md) has the lineage and
the license (stb is public domain or MIT; libstb uses the MIT option).

**Descended from stb** (same algorithms, reorganised: one file per format or topic, classes for
stateful code, bounds-checked reads, no global state):

| Area | From | Lives in |
| :--- | :--- | :--- |
| Decoders: PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PIC, PNM, and the inflater | `stb_image` v2.30 | `src/codec/decode/` |
| Encoders: PNG, JPEG, BMP, TGA, and the zlib compressor | `stb_image_write` v1.16 (the JPEG encoder is based on jo_jpeg) | `src/codec/` |
| Resizer, all filters | `stb_image_resize2` v2.18 | `src/image/resize/` |
| Font file, glyph outlines (TrueType and CFF), rasterizer, atlas packer | `stb_truetype` v1.26, `stb_rect_pack` v1.01 | `src/font/truetype/` |

**Written for libstb** (no stb counterpart):

| Area | What |
| :--- | :--- |
| Public API | `stb::image`, `stb::font`, `stb::resizer`, `stb::canvas`, typed errors, explicit ownership; the whole Python package (nanobind) |
| Editing | crop, pad, flips, rotation, thumbnail, channel conversion, flatten, `blend` modes, `fill`, alpha masks, `hstack` / `vstack` |
| SIMD kernels | `convert`, `flatten` and `blend` with AVX2, SSE2 and scalar backends, picked at run time (`src/image/`) |
| EXIF | orientation reader for JPEG and PNG, written for untrusted input |
| Text | GSUB shaping, OpenType layout, bidi (UAX #9), Arabic joining, `draw_text`, `estimate_text` (`src/font/shaper/`) |
| Drawing | colours, shapes and the canvas (`src/draw/`) |
| zlib | the public `zlib_compress` / `zlib_decompress` API on top of the codec code |
| Tooling | the command line, the CMake package, the tests and the benchmarks |

**What changed on the inherited parts:**

- **Faster encoders.** The PNG deflate uses 4-byte hash chains, lazy matching and dynamic Huffman
  blocks; the JPEG encoder has a buffered bit writer, a vectorisable DCT and an SSE2 colour
  conversion. The compressor measured 1.2x to 1.9x faster than zlib level 6 with 0.4% to 3.3%
  larger output on the project's own samples (source code, a binary, image pixels). Measure on
  your data.
- **Inflate is not faster.** The decompressor is the stb_image-derived inflater: about 2.5x
  slower than zlib on very compressible text, so use zlib if inflate speed is what you need.
- **Safer.** Header-based size limits, bounds-checked reads in the font reader (stb_truetype did
  not check), and typed errors. The font reader has not been fuzzed: prefer fonts you trust.
- **Refactor checks.** `tests/tools/snapshot.py` and `decode_snapshot.py` compare a new build
  against an old one operation by operation, and `jpeg_vs_upstream.py` compares the JPEG encoder
  with upstream.

Performance numbers against Pillow and OpenCV, including where libstb is slower (JPEG encoding
is one), are in [`benchmarks/`](benchmarks/README.md). They depend on the machine, so run them
on yours.

## Documentation

- [`docs/guide.md`](docs/guide.md): the full reference (every call, SIMD, EXIF, DLPack, Pillow,
  errors, design and source layout).
- `docs/`: the Sphinx (Python) and Doxygen (C++) sources of the documentation site.
- [`CHANGELOG.md`](CHANGELOG.md) and [`NOTICE.md`](NOTICE.md).

## Build and test

```sh
pip install -r requirements-dev.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `STB_BUILD_TESTS`, `STB_BUILD_PYTHON`, `STB_BUILD_EXAMPLES` (off by default),
`STB_BUILD_DOCS`. Targets: `stb_core`, `stb_test`, `stb_docs`, `stb_benchmark`.

## License

MIT for libstb. The code derived from stb is MIT / public domain (see [`NOTICE.md`](NOTICE.md)).
