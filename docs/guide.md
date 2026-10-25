# libstb guide

The complete reference for libstb: every Python and C++ call with its rules, encoding and
editing, channels and alpha, SIMD, EXIF orientation, Pillow and DLPack interop, resize
filters, errors, design notes, source layout, and build and test. The short version is in the
[README](../README.md).

## Python

```python
import numpy as np
import libstb as stb

img = stb.Image.open("photo.png")                   # or bytes; also: channels=4, flip=True
img.width, img.height, img.channels                 # 640, 480, 3
img.numpy()                                         # uint8 pixels (H, W, C), no copy; np.asarray(img) works too
np.from_dlpack(img)                                 # DLPack: also torch/jax/cupy.from_dlpack(img), zero-copy
stb.Image.from_dlpack(tensor)                       # and back: a torch/jax/cupy/numpy uint8 array -> Image
pixels = stb.imread("photo.png")                    # just the ndarray (same options as Image.open)
stb.imwrite("out.jpg", pixels, quality=80)          # an ndarray or an Image; format from the extension
small = stb.imresize(pixels, 320, 240)              # resize an ndarray, get an ndarray (no Image needed)
stb.ImageInfo.read("photo.png")                     # header only: (width, height, channels)
stb.Image(np.zeros((8, 8, 4), np.uint8))            # from your own uint8 array (no copy, see below)
img.copy()                                          # an independent image (deep copy)

img.write("out.jpg")                                 # format from the extension, default settings
img.write_jpg("out.jpg", quality=80)                # or pick the format and its settings
data = img.to_png(compression=9)                    # -> bytes (to_png / to_jpg / to_bmp / to_tga)

small = img.resize(320, 240)                        # new Image; default Resizer()
small = img.resize(320, 240, "cubic")               # filter by name (see the table below)
stb.Resizer("cubic")                                # same name works on Resizer itself
stb.Resizer(stb.Resizer.Filter.MITCHELL, stb.Resizer.Edge.WRAP, srgb=False)
img.resize(320, 240, stb.Resizer("linear", srgb=False))  # name + other options

font = stb.Font.open("font.ttf")                    # trusted fonts only, see below (right-to-left text is shaped for you)
text = font.render("Hello\nworld", 32)              # RenderedText(bitmap: Image (1 channel), origin_x, origin_y)
text.bitmap.write("hello.png")
# several texts and shapes on one copy of the image (draw_text copies it every call, a canvas once); each item can override the defaults:
c = stb.Canvas(img)                                 # same methods as C++ stb::canvas, same pixels
c.set_font(font, 48).set_color("white")             # defaults for the texts added next
c.add_rect(10, 10, 220, 60).fill("#222").radius(12).stroke(2, "white")   # shapes: add_rect, add_ellipse, add_line
c.add_text("Title", 20, 20)
c.add_text("Sub", 20, 90).color("gray").bg_color("#222").padding(8)   # per text: also .font() .size() .align()
out = c.finish()                                    # the texts are drawn here, in order
centred = img.draw_text("Hello\nworld", font, 32, img.width // 2, 10, align="center")  # x = centre of every line ("left" is the default; "right" too)
labeled = img.draw_text("Hello", font, 32, 10, 10, color=(255, 255, 255))   # new Image with the text drawn on; color: int, (r,g,b), (r,g,b,a) or "red" / "#ff8800"
font.metrics(32), font.advance("A", 32), font.kerning("A", "V", 32), font.measure("Hello", 32)
font.render_glyph("A", 32)                          # Glyph(bitmap, x_offset, y_offset, advance)
atlas = font.make_atlas("ABCabc123", 32, 256, 256)  # Atlas: .image and atlas["A"] -> AtlasGlyph
stb.estimate_text("Hello", 16, family="sans")        # TextSize, no font file needed (sans / serif / mono tables)

stb.iminfo("photo.png")                             # shortcut: ImageInfo.read(...)
```

