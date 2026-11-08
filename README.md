<div align="center">

<img src="https://raw.githubusercontent.com/MohammadRaziei/libstb/master/docs/images/libstb-sq.svg" width="96" alt="libstb logo"/>

# libstb

**Shaped Typography & Bitmaps: decode, encode, resize, edit, draw text and compress, from Python and C++.**<br/>
No system libraries and no Python dependencies.

[![CI](https://github.com/MohammadRaziei/libstb/actions/workflows/cmake.yml/badge.svg)](https://github.com/MohammadRaziei/libstb/actions/workflows/cmake.yml)
[![Python](https://img.shields.io/badge/python-3.9%2B-blue.svg)](pyproject.toml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](CMakeLists.txt)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE.txt)

[Installation](#installation) ·
[Examples](#examples) ·
[Command line](#command-line) ·
[Configuration](#configuration) ·
[Lineage](#lineage) ·
[Guide](docs/guide.md) ·
[Changelog](CHANGELOG.md)

</div>

---

```python
import libstb as stb

img = stb.Image.open("photo.jpg")                  # PNG, JPEG, BMP, TGA, GIF, ... or bytes
img.resize(320, 240, "cubic").write("small.png")   # the format comes from the extension
```

> [!NOTE]
> **libstb stands for Shaped Typography & Bitmaps.** It descends from Sean Barrett's
> [stb](https://github.com/nothings/stb) libraries (the decoders, encoders, resizer and font
> rasterizer), and the letters of the name still read as *stb*. But the code has been refactored
> so heavily that it is no longer stb, which is why it has its own name instead of calling itself
> a version of it. No stb header is vendored: the code is reorganised into classes and one file
> per topic, parts are redesigned around a modern, typed API (explicit ownership, typed errors,
> no hidden copies), and several hot paths are faster. See [Lineage](#lineage).

## Highlights

- **Python and C++, one library.** The same operations in `stb::image` (C++17) and
  `libstb.Image` (nanobind). The Linux x86-64 wheel is under 1 MB.
- **Many formats in, four out.** Decodes PNG, JPEG, BMP, TGA, GIF and more; encodes PNG, JPEG,
  BMP and TGA.
- **Editing, not just I/O.** Crop, pad, flips, rotation, thumbnail, channel conversion, alpha
  blend modes, fill, masks, stacking, EXIF orientation, vector outlines (anti-aliased fill and
  stroke with caps, joins and dashes) and a canvas for text, shapes and outlines.
- **Text, including Persian and Arabic.** TrueType and CFF fonts, GSUB shaping, bidi reordering
  and pair kerning (marks are placed from GPOS anchors, see the [guide](docs/guide.md)), plus a
  text size estimate that needs no font file.
- **zlib as an API.** `zlib_compress` and `zlib_decompress`, with bounded output so a zip bomb
  is a `LimitError`.
- **Safe by default.** Size limits are checked from the header before pixel memory is allocated,
  errors are typed (`DecodeError`, `EncodeError`, `LimitError`), and EXIF parsing checks every
  offset.
- **Fast where it pays.** SIMD kernels (AVX2, SSE2, scalar) picked at run time with identical
  results, decoded buffers adopted instead of copied, the GIL released in Python calls, and big
  resizes split over threads (the pixels never depend on the thread count).
- **Logging that fits both worlds.** `stb::logging` in C++, modelled on Python's `logging`; in
  Python, libstb's messages arrive in the `logging` module you already configure.

## Installation

```sh
pip install libstb
```

| Command | Adds |
| --- | --- |
| `pip install "libstb[numpy]"` | `Image.numpy()`, `imread`, `imresize` (zero-copy views). |
| `pip install "libstb[pillow]"` | `Image.to_pil()` and `Image.from_pil()`. |

C++ projects use CMake (3.21+, C++17). Fetch the source:

```cmake
include(FetchContent)
FetchContent_Declare(libstb GIT_REPOSITORY https://github.com/MohammadRaziei/libstb.git GIT_TAG master)
FetchContent_MakeAvailable(libstb)

target_link_libraries(my_app PRIVATE libstb::libstb)
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
`cmake --install`. The wheel carries the C++ SDK as an ordinary install prefix,
`libstb/sdk/` (`include/`, `lib/`, `lib/cmake/libstb/`); `libstb --sdk-dir` prints it, so
`-DCMAKE_PREFIX_PATH=$(libstb --sdk-dir)` works too.

The namespace and headers are `stb` (`<stb.h>`, `<stb/*.hpp>`); the CMake target
(`libstb::libstb`) and the Python package (`libstb`) keep the project name. The core is a shared
library. No stb header is included from a public header, and stb symbols do not leak from the
library, so it links next to your own copy of stb.

## Examples

Every task is shown in Python and in C++. The C++ snippets are function bodies: they assume
`#include <stb.h>` (plus `<iostream>`, `<filesystem>` and `<fstream>` where used) and run next to
the files they name. All of them were built with CMake and run before they went into this README.

### Open, inspect and resize

Read the header without decoding, decode with the EXIF rotation applied, then resize and save.
The filter is a name (`nearest`, `linear`, `cubic`, `mitchell`, `box`, ...) or an enum.

**Python**

```python
import libstb as stb

info = stb.iminfo("photo.jpg")                      # header only: no pixel is decoded
print(info.width, info.height, info.format)

img = stb.Image.open("photo.jpg", orient=True)      # apply the EXIF rotation while decoding
img.resize(320, 240, "mitchell").write("small.png")           # the format comes from the extension
img.thumbnail(256, 256).write_jpg("thumb.jpg", quality=80)    # fits the box, never enlarges
```

**C++**

```cpp
const stb::image_info info = stb::image_info::read_file("photo.jpg");   // header only: no pixel is decoded
std::cout << info.width << "x" << info.height << "\n";

stb::load_options opt;
opt.orient = true;                                    // apply the EXIF rotation while decoding
stb::image img = stb::image::open("photo.jpg", opt);

img.resize(320, 240, "mitchell").write("small.png");  // the format comes from the extension
img.thumbnail(256, 256).write_jpg("thumb.jpg", 80);   // fits the box, never enlarges
```

### Thumbnails for a whole folder

`thumbnail` fits the image inside the box and never enlarges. Big resizes use extra threads on
their own, so a loop like this needs no thread code.

**Python**

```python
from pathlib import Path

import libstb as stb

Path("thumbs").mkdir(exist_ok=True)
for path in Path("photos").glob("*.jpg"):
    thumb = stb.Image.open(path, orient=True).thumbnail(256, 256)
    thumb.write(f"thumbs/{path.stem}.png")
```

**C++**

```cpp
namespace fs = std::filesystem;
fs::create_directories("thumbs");
for (const auto& entry : fs::directory_iterator("photos")) {
    if (entry.path().extension() != ".jpg") continue;
    stb::image thumb = stb::image::open(entry.path()).thumbnail(256, 256);
    thumb.write(fs::path("thumbs") / (entry.path().stem().string() + ".png"));
}
```

### Contact sheet and watermark

`hstack` and `vstack` put images side by side with a gap and a background; `blend` composites
one image over another with a blend mode (`normal`, `multiply`, `screen`, ...) and an opacity.
In Python every call returns a new `Image`; in C++ `blend` and `fill` change the image they are
called on (call `copy()` first to keep the original).

**Python**

```python
import libstb as stb

a = stb.Image.open("a.jpg").thumbnail(200, 200)
b = stb.Image.open("b.jpg").thumbnail(200, 200)
sheet = stb.Image.hstack([a, b], gap=8, background=(255, 255, 255), align="center")
sheet.write("sheet.png")

photo = stb.Image.open("photo.jpg")
logo = stb.Image.open("logo.png")                   # RGBA: the alpha channel is honoured
marked = photo.blend(logo, x=photo.width - logo.width - 16, y=16, opacity=0.6)
marked.write("marked.jpg")
```

**C++**

```cpp
stb::image a = stb::image::open("a.jpg").thumbnail(200, 200);
stb::image b = stb::image::open("b.jpg").thumbnail(200, 200);
stb::image::hstack({&a, &b}, 8, "white", stb::align::center).write("sheet.png");

stb::image photo = stb::image::open("photo.jpg");
stb::image logo = stb::image::open("logo.png");       // RGBA: the alpha channel is honoured
photo.blend(logo, photo.width() - logo.width() - 16, 16, stb::blend_mode::normal, 0.6f);  // changes photo
photo.write("marked.jpg");
```

### Text, labels and cards

`draw_text` puts one text on a copy of the image. A `Canvas` collects shapes, texts and images
and draws them on one copy, in order, when you call `finish()`. Load fonts you trust only.

**Python**

```python
import libstb as stb

photo = stb.Image.open("photo.jpg")
font = stb.Font.open("font.ttf")                    # trusted fonts only

# One text onto a copy of the image:
photo.draw_text("Hello", font, 32, 10, 10, color="white").write("hello.png")

# Several shapes and texts on ONE copy: a canvas draws them in order.
c = stb.Canvas(photo)
c.set_font(font, 48).set_color("white")
c.add_rect(10, 10, 360, 70).fill("#222").radius(12).stroke(2, "white")
c.add_text("Title", 24, 18)
c.add_text("subtitle", photo.width // 2, photo.height - 60).align("center").size(24).bg_color("#222").padding(6)
c.finish().write("card.png")
```

**C++**

```cpp
stb::image photo = stb::image::open("photo.jpg");
stb::font font = stb::font::open("font.ttf");         // trusted fonts only

// One text onto a copy of the image:
font.draw_text(photo, "Hello", 32, 10, 10, stb::color("white")).write("hello.png");

// Several shapes and texts on ONE copy: a canvas draws them in order.
stb::canvas c(photo);
c.set_font(font, 48).set_color("white");
c.add_rect(10, 10, 360, 70).fill("#222").radius(12).stroke(2, "white");
c.add_text("Title", 24, 18);
c.add_text("subtitle", photo.width() / 2, photo.height() - 60)
    .align(stb::text_align::center).size(24).bg_color("#222").padding(6);
c.finish().write("card.png");
```

### Persian and Arabic text

Write the text as you would type it. Shaping (joining forms, ligatures) and bidi reordering are
done for you, and mixed Persian and Latin text lays out in the right order.

**Python**

```python
import libstb as stb

font = stb.Font.open("Vazirmatn.ttf")               # any font that has Arabic-script glyphs
canvas = stb.Image.open("photo.jpg").fill("white")
text = "سلام دنیا، libstb 2026"                      # typed as is: shaping and bidi reordering are done for you
canvas.draw_text(text, font, 48, 20, 20, color="black").write("persian.png")
```

**C++**

```cpp
stb::font font = stb::font::open("Vazirmatn.ttf");    // any font that has Arabic-script glyphs
stb::image canvas = stb::image::open("photo.jpg").fill("white");
// UTF-8 as is: shaping and bidi reordering are done for you.
font.draw_text(canvas, "سلام دنیا، libstb 2026", 48, 20, 20, stb::color("black")).write("persian.png");
```

### Vector shapes

An outline is a list of lines and curves. Fill it (`non_zero` by default, or `even_odd`), or
stroke it with a width, caps, joins and dashes; the edges are anti-aliased and there is no extra
dependency. `fill_mask` gives the coverage as a gray image for your own compositing.

**Python**

```python
import libstb as stb

heart = stb.Outline().move_to(50, 90).cubic_to(0, 50, 30, 10, 50, 40).cubic_to(70, 10, 100, 50, 50, 90).close()
big = heart.transformed(stb.Matrix().translate(40, 40).scale(3, 3))    # scale, then move (the last call applies first)

img = stb.Image.open("photo.jpg")
art = img.fill_outline(big, "#e63946").stroke_outline(big, "white", 3, join="round", dashes=[8, 4])
art.write("heart.png")
heart.fill_mask(100, 100).write("heart_mask.png")    # the coverage as a gray Image, for your own compositing
```

**C++**

```cpp
stb::outline heart;
heart.move_to(50, 90).cubic_to(0, 50, 30, 10, 50, 40).cubic_to(70, 10, 100, 50, 50, 90).close();
const stb::outline big = heart.transformed(stb::matrix().translate(40, 40).scale(3, 3));   // scale, then move (the last call applies first)

stb::image img = stb::image::open("photo.jpg");
stb::fill(img, big, stb::color("#e63946"));           // anti-aliased, in place

stb::stroke_style style;
style.width = 3.f;
style.join = stb::line_join::round;
style.dashes = {8.f, 4.f};
stb::stroke(img, big, stb::color("white"), style);
img.write("heart.png");
```

### In memory, NumPy and Pillow

Encode to bytes and decode from bytes with no file. In Python the pixels are also a zero-copy
NumPy view, and Pillow images convert both ways.

**Python**

```python
import numpy as np
from PIL import Image as PILImage

import libstb as stb

pixels = stb.imread("photo.jpg")                    # uint8 ndarray (H, W, C)
small = stb.imresize(pixels, 160, 120)              # ndarray in, ndarray out
stb.imwrite("small.jpg", small, quality=85)

img = stb.Image.open("photo.jpg")
view = img.numpy()                                  # zero-copy view of the pixels
print(view.mean(axis=(0, 1)))
pil = img.to_pil()                                  # needs Pillow; L and RGBA are not copied
back = stb.Image.from_pil(pil.rotate(90, expand=True))
```

**C++**

```cpp
stb::image img = stb::image::open("photo.jpg");
std::vector<std::uint8_t> png = img.to_png(9);        // the bytes of a whole file, e.g. for a socket
stb::image back = stb::image::decode(png.data(), png.size());   // and back, from memory
std::cout << back.width() << "x" << back.height() << ", " << png.size() << " bytes\n";
```

### Untrusted input and zlib

Decoding refuses to allocate more than `max_bytes`, and that is decided from the header, before
any pixel memory exists. Errors are typed, so a bad upload is an exception you can name. The same
bound applies to `zlib_decompress`.

**Python**

```python
from pathlib import Path

import libstb as stb

data = Path("photo.jpg").read_bytes()               # say, an upload from a user
try:
    img = stb.Image.open(data, max_bytes=64 << 20)  # the size limit is checked from the header, before any pixel memory
except stb.LimitError:
    print("too big")
except stb.DecodeError:
    print("not an image libstb can read")

packed = stb.zlib_compress(data, level=9)
raw = stb.zlib_decompress(packed, max_bytes=64 << 20)   # a zip bomb is a LimitError
```

**C++**

```cpp
std::ifstream in("photo.jpg", std::ios::binary);      // say, an upload from a user
std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), {});

try {
    stb::load_options opt;
    opt.max_bytes = 64u << 20;                        // checked from the header, before any pixel memory
    stb::image img = stb::image::decode(data.data(), data.size(), opt);
} catch (const stb::limit_error&) {
    std::cerr << "too big\n";
} catch (const stb::decode_error&) {
    std::cerr << "not an image libstb can read\n";
}

std::vector<std::uint8_t> packed = stb::zlib_compress(data, 9);
std::vector<std::uint8_t> raw = stb::zlib_decompress(packed, 64u << 20);   // a zip bomb is a limit_error
```

### SIMD, threads and logging

You do not need to set any of this: the fastest SIMD backend is picked at run time and threads
are used only where the work pays for them. When you do want control (a library, a thread pool,
a diagnosis), a scope changes one thread only, and the previous setting comes back on exit.
libstb's messages go to Python's `logging` (the logger is `libstb`, with children such as
`libstb.simd`); in C++ you add handlers to `stb::logging::get_logger("libstb")`. libstb is silent
unless asked: at `debug` it says what it decided on each call (the backend, the threads, the sizes),
and at `warning` only when it changed something you asked for.

**Python**

```python
import logging

import libstb as stb

logging.basicConfig(level=logging.INFO)             # libstb's own messages arrive in Python's logging
logging.getLogger("libstb").setLevel(logging.DEBUG)

print(stb.simd_name(), stb.simd_backends(), stb.thread_count())
with stb.threads(1), stb.simd("scalar"):            # for this thread and this block only
    small = stb.Image.open("photo.jpg").resize(320, 240)
```

**C++**

```cpp
namespace lg = stb::logging;
lg::logger& lib = lg::get_logger("libstb");
lib.set_level(lg::level::debug);
lib.add_handler(lg::stream_handler(stderr)).set_formatter(lg::formatter("%(levelname)s %(name)s: %(message)s"));

std::cout << stb::simd_name() << " " << stb::thread_count() << "\n";
{
    stb::thread_scope threads(1);                     // for this thread and this scope only
    stb::simd_scope simd("scalar");
    stb::image small = stb::image::open("photo.jpg").resize(320, 240);
}
```

## Command line

`libstb` (or `python -m libstb`) comes with the Python package:

```sh
libstb --version
libstb info photo.jpg --json
libstb convert photo.jpg small.png --resize 320x           # a missing side keeps the aspect ratio
libstb convert in.png out.jpg --quality 85 --pixel-format gray --orient
libstb zlib compress data.bin data.bin.z -l 9
libstb --cmake-dir                                         # also --sdk-dir, --include-dir, --lib-dir
```

The output format of `convert` comes from the extension of the destination. Exit status is 0 on
success, 1 when a file could not be read or written, and 2 for a usage error.

## Configuration

Nothing here is needed for normal use. The variables are read once, the first time they are needed, and are meant for
diagnosis: comparing two runs of a program you do not want to edit.

| Variable | Values | Effect |
| --- | --- | --- |
| `LIBSTB_SIMD` | `auto`, `off`, or a backend name (`avx2`, `sse2`, `neon`, `scalar`) | Initial SIMD backend. An unknown name prints one line and falls back to `auto`. |
| `LIBSTB_THREADS` | `auto`, `off`, or a number | Most threads one call may use (a ceiling, not a demand). |
| `LIBSTB_LOG` | `debug`, `info`, `warning`, `error`, `critical` or a number | Level of the `libstb` logger. When it is set, it keeps deciding what libstb emits, whatever the Python level. |

## Lineage

libstb is not a wrapper around stb. Its image decoders, encoders, resizer and font reader are
**derived from stb's algorithms and rewritten as libstb's own code**; everything around them is
new. Each derived file says so in its header, and [`NOTICE.md`](NOTICE.md) has the lineage and
the license (stb is public domain or MIT; libstb uses the MIT option).

| Area | From | Lives in |
| :--- | :--- | :--- |
| Decoders: PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PIC, PNM, and the inflater | `stb_image` v2.30 | `src/codec/decode/` |
| Encoders: PNG, JPEG, BMP, TGA, and the zlib compressor | `stb_image_write` v1.16 (the JPEG encoder is based on jo_jpeg) | `src/codec/encode/` |
| Resizer, all filters | `stb_image_resize2` v2.18 | `src/image/resize/` |
| Font file, glyph outlines (TrueType and CFF), rasterizer, atlas packer | `stb_truetype` v1.26, `stb_rect_pack` v1.01 | `src/font/truetype/` |

Written for libstb, with no stb counterpart: the public API (`stb::image`, `stb::font`,
`stb::canvas`, typed errors, the whole Python package), editing and blending, the SIMD kernels,
EXIF, text shaping and bidi, vector outlines, logging, threads and the tooling. The full list is in
the [guide](docs/guide.md#what-is-from-stb-and-what-is-not).

What changed on the inherited parts:

- **Faster encoders.** The PNG deflate uses 4-byte hash chains, lazy matching and dynamic Huffman
  blocks; the JPEG encoder has a buffered bit writer, a vectorisable DCT and an SSE2 colour
  conversion. The compressor measured 1.2x to 1.9x faster than zlib level 6 with 0.4% to 3.3%
  larger output on the project's own samples (source code, a binary, image pixels). Measure on
  your data.
- **Inflate is not faster.** The decompressor is the stb_image-derived inflater: about 2.5x
  slower than zlib on very compressible text, so use zlib if inflate speed is what you need.
- **Safer.** Header-based size limits, bounds-checked reads in the font reader (stb_truetype did
  not check), and typed errors. The font reader has not been fuzzed: prefer fonts you trust.

Performance numbers against Pillow and OpenCV, including where libstb is slower (JPEG encoding
is one), are in [`benchmarks/`](benchmarks/README.md). They depend on the machine, so run them
on yours.

## Documentation

- [`docs/guide.md`](docs/guide.md): the full reference (every call, SIMD, threads, EXIF, DLPack,
  Pillow, errors, logging, design and source layout).
- `docs/`: the Sphinx (Python) and Doxygen (C++) sources of the documentation site.
- [`examples/`](examples): runnable Python scripts, C++ programs and notebooks. They run on the pictures and fonts of
  [`data/`](data) (photographs, an RGBA picture, a Persian and Latin font; licenses in `data/README.md`), so
  `python examples/python/05_font_render.py` and the C++ programs work without arguments.
- [`docs/adr/`](docs/adr): why logging works the way it does (three short decision records).
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
