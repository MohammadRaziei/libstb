# libstb C++ API {#mainpage}

**libstb** is a C++17 library over the [stb](https://github.com/nothings/stb)
single-header libraries: image decoding and encoding (PNG, JPEG, BMP, TGA),
resizing with selectable filters, image editing (crop, flip, rotate, pad, convert,
composite, EXIF orientation) and TrueType font rendering. The stb headers stay
private: the public API is `stb::image`, `stb::resizer`, `stb::font` and friends.

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

## SIMD backends

The pixel kernels pick a backend at run time (`stb::simd_name()`,
`stb::set_simd()`, per thread with `stb::simd_scope`). All backends produce
identical output.
