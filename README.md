# libstb

Python bindings for the [stb](https://github.com/nothings/stb) single-header
libraries. `pip install libstb` and you have image/font processing with **no
system dependencies**. The C++ library is usable from CMake too.

| module | status |
| :--- | :--- |
| `stb_image` (read) | done |
| `stb_image_write` (write: png, jpg, bmp, tga) | done |
| `stb_image_resize2` (resize) | done |
| `stb_truetype` (fonts: metrics, glyphs, text, atlas) | done |
| `stb_rect_pack` | used internally by the atlas packer |

## Python

```python
import numpy as np
import libstb

img = libstb.Image.open("photo.png")                # or bytes; also: channels=4, flip=True
img.width, img.height, img.channels                 # 640, 480, 3
img.numpy()                                         # uint8 pixels (H, W, C), no copy; np.asarray(img) works too
libstb.ImageInfo.read("photo.png")                  # header only: (width, height, channels)
libstb.Image(np.zeros((8, 8, 4), np.uint8))         # wrap your own uint8 array

img.save("out.jpg")                                 # format from the extension, default settings
img.write_jpg("out.jpg", quality=80)                # or pick the format and its settings
data = img.to_png(compression=9)                    # -> bytes (to_png / to_jpg / to_bmp / to_tga)

small = img.resize(320, 240)                        # new Image; default Resizer()
small = img.resize(320, 240, "cubic")               # filter by name (see the table below)
libstb.Resizer("cubic")                             # same name works on Resizer itself
libstb.Resizer(libstb.Resizer.Filter.MITCHELL, libstb.Resizer.Edge.WRAP, srgb=False)
img.resize(320, 240, libstb.Resizer("linear", srgb=False))  # name + other options

font = libstb.Font.open("font.ttf")                 # trusted fonts only, see below
text = font.render("Hello\nworld", 32)              # RenderedText(bitmap: Image (1 channel), origin_x, origin_y)
text.bitmap.save("hello.png")
font.metrics(32), font.advance("A", 32), font.kerning("A", "V", 32), font.measure("Hello", 32)
font.render_glyph("A", 32)                          # Glyph(bitmap, x_offset, y_offset, advance)
atlas = font.make_atlas("ABCabc123", 32, 256, 256)  # Atlas: .image and atlas["A"] -> AtlasGlyph

libstb.load("photo.png")                            # shortcut: Image.open(...).array
libstb.info("photo.png")                            # shortcut: ImageInfo.read(...)
```

**Encoding.** Every format has a pair of methods with its own settings, all
defaulted: `to_png(compression=8)`, `to_jpg(quality=90)`, `to_bmp()`,
`to_tga(rle=True)` return the file's bytes, and `write_png(path, ...)`,
`write_jpg(path, ...)`, `write_bmp(path)`, `write_tga(path, ...)` do the same
and write it (only after encoding succeeded, so a failure never leaves a
truncated file). `save(path)` is the shortcut: it picks the format from the
extension (`.png .jpg .jpeg .bmp .tga`, case-insensitive; `ValueError` for
anything else) and uses the defaults. For other settings call `write_*`.

**Resize filters by name.** `Image.resize(w, h, x)` and `Resizer(x)` accept a
`Resizer.Filter`, or a case-insensitive name (`-` and space count as `_`). Any
other option (`edge`, `srgb`, `max_bytes`) is a keyword of `Resizer(...)`.
`Image.resize` also takes a ready-made `Resizer`, or `None` for the default.

| name | filter | note |
| :--- | :--- | :--- |
| `auto`, `automatic`, `default` | `DEFAULT` | Catmull-Rom when enlarging, Mitchell when shrinking |
| `nearest`, `point` | `POINT` | nearest neighbour |
| `linear`, `bilinear`, `triangle` | `TRIANGLE` | |
| `cubic`, `bicubic`, `catmull_rom` | `CATMULL_ROM` | sharp; the PIL/OpenCV meaning of "cubic" |
| `bspline`, `cubic_bspline` | `CUBIC_BSPLINE` | smooth, slightly blurry; scipy's meaning of "cubic" |
| `mitchell` | `MITCHELL` | good all-round compromise |
| `box`, `area` | `BOX` | |

An unknown name raises `ValueError` listing the valid ones. There is no
`lanczos`: stb_image_resize2 does not have it.

**Fonts: trusted files only.** stb_truetype does no bounds checking; its
author writes "NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT
FILES". Load your own or vetted system fonts, never user uploads. (Images are
different: `stb_image` is fuzzed, and libstb adds dimension and size limits.)

