# libstb

Python bindings for the [stb](https://github.com/nothings/stb) single-header
libraries. `pip install libstb` and you have image/font processing with **no
system dependencies and no Python dependencies**. The C++ library is usable from
CMake too.

numpy is optional: `pip install "libstb[numpy]"` if you want `img.numpy()` /
`libstb.imread()`. It is imported only when one of those is first
used, never by `import libstb`, and without it they raise an `ImportError` that
says so. Everything else works without numpy, including `Image(array)` from any
buffer (a `memoryview`, `array.array`, a `bytearray`, a PIL-exported buffer...),
and `img.tobytes()` and `memoryview(img)` give you the pixels with no numpy at all.

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
np.from_dlpack(img)                                 # DLPack: also torch/jax/cupy.from_dlpack(img), zero-copy
libstb.Image.from_dlpack(tensor)                    # and back: a torch/jax/cupy/numpy uint8 array -> Image
pixels = libstb.imread("photo.png")                 # just the ndarray (same options as Image.open)
libstb.imwrite("out.jpg", pixels, quality=80)       # an ndarray or an Image; format from the extension
libstb.ImageInfo.read("photo.png")                  # header only: (width, height, channels)
libstb.Image(np.zeros((8, 8, 4), np.uint8))         # from your own uint8 array (no copy, see below)
img.copy()                                          # an independent image (deep copy)

img.write("out.jpg")                                 # format from the extension, default settings
img.write_jpg("out.jpg", quality=80)                # or pick the format and its settings
data = img.to_png(compression=9)                    # -> bytes (to_png / to_jpg / to_bmp / to_tga)

small = img.resize(320, 240)                        # new Image; default Resizer()
small = img.resize(320, 240, "cubic")               # filter by name (see the table below)
libstb.Resizer("cubic")                             # same name works on Resizer itself
libstb.Resizer(libstb.Resizer.Filter.MITCHELL, libstb.Resizer.Edge.WRAP, srgb=False)
img.resize(320, 240, libstb.Resizer("linear", srgb=False))  # name + other options

font = libstb.Font.open("font.ttf")                 # trusted fonts only, see below
text = font.render("Hello\nworld", 32)              # RenderedText(bitmap: Image (1 channel), origin_x, origin_y)
text.bitmap.write("hello.png")
font.metrics(32), font.advance("A", 32), font.kerning("A", "V", 32), font.measure("Hello", 32)
font.render_glyph("A", 32)                          # Glyph(bitmap, x_offset, y_offset, advance)
atlas = font.make_atlas("ABCabc123", 32, 256, 256)  # Atlas: .image and atlas["A"] -> AtlasGlyph

libstb.iminfo("photo.png")                          # shortcut: ImageInfo.read(...)
```

**Encoding.** Every format has a pair of methods with its own settings, all
defaulted: `to_png(compression=8)`, `to_jpg(quality=90)`, `to_bmp()`,
`to_tga(rle=True)` return the file's bytes, and `write_png(path, ...)`,
`write_jpg(path, ...)`, `write_bmp(path)`, `write_tga(path, ...)` do the same
and write it (only after encoding succeeded, so a failure never leaves a
truncated file). `write(path)` is the shortcut: it picks the format from the
extension (`.png .jpg .jpeg .bmp .tga`, case-insensitive; `ValueError` for
anything else) and uses the defaults. For other settings call `write_*`.

`libstb.imwrite(path, image, *, quality=None, compression=None, rle=None)` is
the functional form: it takes an `Image` or a uint8 array, picks the format
from the extension, and passes the options that format has (`quality` for jpg,
`compression` for png, `rle` for tga). An option the format does not have is a
`ValueError`, not silently ignored. `libstb.imread(source, *, channels, flip,
max_bytes, orient)` is its counterpart and returns an ndarray.

**Editing.** Every operation returns a new `Image` and leaves the source
alone; `ValueError` on a bad argument, `LimitError` if a result would exceed
2 GiB. Results are exact, checked against Pillow's (`FLIP_*`, `ROTATE_*`,
`TRANSPOSE`, `expand`, `convert`).

```python
img.crop(x, y, width, height)         # must lie inside the image
img.flip_horizontal(); img.flip_vertical()
img.rotate90(turns=1)                 # clockwise quarter turns, negative = counter-clockwise
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
bit-identical results and differ only in speed. `libstb.simd_backends()` lists
the ones this CPU can run, best first, e.g. `['avx2', 'sse2', 'scalar']`:

- `"avx2"`: AVX2 kernels, picked at run time when the CPU and OS support them
  (x86-64, GCC or Clang builds). The same wheel therefore runs on any x86-64 CPU.
- `"sse2"` (x86-64) or `"neon"` (arm64): the same loops vectorised by the compiler for
  the platform's baseline SIMD. This is what arm64, MSVC and older x86 CPUs run.
- `"scalar"`: the same loops with auto-vectorisation off, the plain reference. Always
  available; the tests check every other backend against it.

`libstb.simd_name()` is the backend in use (the first by default);
`libstb.set_simd("sse2")` switches (`"auto"` restores the default; `ValueError` for
an unknown or unavailable name); `LIBSTB_SIMD=<name>` in the environment chooses
the initial one (`off` means `scalar`). It is for comparing and measuring, not
something you need to set. How much each helps depends on the loop: on 16 MP,
AVX2 makes `convert` to gray about 1.8x faster and RGBA `composite` about 2.6x
faster than scalar, while SSE2 only helps the RGBA `composite` (about 1.9x) and is
no faster than scalar for `convert` or for `composite` onto RGB (measured on one
core of a shared machine: expect other numbers on yours). Flips, turns, `crop` and
`pad` only move bytes and are limited by memory bandwidth, so they have no SIMD
variants: AVX2 would not make them faster.

**EXIF orientation.** Phone photos are stored sideways and carry an EXIF tag
saying how to turn them; stb ignores it. `Image.open(path, orient=True)` (and
`imread(..., orient=True)`) applies it while decoding, for JPEG and PNG (`eXIf`
chunk); width and height may swap, and `flip=True` is applied after it. It is
off by default so decoding keeps returning the stored pixels, and `iminfo` keeps
reporting the stored size. `libstb.exif_orientation(source)` returns the 1..8
value (1 when there is none or the data is unreadable; it never throws on bad
input and is safe on untrusted files) and `img.orient(value)` applies one:

```python
img = libstb.Image.open("IMG_0001.jpg", orient=True)     # upright
turn = libstb.exif_orientation("IMG_0001.jpg")            # or ask first...
img = libstb.Image.open("IMG_0001.jpg").orient(turn)      # ...and apply it yourself
```

Only the tag is read (JPEG `APP1`, PNG `eXIf`); other EXIF data and ICC profiles
are not.

**Pillow.** Pillow is not a dependency: it is imported only when you call something that
needs it, like numpy (`pip install "libstb[pillow]"`; `import libstb` never loads it).

```python
pil = img.to_pil()                 # libstb -> Pillow: L, LA, RGB or RGBA by channel count (a copy)
img = libstb.Image.from_pil(pil)   # Pillow -> libstb
```

`from_pil` takes `L`, `LA`, `RGB` and `RGBA` as they are and converts the modes that hold
8-bit colour in another form: `1` to `L`, `P` to `RGB` (`RGBA` if the palette has
transparency), `PA` and `RGBa` to `RGBA`, `La` to `LA`, `RGBX`, `CMYK` and `YCbCr` to `RGB`.
Modes with no 8-bit colour (`I`, `I;16`, `F`, `LAB`, `HSV`) raise `TypeError`: convert them
yourself.

Both directions also work without those methods, because Pillow has neither DLPack nor a
buffer export and speaks NumPy's `__array_interface__`, which `Image` implements both ways:
`PILImage.fromarray(img)` and `libstb.Image(pil)`. That path is strict on purpose: it takes
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

**Fonts: trusted files only.** stb_truetype does no bounds checking; its
author writes "NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT
FILES". Load your own or vetted system fonts, never user uploads. (Images are
different: `stb_image` is fuzzed, and libstb adds dimension and size limits.)

