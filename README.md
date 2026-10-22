# libstb

Fast image, font and zlib processing for **Python and C++**, with no system dependencies and no
Python dependencies: decode, encode, resize, edit, draw text and compress with one small library.

libstb is highly powered by [stb](https://github.com/nothings/stb), and that is where the name
comes from. It is not a thin wrapper, though. The API is our own (typed, with explicit
ownership and errors), and several parts are rewritten or patched for speed, so both the API
and the performance differ from plain stb. See [what is from stb](#what-is-from-stb-and-what-is-not).

## Python

```sh
pip install libstb            # numpy and Pillow are optional: "libstb[numpy]", "libstb[pillow]"
```

```python
import libstb as stb

img = stb.Image.open("photo.jpg")                 # PNG, JPEG, BMP, TGA, GIF, ... (or bytes)
img.width, img.height, img.channels               # 640, 480, 3

small = img.resize(320, 240, "cubic")             # filter by name: nearest, linear, cubic, mitchell, box...
small.write("small.png")                          # format from the extension (png, jpg, bmp, tga)
small.write_jpg("small.jpg", quality=80)          # or choose the format and its settings
data = small.to_png(compression=9)                # -> bytes

thumb = img.thumbnail(256, 256)                   # fits the box, never enlarges
img.crop(10, 10, 200, 100).rotate90().convert(1)  # every edit returns a new Image

pixels = img.numpy()                              # zero-copy view (needs numpy); DLPack works too
photo = stb.Image.open("IMG_0001.jpg", orient=True)   # apply the EXIF rotation while decoding

font = stb.Font.open("font.ttf")                  # trusted fonts only
labeled = img.draw_text("Hello", font, 32, 10, 10, color=(255, 255, 255))   # text onto a copy
centred = img.draw_text("Hello", font, 32, img.width // 2, 10, align="center")  # x is the centre of the line
font.render("Hello", 32).bitmap.write("hello.png")
# Persian / Arabic text works as typed: the font's GSUB shapes and ligates it, the line is reordered (no GPOS, see docs/guide.md)
stb.estimate_text("Hello", 16)                    # text size with no font file at all

packed = stb.zlib_compress(data)                  # zlib streams: the compressor that writes PNG
data = stb.zlib_decompress(packed, max_bytes=64 << 20)   # output is bounded: a zip bomb is a LimitError
```

## C++ with CMake

```cmake
include(FetchContent)
FetchContent_Declare(libstb GIT_REPOSITORY https://github.com/MohammadRaziei/libstb.git GIT_TAG master)
FetchContent_MakeAvailable(libstb)

target_link_libraries(my_app PRIVATE libstb::core)    # CMake 3.21+, C++17
```

`find_package(libstb CONFIG)` works too, after `cmake --install` or after `pip install libstb`
(`stb.get_cmake_dir()` gives the config directory).

```cpp
#include <stb.h>

stb::image img = stb::image::open("photo.jpg");          // throws stb::error subclasses
stb::image small = img.resize(320, 240, "cubic");        // or stb::resize_filter::mitchell
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
(`libstb::core`) and the Python package (`libstb`) keep the project name. No stb header is
ever included from a public header, and stb symbols do not leak from the library, so it links
next to your own copy of stb.

## What is from stb, and what is not

libstb uses these stb modules, vendored in `src/third_party/` (no submodules):

| stb module | used for |
| :--- | :--- |
| `stb_image` | decoding (PNG, JPEG, BMP, TGA, GIF, ...) |
| `stb_image_write` | encoding PNG, JPEG, BMP, TGA |
| `stb_image_resize2` | resizing, all filters |
| `stb_truetype` | font metrics, glyphs, text rendering, atlases |
| `stb_rect_pack` | packing glyphs into an atlas |

What we changed on top, for speed and for a safer, more convenient API:

- **Faster encoders.** `stb_image_write.h` carries two local patches: a new PNG deflate (4-byte
  hash chains, lazy matching, dynamic Huffman blocks: within about 1% of zlib level 6 in size at
  roughly twice its speed) and a faster JPEG encoder (buffered bit writer, vectorisable DCT, SSE2 colour
  conversion). The other stb headers are byte-for-byte upstream.
- **SIMD where it pays.** `convert`, `composite` and `flatten` have AVX2, baseline SIMD and
  scalar builds that give identical results; the best one is picked at run time.
- **No hidden copies.** Decoded buffers are adopted, not copied; arrays and DLPack tensors are
  shared when they can be; Python calls release the GIL.
- **zlib as an API.** The PNG compressor and stb's inflate are exposed as
  `zlib_compress` and `zlib_decompress` (any bytes-like object in, bytes out), with a bounded
  output so a zip bomb is a `LimitError`. They interoperate with python's `zlib`. Compression
  was 1.2x to 1.9x faster than zlib level 6 at 0.4% to 3.3% larger output on our samples;
  decompression is stb_image's and is **not** faster than zlib (about 2.5x slower on very
  compressible text), so use zlib if inflate speed is what you need.
- **Editing stb does not have.** Crop, pad, flips, rotation, thumbnail, channel conversion,
  alpha compositing, EXIF orientation, resize filters by name, and a font-free text size
  estimate.
- **Safety.** Size limits are checked from the header before any pixel memory is allocated,
  errors are typed (`stb.Error` with `DecodeError`, `EncodeError`, `LimitError`), and EXIF
  parsing checks every offset. stb_truetype is still not safe on untrusted fonts: load only
  fonts you trust.

Performance numbers against Pillow and OpenCV, including where libstb is slower (JPEG
encoding is one), are in [`benchmarks/`](benchmarks/README.md). They depend on the machine, so
run them on yours.

## More

- [`docs/guide.md`](docs/guide.md): the full reference (every call, SIMD, EXIF, DLPack, Pillow,
  errors, design and source layout).
- `docs/`: the Sphinx (Python) and Doxygen (C++) sources of the documentation site.
- [`CHANGELOG.md`](CHANGELOG.md).

## Build and test

```sh
pip install -r requirements-dev.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `STB_BUILD_TESTS`, `STB_BUILD_PYTHON`, `STB_BUILD_DOCS`. Targets: `stb_core`,
`stb_test`, `stb_docs`, `stb_benchmark`.

## License

MIT for libstb. The vendored stb headers are MIT / public domain (see
`src/third_party/README.md`).
