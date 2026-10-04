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

Everything is byte-for-byte upstream EXCEPT stb/stb_image_write.h, which carries two
local patches (summarised in the comment at the top of the file, marked "libstb patch"
in the code):

- **PNG deflate:** `stbi_zlib_compress` was rewritten. Upstream only emits fixed-Huffman
  blocks from a small 3-byte hash table; this one does 4-byte hash-chain LZ77 with lazy
  matching and picks dynamic / fixed / stored per block, which brings PNGs to within
  about 1% of zlib level 6 in size at roughly twice its speed. The output is a plain zlib
  stream; the `STBIW_ZLIB_COMPRESS` hook and the function signature are unchanged.
- **JPEG:** the encoder is about 1.2x to 1.5x faster (benchmark suite, 1 MP and up) and its output is byte-for-byte upstream's.
  The entropy coder uses a buffered 64-bit bit writer (upstream called the sink once per
  output byte) and walks only the non-zero coefficients; the DCT and quantizer are written
  so compilers vectorize them; colour conversion and the 8x8 transposes use SSE2 on x86 (a
  plain C fallback runs everywhere else). `tests/python/test_jpeg_golden.py` holds SHA-256
  digests of upstream's output, so any later change that alters a byte fails a test.

Each header is included ONLY from src/core/*.cpp (with its STB_*_IMPLEMENTATION
defined there), never from a public include/libstb/*.hpp header. To update:
overwrite the file, bump the version above, rerun the tests (for stb_image_write.h,
reapply the two patches above first).
