# third_party

Vendored dependencies: committed single-header files, never a system package
or a submodule.

| path | upstream | version | license |
| :--- | :--- | :--- | :--- |
| stb/stb_image.h | github.com/nothings/stb | v2.30 | MIT / public domain |
| stb/stb_image_write.h | github.com/nothings/stb | v1.16 | MIT / public domain |
| stb/stb_image_resize2.h | github.com/nothings/stb | v2.18 | MIT / public domain |
| stb/stb_truetype.h | github.com/nothings/stb | v1.26 | MIT / public domain |
| stb/stb_rect_pack.h | github.com/nothings/stb | v1.01 | MIT / public domain |

Each header is included ONLY from src/core/*.cpp (with its STB_*_IMPLEMENTATION
defined there), never from a public include/libstb/*.hpp header. To update:
overwrite the file, bump the version above, rerun the tests.