**Encoding.** Every format has a pair of methods with its own settings, all
defaulted: `to_png(compression=8)`, `to_jpg(quality=90)`, `to_bmp()`,
`to_tga(rle=True)` return the file's bytes, and `write_png(path, ...)`,
`write_jpg(path, ...)`, `write_bmp(path)`, `write_tga(path, ...)` do the same
and write it (only after encoding succeeded, so a failure never leaves a
truncated file). `write(path)` is the shortcut: it picks the format from the
extension (`.png .jpg .jpeg .bmp .tga`, case-insensitive; `ValueError` for
anything else) and uses the defaults. For other settings call `write_*`.

`stb.imwrite(path, image, *, quality=None, compression=None, rle=None)` is
the functional form: it takes an `Image` or a uint8 array, picks the format
from the extension, and passes the options that format has (`quality` for jpg,
`compression` for png, `rle` for tga). An option the format does not have is a
`ValueError`, not silently ignored. `stb.imread(source, *, channels, flip,
max_bytes, orient)` is its counterpart and returns an ndarray.

**Editing.** Every operation returns a new `Image` and leaves the source
alone; `ValueError` on a bad argument, `LimitError` if a result would exceed
2 GiB. Results are exact, checked against Pillow's (`FLIP_*`, `ROTATE_*`,
`TRANSPOSE`, `expand`, `convert`).

```python
img.crop(x, y, width, height)         # must lie inside the image
img.flip_horizontal(); img.flip_vertical()
img.rotate90(turns=1)                 # counter-clockwise quarter turns (like np.rot90), negative = clockwise
img.transpose()                       # swap rows and columns
img.pad(left, top, right, bottom, fill=(255, 255, 255))   # fill: an int, or one value per channel
img.thumbnail(256, 256)               # fit inside the box, aspect ratio kept, never enlarges
img.thumbnail(256, 256, "linear")     # same resizer argument as resize()
```

**Channels and alpha.** `img.convert(channels)` goes between 1 (gray), 2 (gray +
alpha), 3 (RGB) and 4 (RGBA). Gray is 0.299 R + 0.587 G + 0.114 B on the stored
values (no gamma, as in Pillow), a missing alpha is 255, and a dropped alpha is
simply discarded: use `img.flatten(background=(255, 255, 255))` to blend it onto
a colour first. `img.split()` gives one single-channel `Image` per channel and
`Image.merge(channels)` puts 1 to 4 of them back (in any order).
`base.composite(overlay, x=0, y=0)` blends `overlay` on top with the usual "over"
operator and straight alpha: the overlay may hang over the edges, may have any
channel count (no alpha means opaque), and the result keeps the base's channel
count. Its alpha equals Pillow's `alpha_composite`; the colour is within one level
wherever the pixel is visible (where alpha is almost 0 the colour is ill-defined,
and it does not matter). `Image.open(..., channels=n)` converts inside stb, which
can differ from `convert(n)` by a rounding step.

**SIMD.** The loops that are limited by arithmetic rather than by memory
(`convert`, `composite`, `flatten`) exist in up to three builds that give
bit-identical results and differ only in speed. `stb.simd_backends()` lists
the ones this CPU can run, best first, e.g. `['avx2', 'sse2', 'scalar']`:

- `"avx2"`: AVX2 kernels, picked at run time when the CPU and OS support them
  (x86-64, GCC or Clang builds). The same wheel therefore runs on any x86-64 CPU.
- `"sse2"` (x86-64) or `"neon"` (arm64): the same loops vectorised by the compiler for
  the platform's baseline SIMD (on x86-64, hand-written SSE2 kernels for the gray
  conversions and RGBA over RGB). This is what arm64, MSVC and older x86 CPUs run.
- `"scalar"`: the same loops with auto-vectorisation off, the plain reference. Always
  available; the tests check every other backend against it.

`stb.simd_name()` is the backend in use on the calling thread (the first by
default). To change it from code:

- `with stb.simd("scalar"):` uses a backend for the calling thread only, for the
  length of the block; other threads are not affected and blocks nest. In C++ the
  same is `stb::simd_scope guard("scalar");`. Prefer this in a library or a thread pool.
- `stb.set_simd("sse2")` switches the whole process (`"auto"` restores the
  default).

