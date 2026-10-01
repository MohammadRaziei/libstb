# third_party

Vendored dependencies: committed source files, never a system package or a
submodule. Everything here is compiled into libstb itself, so there is nothing
to install and nothing to link.

| path | upstream | version | license |
| :--- | :--- | :--- | :--- |
| stb/stb_image.h | github.com/nothings/stb | v2.30 | MIT / public domain |
| stb/stb_image_write.h | github.com/nothings/stb | v1.16 | MIT / public domain |
| stb/stb_image_resize2.h | github.com/nothings/stb | v2.18 | MIT / public domain |
| stb/stb_truetype.h | github.com/nothings/stb | v1.26 | MIT / public domain |
| stb/stb_rect_pack.h | github.com/nothings/stb | v1.01 | MIT / public domain |
| libdeflate/ | github.com/ebiggers/libdeflate | v1.26 | MIT |

Each header is included ONLY from src/core/*.cpp (with its STB_*_IMPLEMENTATION
defined there), never from a public include/libstb/*.hpp header. To update:
overwrite the file, bump the version above, rerun the tests.

## libdeflate

Only the compression half, zlib format, is vendored (`lib/deflate_compress.c`,
`zlib_compress.c`, `adler32.c`, `utils.c`, the matchfinders, and the x86/arm CPU
feature detection: about 380 KB, no decompression, no gzip, no CRC). It replaces
stb_image_write's own deflate through stb's official `STBIW_ZLIB_COMPRESS` hook
(see `src/core/image.cpp`): about 3x faster PNG encoding and files about 40%
smaller at the same level. It is plain C99 with its own runtime CPU dispatch, so
a wheel built for generic x86-64 or arm64 still uses AVX2/NEON where present.
Not exported from the shared library (`LIBSTB_VENDORED`, see below).

Changes from upstream, all marked "libstb patch":
- `lib/lib_common.h`: when `LIBSTB_VENDORED` is defined the `libdeflate_*`
  symbols are not exported.

To update: replace the files from a new tag, keep the patch above, rerun the tests.

## Local patches to stb

`stb/stb_image_write.h` is v1.16 with one change, the JPEG encoder only
(`stbiw__jpg_dct_cols`, `stbiw__jpg_calcBits` and one block in
`stbiw__jpg_processDU`, each marked "libstb patch"): the DCT, the quantization
and the bit count were rewritten so compilers vectorize them. The arithmetic is
the same operations in the same order, and the bytes written are identical to
upstream's (checked on 11 image sizes/qualities/channel counts). Together with
`-fno-trapping-math` (set in the top-level CMakeLists.txt) JPEG encoding is about
2x faster. When updating stb_image_write.h, reapply these three spots, or drop
them and accept the slower encoder.
