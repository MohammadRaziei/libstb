# third_party

Vendored dependencies: committed single-header files, never a system package
or a submodule. Both are byte-for-byte upstream, so updating one is "overwrite the file".

| path | upstream | version | license |
| :--- | :--- | :--- | :--- |
| stb/stb_image.h | github.com/nothings/stb | v2.30 | MIT / public domain |
| stb/stb_image_resize2.h | github.com/nothings/stb | v2.18 | MIT / public domain |

Each header is included ONLY from one source file (with its STB_*_IMPLEMENTATION defined there,
and STB_*_STATIC so no symbol leaks out of the library), never from a public include/stb/*.hpp
header:

| header | compiled in |
| :--- | :--- |
| stb_image.h | src/codec/decode.cpp |
| stb_image_resize2.h | src/image/resize.cpp |

Two more stb headers were vendored until the font reader was rewritten as libstb's own code,
with identical output (checked on 150 real fonts, TrueType and CFF, against the stb build):
`stb_truetype.h` became src/font/truetype/ (face.cpp, cff.cpp, raster.cpp, atlas.cpp) and
`stb_rect_pack.h` its skyline packer (atlas.cpp).

stb_image_write.h is NOT vendored any more: its encoders were rewritten as libstb's own code in
src/codec/ (png.cpp, jpeg.cpp, bmp_tga.cpp and the zlib compressor deflate.cpp), with the same
output bytes:

- **PNG deflate:** upstream only emits fixed-Huffman blocks from a small 3-byte hash table; ours
  does 4-byte hash-chain LZ77 with lazy matching and picks dynamic / fixed / stored per block,
  which brings PNGs to within about 1% of zlib level 6 in size at roughly twice its speed.
  `tests/tools/zlib_fuzz.py` fuzzes it against Python's zlib.
- **JPEG:** about 1.2x to 1.5x faster than upstream (benchmark suite, 1 MP and up). The entropy
  coder uses a buffered 64-bit bit writer (upstream called the sink once per output byte) and
  walks only the non-zero coefficients; the DCT and quantizer are written so compilers vectorize
  them; colour conversion and the 8x8 transposes use SSE2 on x86 (a plain C++ fallback runs
  elsewhere). Built with the same compiler and flags it writes the same bytes as upstream
  stb_image_write v1.16, and `tests/tools/jpeg_vs_upstream.py` checks that after any change. The
  bytes are not a contract across platforms: a compiler that fuses multiply-add (Apple clang on
  ARM64, x86 `-mfma`) flips rare rounding ties in upstream's own encoder too, so the test suite
  checks JPEG quality with tolerances and never compares bytes.