Errors mirror the C++ hierarchy: `libstb.Error` (a `RuntimeError`) with
`DecodeError`, `EncodeError`, `LimitError` below it, so a single
`except libstb.Error` catches every libstb failure. Bad arguments raise
`ValueError`, unreadable files `OSError`. Images whose decoded size would
exceed `max_bytes` (default 512 MiB) are rejected from the header, before any
pixel memory is allocated.

## C++ / CMake

```cmake
# after `pip install libstb`
execute_process(COMMAND ${Python3_EXECUTABLE} -c "import libstb; print(libstb.get_cmake_dir())"
    OUTPUT_VARIABLE libstb_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
find_package(libstb CONFIG REQUIRED)
target_link_libraries(app PRIVATE libstb::core)
```

The C++ namespace and headers are `stb` (`<stb.h>`, `<stb/*.hpp>`); the CMake
target (`libstb::core`) and the Python package (`libstb`) keep the project name.

```cpp
#include <stb.h>

stb::image img = stb::image::open("in.png");        // throws stb::error subclasses
img(0, 0, 1) = 255;                                       // unchecked pixel access
img.save("out.jpg");                                      // format from the extension, default settings
img.write_jpg("out.jpg", 80);                             // or pick the format and its settings
std::vector<std::uint8_t> bytes = img.to_png(9);          // to_png / to_jpg / to_bmp / to_tga -> file bytes

stb::image small = img.resize(320, 240);              // default resizer
stb::image crisp = img.resize(320, 240, "cubic");      // filter by name, or by enum:
stb::image soft  = img.resize(320, 240, stb::resize_filter::mitchell);

stb::resize_options o;                                 // full control: filter, edge, srgb, max_bytes
o.edge = stb::resize_edge::wrap;
stb::resizer r(o);                                     // also: resizer("cubic"), resizer(resize_filter::box)
stb::image tile = img.resize(320, 240, &r);            // nullptr = default resizer; r.resize(img, w, h) works too

stb::font f = stb::font::open("font.ttf");          // cheap to copy, thread-safe
stb::text_bitmap t = f.render("h\xC3\xA9llo", 32);      // UTF-8 in, 1-channel image out
stb::atlas atlas = f.make_atlas(U"abc", 32, 256, 256);
```

Design: stb never appears in a public header. Where a class would hold stb
state, it is hidden behind a pimpl: `font` keeps the font bytes and the
`stbtt_fontinfo` in an opaque `font::impl` (shared and immutable, so copies are
cheap and threads can share one). `image` and `resizer` carry no stb state at
all (plain ints/enums), so a pimpl there would only add indirection. `image` is
a value type (rule of zero). The four output formats are a closed set, so there
is no encoder class hierarchy: each format is a `to_*` / `write_*` pair on
`image` with its own defaulted settings, implemented in `src/core/encode.cpp`
(the one place `stb_image_write` is compiled). Runtime failures derive from
`stb::error` (`decode_error`, `encode_error`, `limit_error`, `io_error`);
programmer errors throw `std::invalid_argument`.

Or `cmake --install build` and `find_package(libstb)` as usual.

## Layout (mirrors httpp / ctoon)

```
include/stb.h               umbrella header, owns LIBSTB_VERSION_* (single source of truth)
include/stb/*.hpp           public API (error, image, resizer, font, utf8); never includes stb
src/core/*.cpp              implementation; the only place stb headers are compiled
src/third_party/stb/        vendored stb headers (committed, no submodules)
src/bindings/python/        nanobind module (bind_*.cpp, one per area) + the `libstb` Python package
cmake/                      DynamicVersion, Startup, Optimize, package config
tests/{cpp,python,cmake}    unit tests + find_package consumer test, all run by ctest
tests/data/                 synthetic test font (solid-rectangle glyphs) + the script that makes it
```

Each stb header is compiled with `STB_*_STATIC`, so no `stbi_*` symbol leaks
from `libstb_core`: it links fine next to your own copy of stb.

## Build & test

```sh
pip install -r requirements-dev.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Wheels: abi3 (`cp312-abi3`, covers 3.12+) plus one wheel each for cp39-cp311;
see `.github/workflows/wheels.yml`. Free-threaded builds are not supported.

## License

MIT for libstb. The vendored stb headers are MIT / public domain (see
`src/third_party/README.md`).
