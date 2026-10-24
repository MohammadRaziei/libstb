# Changelog

All notable changes to libstb are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Dates are commit dates.

This file was generated from the git history (61 commits, tags `v0.3.0`, `v0.4.0`, `v0.5.0`, `v0.6.0`, `v0.7.0`); 0.8.0 was written by hand from the 26 commits after `v0.7.0`.

## [Unreleased]

### Changed
- Source layout: `src/core/` is gone. One folder per module: `src/util`, `src/image`, `src/codec`,
  `src/zlib`, `src/font` (with `shaper/`), `src/draw`. Internal headers are included relative to
  `src/` (`"image/simd.hpp"`).
- Public headers merged by cohesion: `stb/resizer.hpp` is part of `stb/image.hpp` (which removes the
  opaque forward declaration between the two), `stb/shape.hpp` and `stb/canvas.hpp` became
  `stb/draw.hpp`, `stb/env.hpp` and `stb/utf8.hpp` became `stb/util.hpp`. `stb.h` still includes
  everything.
- The encoders and the zlib compressor are libstb's own code in `src/codec/` (`png.cpp`,
  `jpeg.cpp`, `bmp_tga.cpp`, `deflate.cpp`), written as classes with no global state (the old
  mutex around `stb_image_write`'s globals is gone). `stb_image_write.h` is no longer vendored: the
  four remaining stb headers are byte-for-byte upstream. Output bytes are unchanged (PNG at every
  level, JPEG, BMP, TGA, zlib), and the JPEG encoder still writes the same bytes as upstream
  `stb_image_write` v1.16 built with the same compiler. BMP and TGA encoding is faster.
- `bidi::resolve` is a class with one method per UAX #9 rule, `text_shaper::shape_line` and
  `gsub_table::make_plan` are split into steps, the three `thumbnail` overloads and the two
  `Resizer` constructors share their code, constants are snake_case.

### Added
- Round-trip tests (`tests/python/test_roundtrip.py`, `tests/cpp/test_codec.cpp`) and two
  development checks that are not tests: `tests/tools/snapshot.py` (byte-for-byte comparison of
  two builds) and an updated `tests/tools/jpeg_vs_upstream.py` / `zlib_fuzz.py`.

## [0.8.0] - 2026-10-22

### Added
- Drawing text (`4ab53e3`, `15ad30c`): `stb::font::draw_text` and `Image.draw_text` write multi-line
  UTF-8 text onto a 1 to 4 channel image with alpha blending. `text_align` (`left`, `center`,
  `right`) aligns every line to the x you give.
- `stb::color` (`<stb/color.hpp>`, `ededbef`): colours from names, `#hex`, `{r, g, b[, a]}` and
  string literals, and `color::from_value(0xRRGGBB)`. Used by `draw_text` and the canvas.
- Canvas (`stb::canvas`, `Image.canvas`, `1fc491a`, `4201852`, `bcc2e39`): collect texts and
  shapes, drawn together by `finish()`. `add_text` returns an item with its own `.font()`,
  `.size()`, `.color()`, `.align()`, `.bg_color()` and `.padding()`. `add_rect` (with `.radius()`),
  `add_ellipse` and `add_line` return a shape with `.fill()`, `.no_fill()` and `.stroke()`, with
  smooth edges (`6243481`). The API is the same in C++ and Python, and golden-hash tests keep
  their output byte-identical.
- Right-to-left text (Persian, Arabic, Urdu, Hebrew) in `draw_text`, `measure`, `render` and the
  canvas: pass the text as typed, with no `arabic-reshaper` or `python-bidi`. The line is
  ordered with the Unicode bidirectional algorithm (UAX #9, without explicit embeddings and
  isolates), ZWNJ and ZWJ are honoured and brackets are mirrored.
  - Letter shapes and ligatures come from the font's GSUB table for Arabic script: `isol`,
    `init`, `medi`, `fina`, `ccmp`, `rlig` (lam-alef), `calt` and `liga` (so "الله" is the one
    glyph the font draws for it). Lookup types 1 to 7 are read (single, multiple, alternate,
    ligature, context, chaining context in all three formats, extension), with lookup flags and
    GDEF. A font without GSUB but with the Arabic Presentation Forms in its `cmap` is shaped
    through those, and a form it lacks falls back to the plain letter (`cea22ba`, `79fce16`).
  - Marks (harakat) sit over or under their letter by a simple rule, with no room of their own
    (`7c301be`).
  - Checked against HarfBuzz: identical glyph ids on 3,000 random strings, with and without
    marks, on each of Vazirmatn, Noto Naskh Arabic and Noto Sans Arabic. About 0.2 microseconds
    per character, 2.4 times faster than Pillow with raqm for the layout of a 438-character
    paragraph. The GSUB table is parsed the first time right-to-left text is drawn with a font
    (about 0.1 ms); opening a font is as fast as before.
  - The font is untrusted input. Every read is range-checked, the growth of the glyph run and
    the work of the lookups are limited in proportion to the text, and 50,000 damaged fonts
    shaped under AddressSanitizer and UBSan without a crash.
  - Not done: GPOS (kerning is the `kern` table only, marks are not placed by the font's
    anchors, so fonts that depend on GPOS for their shapes, some Nastaliq fonts, look wrong),
    GSUB lookup type 8, `locl` per language, and the Syriac features. Not checked: Hebrew,
    Urdu-specific letters, OpenType-CFF (`.otf`) fonts and B Nazanin.
- `stb::font::shape_glyphs` / `Font.shape_glyphs(text)`: the glyph indices shaping chose, in
  visual order, for diagnostics and tests.
- `<stb/shaping.hpp>`: `stb::shape_for_display`, `stb::has_rtl`, `stb::is_arabic_mark` and
  `stb::is_arabic_mark_below`, the code-point level steps behind the above.
- Tests: white-box tests of the GSUB engine with hand-made tables (every lookup type and
  format, a stage mask, truncated and damaged tables), HarfBuzz reference glyph ids in C++ and
  Python, a live HarfBuzz comparison in Python when `uharfbuzz` is installed (now in the `test`
  extra), a Persian golden hash shared by C++ and Python, and two Vazirmatn subsets under the
  SIL OFL 1.1 in `tests/data` (`make_persian_font.py` rebuilds them).

### Changed
- `Image.canvas` and `Image.draw_text` are nanobind methods, and a font argument of the wrong
  type raises `TypeError` (`fc3aaff`, `402153d`).
- `stb::color` converts implicitly from string literals and brace lists.
- Benchmark report and docs: Chart.js and KaTeX load from cdnjs, brand colours, download buttons
  for the benchmark data, and zero Doxygen and Sphinx warnings (`641561c`, `054ea1f`, `ee3af4f`).

### Fixed
- Windows: a test variable named `small` collided with an `rpcndr.h` macro, and the canvas box
  arithmetic is 64-bit with parenthesized `min`/`max` (`3145a8a`).
- Windows (MSVC): the C++ tests are built with `/utf-8`, so Persian and Arabic text written as
  `"\u0633"` in a narrow string literal stays UTF-8 instead of becoming `?` in the ANSI code page,
  and the shaping tests parenthesize `std::min`/`std::max` against the `windows.h` macros.

## [0.7.0] - 2026-10-17

### Breaking
- CMake names changed (`016551c`, `c76d406`, `3b02769`). Targets `libstb_*` are now `stb_*`:
  `stb_core` (the library; still exported as `libstb::core`), `stb_test`, `stb_test_cpp`,
  `stb_test_python`, `stb_test_cmake`, `stb_docs` (`_cpp`, `_python`, `_index`), `stb_coverage`
  (`_cpp`, `_python`, `_total`), `stb_benchmark` and `stb_bench_*`; the C++ test executable
  `test_libstb_cpp` is now `test_stb_cpp`. The options `LIBSTB_BUILD_TESTS`, `LIBSTB_BUILD_PYTHON`
  and `LIBSTB_BUILD_DOCS` are now `STB_BUILD_TESTS`, `STB_BUILD_PYTHON` and `STB_BUILD_DOCS`.
  Scripts and CI that call the old names must be updated. Not renamed: the package and project
  name `libstb`, `find_package(libstb CONFIG)`, `libstb::core`, `import libstb`, the extension
  module `libstb_py`, and environment variables such as `LIBSTB_SIMD`.

### Added
- `stb::zlib_compress` / `stb::zlib_decompress` (`<stb/zlib.hpp>`; Python: `libstb.zlib_compress`,
  `libstb.zlib_decompress`, `DEFAULT_ZLIB_LEVEL`, `DEFAULT_ZLIB_MAX_BYTES`) (`0088cb8`): zlib streams
  with the PNG compressor and stb_image's inflate. `level` uses zlib's numbering (0 stores, 1 to 9,
  -1 default 6). The output of `zlib_decompress` is bounded by `max_bytes` (a zip bomb is a
  `limit_error` / `LimitError`, never an out-of-memory), the Adler-32 is verified, and both
  directions interoperate with zlib. Compression was 1.2x to 1.9x faster than zlib level 6 with
  0.4% to 3.3% larger output on our samples; decompression is **not** faster than zlib (about
  2.5x slower on very compressible text). Any bytes-like object is accepted in Python and the GIL
  is released.
- `stb::estimate_text` / `stb::estimate_metrics` (Python: `libstb.estimate_text`,
  `libstb.estimate_metrics`): rough text size without a font file, from fixed width tables for
  `sans`, `serif` and `mono` (Liberation metrics, generated by `tools/gen_font_metrics.py`, no font
  bundled). No kerning; CJK and emoji count as 1 em and other non-Latin text is approximated.
- Tests: a real PNG decompression bomb (a valid 0.5 MB file that declares 576 MB), generated on
  demand by `tests/python/pngbomb.py`, checked from Python (`test_png_bomb.py`, with a memray bound)
  and C++ (`test_a_real_png_bomb_is_refused_from_its_header`) (`83226ea`); zlib tests against
  python's `zlib` in both directions, zip bombs and exact `max_bytes` bounds; font estimator
  tests, including agreement with the real Liberation fonts when they are installed.
- `docs/guide.md`: the complete reference that used to be the README (`ec879bb`).
- Documentation pages for the zlib and estimator APIs (Sphinx and Doxygen).

### Changed
- README rewritten as a short landing page: Python first, then C++ with CMake, then which stb
  modules are used and how libstb differs from plain stb in API and performance (`ec879bb`).
- Internal: stb's headers are still compiled in one translation unit (`src/core/image.cpp`); the
  zlib API reaches them through `src/core/zlib_bridge.hpp`, so the compressor and inflater exist
  once. The Python bindings share one `py_buffer` helper (`binding.hpp`).

### Fixed
- The C++ install snippets (`docs/cpp/index-install.txt`, `docs/cpp/mainpage.md`) used
  `GIT_TAG main`, but the default branch is `master`.

## [0.6.0] - 2026-10-16

### Breaking
- `Image.rotate90(turns=1)` (C++: `stb::image::rotate90`) now turns **counter-clockwise**, like
  `numpy.rot90` and Pillow. A negative `turns` turns clockwise. Before, positive meant clockwise.
  `orient()` (EXIF) is unchanged.

### Added
- Image editing API on `Image` and `stb::image`: `crop`, `flip_horizontal`, `flip_vertical`,
  `rotate90`, `transpose`, `orient`, `pad`, `thumbnail`, `convert`, `split`, `Image.merge`,
  `composite`, `flatten`. Every operation returns a new image.
- EXIF orientation support (`exif_orientation`, `orient`).
- Channel and alpha handling (gray, RGB, RGBA conversion and compositing).
- `libstb.imresize(array, width, height, resizer=None)`: ndarray in, ndarray out.
- Runtime SIMD backend selection: `simd_backends()`, `simd_name()`, `set_simd()`, the `LIBSTB_SIMD`
  environment variable (an unknown or unavailable name prints one line on stderr and falls back to
  auto), and a per-thread override: `with libstb.simd("scalar"):` (C++: `stb::simd_scope`).
- Hand-written SSE2 kernels (RGB/RGBA to gray, RGBA over RGB), bit identical to the scalar backend.
- `stb::env()` in `include/stb/env.hpp`: cross-platform, copy-returning environment variable reader
  (uses `_dupenv_s` on MSVC).
- `Image.to_pil()` / `Image.from_pil()`. Pillow is loaded only when these are called
  (`pip install "libstb[pillow]"`). Only 8-bit L/RGBA modes are supported, others raise `TypeError`.
- Examples 08 to 11 (geometry, channels and alpha, EXIF orientation, Pillow) and the notebook
  `04_edit_and_orient`.
- Tests: edit, EXIF, channels, geometry, SIMD, array interface, functional, Pillow conversion,
  garbage collection (`test_gc.py`) and allocation checks with `pytest-memray` (`test_memory.py`).
- `tests/python/test_torch.py`: DLPack in both directions with real torch (CPU). torch is not a
  test dependency: the module is skipped when it cannot be imported.
- Documentation site: `-DLIBSTB_BUILD_DOCS=ON`, then `cmake --build build --target libstb_docs` builds the
  C++ reference (Doxygen), the Python reference (Sphinx) and a landing page into `build/docs/out`.
- Coverage: `cmake --build build --target libstb_coverage` builds an lcov report for the C++ core, a
  coverage.py report for the Python layer, a merged total and a dashboard in `build/coverage` (needs
  `lcov` and `coverage`). The C++ tests compile the core sources into the test executable, so
  coverage is just a few flags on that target; the shipped `libstb::core` and the Python extension are
  unchanged.
- CI: QEMU user-mode job (`emulate`) that checks the fallback to portable SIMD backends on a CPU
  without AVX2 (Nehalem) and the use of AVX2 kernels when available (Haswell).
- CI: native ARM64 runners (Linux and Windows) in the wheel matrix.
- CI: `build-docs` and `build-coverage` jobs in `cmake.yml` (coverage is also sent to Codecov), and a
  `deploy-pages` job in the orchestrator that publishes the docs with the coverage dashboard under
  `/coverage`. The coverage job installs the CPU build of torch so `test_torch.py` runs there.

### Changed
- The scalar backend is compiled without compiler auto-vectorization, so it is really scalar.
- Wheel matrix runs with `fail-fast: false`.
- Python examples, notebooks, tests, benchmarks, README and docs import the package as
  `import libstb as stb` and write `stb.Image`, `stb.imread`, ... (the package name is still `libstb`).
- Python 3.12+ builds use the stable ABI: `wheel.py-api = "cp312"` is set in `pyproject.toml`, giving
  one `cp312-abi3` wheel for 3.12 and newer.
- `pytest-memray` is only required on x86_64 and arm64, and not on Windows. `numpy` is not required
  for the test extra on Windows ARM64 with Python < 3.11; the test modules use
  `pytest.importorskip` for numpy and Pillow.
- Benchmarks: new `imresize` and editing runners (`crop`, `rotate90`, `flip_horizontal`,
  `convert_gray`, `composite`).
- README: image manipulation, EXIF, SIMD diagnosis, memory checks and GC support are documented.

### Fixed
- `nanobind: leaked 1 instances!` at interpreter exit. An `Image` that views a Python object holds a
  reference the GC cannot see, which could form an uncollectable cycle. `Image` now implements
  `tp_traverse` / `tp_clear` and the keep-alive deleter reports the object it holds
  (`image::owner_deleter<D>()` in `image.hpp`).
- Windows build: a test variable named `small` collided with the `small` macro from Windows headers
  (renamed to `tiny`).
- `test_simd.py`: emulator backend detection in an assertion.

## [0.5.0] - 2026-10-07

### Added
- DLPack interop (`Image.from_dlpack`, `__dlpack__`) and image I/O helpers `imread` / `imwrite`.
- Examples `06_imread_imwrite.py` and `07_dlpack.py`, and updated notebooks.
- Tests for DLPack and the functional API.

### Changed
- Legacy DLPack behavior on numpy < 2.1 (read-only views, no `copy=` / `device=` arguments) is
  handled and documented; the affected tests are skipped on those versions.
- README API examples updated.

## [0.4.0] - 2026-10-04

### Added
- Standalone benchmark suite (`benchmarks/`) with its own build system and an HTML report, with
  LaTeX formulas rendered by KaTeX (inlined, so the report stays self-contained).
- Pillow as a test-only dependency, with image format parity tests.
- Exhaustive PNG roundtrip exactness tests (noise, flat, gradient, tiled, sparse, photo-like,
  degenerate sizes, more than 64K deflate tokens), a lossy tolerance test and a JPEG golden test.
- `Font.open()` accepts a file path and uses memory mapping on Unix and macOS.
- Getting Started, resize filters and font rendering example notebooks and the first Python examples
  (`01` to `05`).

### Changed
- Faster font loading (memory mapping, fast integer conversion).
- Faster JPEG encoding on GCC/Clang (`-fno-trapping-math` enables vectorized float to int
  conversion) and a faster PNG encoder in the bundled `stb_image_write.h`. The default PNG
  compression level is 8 (was 6).
- libdeflate was vendored and then removed again within this cycle; the PNG path uses the optimized
  `stb_image_write.h`.
- Lossy tolerances in tests are standardized; benchmarks and `verify.py` use normalized mean
  absolute error (NMAE).
- Documentation states that the optimized JPEG encoder is not guaranteed to be byte-identical across
  compilers and flags (rare rounding ties, for example Apple clang on ARM64 or x86 with `-mfma`).
  A tool, `tests/tools/jpeg_vs_upstream.py`, checks equivalence for identical build settings.

## [0.3.0] - 2026-09-30

### Added
- Python binding: errors map OS error codes to `FileNotFoundError`, `PermissionError` and similar;
  the GIL is released during font rendering (`render_glyph`, `render`, `make_atlas`).

### Changed
- C++ namespace and headers renamed from `libstb` to `stb` (`include/libstb.h` became
  `include/stb.h`, `include/libstb/` became `include/stb/`). The Python package is still `libstb`.
- The abstract encoder hierarchy (`PngEncoder`, `JpegEncoder`, ...) was removed in favor of direct
  methods on the image type (`to_png`, `write_jpg`, ...).
- `save()` was replaced by `write()`; the format is detected from the file extension
  (case-insensitive).
- Resize accepts a filter by name (for example `"cubic"`).
- Documentation clarifies the move-only semantics of `stb::image` and pixel memory ownership, and
  the difference between numpy views and copies.

### Fixed
- Windows: `utest.h` macro collision, and the `NOMINMAX` / `WIN32_LEAN_AND_MEAN` guards in the test
  headers.

## Initial commit - 2026-09-28 (tag `__beta__`)

First state: C++17 core over the stb single-header libraries with image decoding and
encoding (PNG, JPEG, BMP, TGA), resizing with selectable filters, TrueType font rendering, and a
nanobind Python binding.

[Unreleased]: https://github.com/mohammadraziei/libstb/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/mohammadraziei/libstb/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/mohammadraziei/libstb/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/mohammadraziei/libstb/compare/__beta__...v0.3.0