Both raise `ValueError` (C++: `std::invalid_argument` / `false`) for an unknown or
unavailable name. The environment variable `LIBSTB_SIMD=<name>` chooses the initial
backend (`off` means `scalar`) and is for diagnosis, such as comparing two runs of a
program you do not want to edit; a name that is unknown or unavailable on the CPU
prints one line on stderr and falls back to `auto`. None of this is something you need
to set: the default is the fastest backend. How much each helps depends on the loop. On
a 4 MP image (one core of a shared machine: expect other numbers on yours) `convert` to
gray from RGB or RGBA is about 1.9x to 3x faster than scalar with SSE2 and about the
same with AVX2, RGBA `composite` onto RGB is about 2x faster with either, and RGBA
`composite` onto RGBA is about 2.4x faster with SSE2 and 5x with AVX2. The "sse2" backend
runs hand-written SSE2 kernels for the gray conversions and for RGBA over RGB (where the
compiler's vectoriser struggles with 3-byte pixels), and the compiler-vectorised loops for
the other pairs. Flips, turns, `crop` and `pad` only move bytes and are limited by memory bandwidth, so they have no SIMD
variants: AVX2 would not make them faster.

**EXIF orientation.** Phone photos are stored sideways and carry an EXIF tag
saying how to turn them; stb ignores it. `Image.open(path, orient=True)` (and
`imread(..., orient=True)`) applies it while decoding, for JPEG and PNG (`eXIf`
chunk); width and height may swap, and `flip=True` is applied after it. It is
off by default so decoding keeps returning the stored pixels, and `iminfo` keeps
reporting the stored size. `stb.exif_orientation(source)` returns the 1..8
value (1 when there is none or the data is unreadable; it never throws on bad
input and is safe on untrusted files) and `img.orient(value)` applies one:

```python
img = stb.Image.open("IMG_0001.jpg", orient=True)     # upright
turn = stb.exif_orientation("IMG_0001.jpg")               # or ask first...
img = stb.Image.open("IMG_0001.jpg").orient(turn)         # ...and apply it yourself
```

Only the tag is read (JPEG `APP1`, PNG `eXIf`); other EXIF data and ICC profiles
are not.

**Pillow.** Pillow is not a dependency: it is imported only when you call something that
needs it, like numpy (`pip install "libstb[pillow]"`; `import libstb` never loads it).

```python
pil = img.to_pil()                 # libstb -> Pillow: L, LA, RGB or RGBA by channel count (a copy)
img = stb.Image.from_pil(pil)      # Pillow -> libstb
```

`from_pil` takes `L`, `LA`, `RGB` and `RGBA` as they are and converts the modes that hold
8-bit colour in another form: `1` to `L`, `P` to `RGB` (`RGBA` if the palette has
transparency), `PA` and `RGBa` to `RGBA`, `La` to `LA`, `RGBX`, `CMYK` and `YCbCr` to `RGB`.
Modes with no 8-bit colour (`I`, `I;16`, `F`, `LAB`, `HSV`) raise `TypeError`: convert them
yourself.

Both directions also work without those methods, because Pillow has neither DLPack nor a
buffer export and speaks NumPy's `__array_interface__`, which `Image` implements both ways:
`PILImage.fromarray(img)` and `stb.Image(pil)`. That path is strict on purpose: it takes
only 8-bit `L`, `LA`, `RGB` and `RGBA` and raises `TypeError` otherwise (a palette or CMYK
image would otherwise be read as if its indices or inks were colours, as `np.asarray(pil)`
does). `Image(x)` accepts any object with `__array_interface__` (a writable contiguous
address is shared and kept alive, like a NumPy array; a read-only or strided one, or data
given as bytes, is copied and bounds-checked; an address cannot be checked, so only hand over
objects you trust). The interface reports a gray image as `(H, W)`, which Pillow requires;
`np.asarray(img)` and `img.numpy()` still give `(H, W, 1)`.

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

**Fonts: prefer trusted files.** libstb's font reader (`src/font/truetype/`) checks every read against
the file's bounds and caps composite-glyph recursion, so a damaged font gives wrong glyphs rather than
out-of-range reads, but unlike `stb_image` it has not been fuzzed. Load your own or vetted system fonts
rather than user uploads. (Images: `stb_image` is fuzzed, and libstb adds dimension and size limits.)

**Right-to-left text (Persian, Arabic, Urdu, Hebrew).** `draw_text`, `measure`, `render` and the
canvas shape and reorder it for you: pass the text in the order you type it, no pre-processing with
`arabic-reshaper` or `python-bidi`. The line is ordered with the Unicode bidirectional algorithm
(UAX #9: the paragraph direction is that of the first strong letter, Latin words and numbers inside
Persian text keep their order, brackets are mirrored), ZWNJ and ZWJ are honoured, each `\n` line is
shaped on its own, and text with no right-to-left letter is drawn exactly as before.

Letter shapes come from the font. If it has a GSUB table for Arabic script (every modern Persian
and Arabic font does), libstb runs it the way an OpenType shaper does: the `isol`, `init`, `medi`,
`fina` shapes, `ccmp`, `rlig` (lam-alef), `calt` and `liga` (so "الله" is the one glyph the font
draws for it). Lookup types 1 to 7 are read (single, multiple, alternate, ligature, context,
chaining context, extension), with lookup flags and GDEF. A font without GSUB but with the Arabic
Presentation Forms in its `cmap` is shaped through those, and a form the font lacks falls back to the
plain letter. On Vazirmatn, Noto Naskh Arabic and Noto Sans Arabic the glyphs are identical to
HarfBuzz's on 18,000 random strings (`Font.shape_glyphs` returns them, for diagnostics). Speed is
about 0.2 microseconds per character, and the GSUB table is parsed the first time right-to-left text
is drawn with a font (about 0.1 ms), not when the font is opened.

What is not done: GPOS. Kerning comes from the `kern` table only, and marks (harakat) are placed by a
simple rule rather than the font's anchors: centred on the ink of their letter and stacked above it
(below for kasra and similar signs, shadda first), with no room of their own. That looks like the
real thing on ordinary vowelled text but not pixel for pixel, and text set in a font that depends on
GPOS for its shapes (some Nastaliq fonts) will look wrong. GSUB lookups of type 8 (reverse chaining)
are skipped, only the default language system of the script is used (no `locl` per language), and
`font.advance` and `font.kerning` work on one code point and do no shaping. Not checked: Hebrew,
Urdu-specific letters, text with explicit bidi controls (embeddings and isolates are ignored). A font
is untrusted input: damaged GSUB data cannot read outside the file, and the work and the growth of
the glyph run are limited in proportion to the text (50,000 damaged fonts were shaped under
AddressSanitizer without a crash).

Errors mirror the C++ hierarchy: `stb.Error` (a `RuntimeError`) with
`DecodeError`, `EncodeError`, `LimitError` below it, so a single
`except stb.Error` catches every libstb failure. Bad arguments raise
`ValueError`, unreadable files `OSError` (`FileNotFoundError`, `PermissionError`,
... as the OS says). Images whose decoded size would
exceed `max_bytes` (default 512 MiB) are rejected from the header, before any
pixel memory is allocated.

`stb.Image` is `stb::image` itself, not a wrapper: `open`, `resize`, `to_*` and
`write*` are the C++ members, run without the GIL. `img.numpy()` / `np.asarray(img)`
are numpy views of its pixels (kept alive by the view); `memoryview(img)` is the
same zero-copy view as a plain 3-D `memoryview` (numpy reads it too), and
`img.tobytes()` is a copy, both without numpy.

**DLPack.** `Image` implements `__dlpack__` / `__dlpack_device__`, so
`np.from_dlpack(img)`, `torch.from_dlpack(img)`, `jax.numpy.from_dlpack(img)`
and `cupy.from_dlpack(img)` share the pixels with no copy and no framework
import on libstb's side. The layout is fixed: `uint8`, shape `(height, width,
channels)`, on the CPU (`dl_device` other than the CPU is a `BufferError`).
The view keeps the image alive. It is writable with numpy 2.1 or newer (older
numpy, the last one on Python 3.9 being 2.0.2, imports every DLPack view
read-only and has no `copy=` argument; `img.numpy()` is always writable).
Asking for `copy=True` gives an independent copy. The other way round, `stb.Image.from_dlpack(x)` takes any
DLPack object holding `uint8` pixels of shape `(H, W)` or `(H, W, 1..4)` (a
torch, jax, cupy or numpy array, another `Image`): a writable C-contiguous CPU
array is shared, anything else is copied, and `copy=True` always copies.
(`Image(x)` accepts the same objects; `from_dlpack` additionally insists on
`__dlpack__`, so a stray `bytes` is a clear `TypeError`.) For a different layout or dtype (CHW, float32), convert on
the framework side, e.g. `torch.from_dlpack(img).permute(2, 0, 1).float() / 255`.

Pixels are never copied behind your back. `Image(array)` uses a writable,
C-contiguous uint8 array in place, so the image and the array share their
pixels (changes show on both sides, and the image keeps the array alive). Any
other array (read-only, or strided like `arr[::-1]`) cannot be shared and is
copied. `Image.open` takes ownership of stb's own buffer, no copy either. When
you want an independent image, say so: `img.copy()` (also `copy.copy` and
`copy.deepcopy`).

## C++ / CMake

```cmake
# after `pip install libstb`
execute_process(COMMAND ${Python3_EXECUTABLE} -c "import libstb as stb; print(stb.get_cmake_dir())"
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
stb::image dup = img.copy();                              // image is move-only: copies are explicit
img.write("out.jpg");                                      // format from the extension, default settings
img.write_jpg("out.jpg", 80);                             // or pick the format and its settings
std::vector<std::uint8_t> bytes = img.to_png(9);          // to_png / to_jpg / to_bmp / to_tga -> file bytes

stb::image small = img.resize(320, 240);              // default resizer
stb::image crisp = img.resize(320, 240, "cubic");      // filter by name, or by enum:
stb::image soft  = img.resize(320, 240, stb::resize_filter::mitchell);

stb::resize_options o;                                 // full control: filter, edge, srgb, max_bytes
o.edge = stb::resize_edge::wrap;
stb::resizer r(o);                                     // also: resizer("cubic"), resizer(resize_filter::box)
stb::image tile = img.resize(320, 240, &r);            // nullptr = default resizer; r.resize(img, w, h) works too

stb::image part  = img.crop(10, 10, 200, 100);        // geometry: each returns a new image
stb::image turned = img.rotate90();                    // counter-clockwise; also flip_horizontal/vertical, transpose
stb::image framed = img.pad(8, 8, 8, 8, {255, 255, 255, 255});   // fill[c] = value of channel c
stb::image thumb = img.thumbnail(256, 256);            // fits the box, never enlarges (+ resize's filter arguments)
stb::image gray = img.convert(1);                      // 1..4 channels; flatten() blends alpha onto a colour
std::vector<stb::image> planes = img.split();          // stb::image::merge(planes) is the inverse
stb::image over = img.composite(logo, 16, 16);         // "over" operator; logo may hang over the edges

stb::load_options lo;                                  // EXIF orientation: JPEG APP1 / PNG eXIf
lo.orient = true;                                      //   upright while decoding (then `flip` goes after it)
stb::image photo = stb::image::open("IMG_0001.jpg", lo);
int turn = stb::exif_orientation(bytes.data(), bytes.size());   // 1..8, 1 if none; never throws
stb::image upright = raw.orient(turn);                 // or apply it yourself

stb::font f = stb::font::open("font.ttf");          // cheap to copy, thread-safe
stb::text_bitmap t = f.render("h\xC3\xA9llo", 32);      // UTF-8 in, 1-channel image out
stb::image labeled = f.draw_text(img, "Hello", 32, 10, 10, {255, 255, 255});  // text onto a copy; colours: {r, g, b[, a]}, "red", "#ff8800", "red"_c, 0xff8800_rgb
stb::image centred = f.draw_text(img, "Hello", 32, img.width() / 2, 10, stb::color(), stb::text_align::center);
stb::canvas c(img);                                      // <stb/draw.hpp>: one copy, many texts
c.set_font(f, 48).set_color("white");                    // defaults for the texts added next
c.add_rect(10, 10, 220, 60).fill("#222").radius(12).stroke(2, "white");   // shapes: add_rect, add_ellipse, add_line
c.add_text("Title", 20, 20);
c.add_text("Sub", 20, 90).color("gray").bg_color("#222").padding(8);               // per text: also .font() .size() .align()
stb::image out = c.finish();                             // the texts are drawn here, in order
stb::atlas atlas = f.make_atlas(U"abc", 32, 256, 256);
stb::text_size est = stb::estimate_text("Hello", 16);    // no font file: fixed sans / serif / mono tables
```

Design: stb never appears in a public header. Where a class would hold stb
state, it is hidden behind a pimpl: `font` keeps the font bytes and the
`stbtt_fontinfo` in an opaque `font::impl` (shared and immutable, so copies are
cheap and threads can share one). `image` and `resizer` carry no stb state at
all (plain ints/enums), so a pimpl there would only add indirection. `image` is
move-only: its pixels are a `shared_ptr<uint8_t>` whose deleter (or aliased
owner) decides who frees them, so the same class holds memory it allocated,
stb's decode buffer (adopted, not copied) or memory somebody else owns
(`image::wrap`, which is how a numpy array becomes an image without a copy).
A copy is always spelled `copy()`. The four output formats are a closed set, so there
is no encoder class hierarchy: each format is a `to_*` / `write_*` pair on
`image` with its own defaulted settings; the format code itself is in `src/codec/` and knows nothing
about `image`.
The editing operations are plain loops over the pixel buffer in `src/image/ops.cpp`
(no stb involved; the arithmetic-heavy kernels are `src/image/kernels.hpp`, instantiated
three times - AVX2, baseline SIMD, and `kernels_scalar.cpp` with auto-vectorisation off -
and the backend is chosen in `src/image/simd.cpp`; build with `-DSTB_NO_SIMD_DISPATCH` to leave
the AVX2 ones out) and the EXIF reader is `src/image/exif.cpp`: it looks only at the
Orientation tag and checks every offset against the buffer.
Runtime failures derive from
`stb::error` (`decode_error`, `encode_error`, `limit_error`, `io_error`);
programmer errors throw `std::invalid_argument`.

Or `cmake --install build` and `find_package(libstb)` as usual.

## Layout (mirrors httpp / ctoon)

```
include/stb.h               umbrella header, owns LIBSTB_VERSION_* (single source of truth)
include/stb/*.hpp           public API (error, util, color, image, font, shaping, draw, zlib); never includes stb
src/util/                   file I/O helpers, UTF-8
src/image/                  image, editing ops, resize, EXIF, SIMD kernels and their dispatch
src/codec/                  every format: decode (stb_image), PNG / JPEG / BMP / TGA encoders, deflate
src/zlib/                   the public zlib API (on top of src/codec)
src/font/                   fonts, text rendering, atlases; truetype/ (font file, CFF, rasterizer, packer) and
                            shaper/ (arabic, bidi, gsub, otl)
src/draw/                   shapes and colours
src/third_party/stb/        two vendored stb headers (stb_image, stb_image_resize2), byte-for-byte upstream
src/bindings/python/        nanobind module (bind_*.cpp, one per area) + the `libstb` Python package
cmake/                      DynamicVersion, Startup, Optimize, package config
tests/{cpp,python,cmake}    unit tests + find_package consumer test, all run by ctest
tests/tools/                dev checks that are not tests (snapshot.py, jpeg_vs_upstream.py, zlib_fuzz.py)
tests/data/                 synthetic test font (solid-rectangle glyphs) + the script that makes it
```

Each stb header is compiled with `STB_*_STATIC`, so no `stbi_*` symbol leaks
from `stb_core`: it links fine next to your own copy of stb.

## Build & test

```sh
pip install -r requirements-dev.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/python/test_memory.py` repeats each operation on a real image and fails if memory
allocated by it (native allocations included) is still alive afterwards, and checks that
`Image(array)`, `from_dlpack`, `crop` and `convert` do not make hidden copies. The limits
are on by default whenever `pytest-memray` can be imported (it is in the `test` extra on the platforms that have a wheel for it; `tests/python/conftest.py` switches
`--memray` on; `LIBSTB_NO_MEMRAY=1` turns it off), and the module is skipped when the plugin is
missing, as on Windows.

Wheels: abi3 (`cp312-abi3`, covers 3.12+) plus one wheel each for cp39-cp311;
see `.github/workflows/wheels.yml`. Free-threaded builds are not supported.

## License

MIT for libstb. The vendored stb headers are MIT / public domain (see
`src/third_party/README.md`).