Errors mirror the C++ hierarchy: `libstb.Error` (a `RuntimeError`) with
`DecodeError`, `EncodeError`, `LimitError` below it, so a single
`except libstb.Error` catches every libstb failure. Bad arguments raise
`ValueError`, unreadable files `OSError` (`FileNotFoundError`, `PermissionError`,
... as the OS says). Images whose decoded size would
exceed `max_bytes` (default 512 MiB) are rejected from the header, before any
pixel memory is allocated.

`libstb.Image` is `stb::image` itself, not a wrapper: `open`, `resize`, `to_*` and
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
Asking for `copy=True` gives an independent copy. The other way round, `libstb.Image.from_dlpack(x)` takes any
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
stb::image turned = img.rotate90();                    // clockwise; also flip_horizontal/vertical, transpose
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
stb::atlas atlas = f.make_atlas(U"abc", 32, 256, 256);
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
`image` with its own defaulted settings, next to decoding in `src/core/image.cpp`.
The editing operations are plain loops over the pixel buffer in `src/core/image_ops.cpp`
(no stb involved; the arithmetic-heavy kernels are `src/core/image_kernels.hpp`, instantiated
three times - AVX2, baseline SIMD, and `image_kernels_scalar.cpp` with auto-vectorisation off -
and the backend is chosen in `src/core/simd.cpp`; build with `-DSTB_NO_SIMD_DISPATCH` to leave
the AVX2 ones out) and the EXIF reader is `src/core/exif.cpp`: it looks only at the
Orientation tag and checks every offset against the buffer.
Runtime failures derive from
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
