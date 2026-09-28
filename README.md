# libstb

Python bindings for the [stb](https://github.com/nothings/stb) single-header
libraries. `pip install libstb` and you have image/font processing with **no
system dependencies**. The C++ library is usable from CMake too.

| module | status |
| :--- | :--- |
| `stb_image` (read) | done |
| `stb_image_write` (write) | planned |
| `stb_image_resize2` (resize) | planned |
| `stb_truetype` (fonts) | planned |

## Python

```python
import libstb

img = libstb.load("photo.png")               # uint8 ndarray, (H, W, C)
rgba = libstb.load("photo.png", channels=4)  # force 4 channels
libstb.info("photo.png")                     # ImageInfo(width, height, channels), header only
libstb.load(data_bytes, flip=True)           # from memory, flipped vertically
```

Errors: `ValueError` (bad arguments), `RuntimeError` (undecodable or oversized
image), the usual `OSError` family for files. Images whose decoded size would
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

```cpp
#include <libstb.h>
libstb::image img = libstb::load(bytes.data(), bytes.size());  // throws on error
```

Or `cmake --install build` and `find_package(libstb)` as usual.

## Layout (mirrors httpp / ctoon)

```
include/libstb.h            umbrella header, owns LIBSTB_VERSION_* (single source of truth)
include/libstb/*.hpp        public API; never includes stb
src/core/*.cpp              implementation; the only place stb headers are compiled
src/third_party/stb/        vendored stb headers (committed, no submodules)
src/bindings/python/        nanobind module + the `libstb` Python package
cmake/                      DynamicVersion, Startup, Optimize, package config
tests/{cpp,python,cmake}    unit tests + find_package consumer test, all run by ctest
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
