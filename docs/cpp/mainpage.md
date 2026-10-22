# libstb C++ API {#mainpage}

**libstb** is a C++17 library over the [stb](https://github.com/nothings/stb)
single-header libraries: image decoding and encoding (PNG, JPEG, BMP, TGA),
resizing with selectable filters, image editing (crop, flip, rotate, pad, convert,
composite, EXIF orientation), TrueType font rendering and drawing text onto images (`font::draw_text`, or `canvas` for many texts and shapes on one copy) (plus a text size estimator
that needs no font file) and zlib compression and decompression with an output
limit. The stb headers stay private: the public API is `stb::image`,
`stb::resizer`, `stb::font`, `stb::zlib_compress`, `stb::zlib_decompress` and
friends.

## Installation

```cmake
# CMakeLists.txt
include(FetchContent)
FetchContent_Declare(
  libstb
  GIT_REPOSITORY https://github.com/MohammadRaziei/libstb.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(libstb)

target_link_libraries(my_app PRIVATE libstb::core)
```

Requires CMake 3.21+ and a C++17 compiler. The namespace and the headers are
`stb` (`<stb.h>`, `<stb/*.hpp>`); the CMake target (`libstb::core`) keeps the
project name. After a `cmake --install`, `find_package(libstb CONFIG REQUIRED)`
works the same way.

## Quick example

```cpp
#include <stb.h>

int main() {
    stb::image img = stb::image::open("in.png");      // throws stb::error subclasses
    stb::image small = img.resize(320, 240, "cubic"); // filter by name
    stb::image gray  = small.convert(1);              // 1..4 channels
    gray.write("out.jpg");                            // format from the extension
}
```

`stb::image` is move-only: copies are explicit (`img.copy()`). Every editing
operation returns a new image. Runtime failures derive from `stb::error`
(`decode_error`, `encode_error`, `limit_error`, `io_error`); programmer errors
throw `std::invalid_argument`.

## Right-to-left text

`stb::font` (`draw_text`, `measure`, `render`) and `stb::canvas` shape and reorder Persian, Arabic, Urdu and
Hebrew text: pass it in typing order. Letter shapes, lam-alef and other ligatures (`ccmp`, `isol/init/medi/fina`,
`rlig`, `calt`, `liga`) come from the font's GSUB table (lookup types 1 to 7); a font without GSUB but with the
Arabic Presentation Forms in its cmap is shaped through those. ZWNJ and ZWJ are honoured and each line is
ordered with UAX #9. There is no GPOS: kerning is the `kern` table only, and marks (harakat) are placed by a
rule (centred on the letter, stacked above or below its ink). `font::shape_glyphs` returns the glyph ids
that were chosen. The code-point-level steps are public in `<stb/shaping.hpp>` (`stb::shape_for_display`,
`stb::has_rtl`).

## SIMD backends

The pixel kernels pick a backend at run time (`stb::simd_name()`,
`stb::set_simd()`, per thread with `stb::simd_scope`). All backends produce
identical output.
