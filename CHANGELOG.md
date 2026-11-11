# Changelog

All notable changes to libstb are listed here, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Dates are commit dates.

This file was generated from the git history (61 commits, tags `v0.3.0`, `v0.4.0`, `v0.5.0`, `v0.6.0`, `v0.7.0`); 0.8.0 was written by hand from the 26 commits after `v0.7.0`.

## [Unreleased]

### Changed
- **Left-to-right text is shaped** (`font::shape`, and with it `draw_text`, `measure`, `render` and the canvas): a line without a
  right-to-left letter used to be one glyph per character; it now runs the font's GSUB (`ccmp`, `locl`, `rlig`, `calt`, `liga`,
  `clig`, `rclt`) and GPOS (`kern`, `dist`, `mark`, `mkmk`) per script run (Latin, Greek, Cyrillic), so "fi" is one glyph
  (cluster 0; `shape_options::ligatures = false` gives two), accents are decomposed by `ccmp` and hung on their letter from the
  font's anchors, and kerning comes from the feature. Fonts without features for the script, and other scripts, are unchanged
  (one glyph per character). **Visible change:** text drawn with a font that has `liga` now shows its ligatures. Output equals HarfBuzz's
  (glyphs, clusters, advances) on random strings of one script; the text is not Unicode-normalised first, and left-to-right words
  inside a right-to-left line are still not shaped. Issue 8 of the novasvg list. Test font `libstb-latin-test.ttf` (`make_latin_test_font.py`).
- `kerning_mode::kern_table` now means the legacy `kern` table only in `font::shape` for every script: the GPOS kerning that the
  shaper computed (Arabic text had it already) is no longer added on top of it.

### Added
- **`composite_row` and `composite_solid_row`** (issue 11 of the novasvg list): the Porter-Duff operators (`composite_op`, all 13)
  and the blend modes (`blend_mode`, all 9) on one row of premultiplied 0xAARRGGBB pixels, in place, with a coverage
  `const_alpha` 0..255. They are the inner loop of a rasterizer: raw `std::uint32_t` rows, no clipping, no geometry. The source is a
  row or one colour. The integers are those of novasvg's own compositing code (rounding included), so a rasterizer that moves to
  these functions draws the same pixels; the result is within two levels of the floating-point W3C formulas for every operator x
  mode, and within one level for the plain operators. Operators other than source_over combined with a blend mode take the
  source with the backdrop's colours mixed in, then run at full strength (coverage decides the share). Tests:
  `test_composite_row.cpp` (13 x 9 x 3 coverages against a float reference, and against `image::blend`). No Python binding
  (it works on raw rows).
- **Font weight and width** (issue 10 of the novasvg list): `font::weight()` and `font::width()` (OS/2 `usWeightClass` 1..1000 and
  `usWidthClass` 1..9; a font without an OS/2 table, or with a value outside the range, gives 700 or 400 by `is_bold()` and 5), and
  the same two fields (`weight`, `width`) in `font_face_info` / `system_font_info`, so `font::scan` and `font::system_fonts` tell
  "DejaVu Sans", "DejaVu Sans ExtraLight" and "DejaVu Sans Condensed Bold" apart although `family_name()` (name id 16) is the same
  for all three. `font::legacy_family_name()` (and `legacy_family_name` in `font_face_info`) is name id 1 ("DejaVu Sans Light"),
  the name a program that knows only regular / bold / italic lists the face under, and what `font-family: Inter Light` means.
  Python: `Font.weight`, `Font.width`, `Font.legacy_family_name`, and `weight`, `width`, `legacy_family_name` (last, with defaults)
  in `FontFaceInfo` and `SystemFont`. The test fonts of `make_info_test_fonts.py` carry them (300 / 700 / width 4).
- **Font names and style** (issue 6 of the novasvg list): `font::family_name()` (name id 16, else 1; English first, UTF-8),
  `font::is_bold()` and `font::is_italic()` (OS/2 `fsSelection`, `head.macStyle` when there is no OS/2 table);
  `font::scan(path or bytes)` gives a `font_face_info` (`index`, `family_name`, `bold`, `italic`) for every face of a
  .ttf / .otf / .ttc / .woff by reading only the `name`, `OS/2` and `head` tables (a file on disk is mapped, not read);
  `font::system_font_dirs()` and `font::system_fonts()` list the faces under the operating system's font folders as
  `system_font_info` (a `font_face_info` plus `path`), sorted, skipping files that are not fonts; `system_fonts(dirs)`
  does the same for the folders you give. Python: `Font.family_name`, `Font.is_bold`, `Font.is_italic`, `Font.scan`,
  `Font.system_font_dirs`, `Font.system_fonts`, `FontFaceInfo`, `SystemFont`. Test fonts `libstb-info.ttc` and
  `libstb-info.woff` (`make_info_test_fonts.py`).
- **`image::premultiplied()` and `image::unpremultiplied()`** (Python: `Image.premultiplied()`, `Image.unpremultiplied()`):
  a copy with the colour channels multiplied by alpha (`c * a / 255`) and the inverse (`c * 255 / a`, alpha 0 gives zeros),
  for `rgba` and `gray_alpha`; other formats come back as a copy. The bytes keep the order R, G, B, A and the image does not
  track that it is premultiplied (the default storage stays straight RGBA). Same integers as `rgba_to_premultiplied_argb`
  (the channel arithmetic is now shared), run by the SIMD backend in use for RGBA.
- **`premultiplied_image`** (Python: `PremultipliedImage`): pixels already multiplied by alpha, one 32-bit word `0xAARRGGBB` per pixel
  (what rasterizers draw into, and what `rgba_to_premultiplied_argb` produces), as a type of its own so that `blend`, the encoders
  and `resize` cannot be called on it by mistake. `from_image()` (any format) and `to_image()` use the SIMD conversions; `wrap()`
  views external memory (4-byte aligned, stride a multiple of 4, padding allowed); move-only with an explicit `copy()`. In Python:
  `from_image`, `to_image`, `copy`, `width`, `height`, `stride`, `empty` and a no-copy `(height, width)` uint32 `__array_interface__`.
  Issue 9 of the novasvg list (novasvg can drop its `surface` and `pixmap`).
- `stb::detail::image_base`: the pixel memory and size that `image` and `premultiplied_image` share (protected constructors, so it is
  not usable on its own). `image` derives from it; its public interface is unchanged.

### Fixed
- `color::try_parse` / `from_string`: `hsl()` and `hsla()` are converted in float32, as browsers do, so a value that is a
  .5 tie in exact arithmetic rounds the way Chromium paints it (`hsl(90, 100%, 76.2745098039%)` is now R=195, not 194).

## [0.10.0] - 2026-11-09

### Added
- **CSS colours.** `color::from_name` knows all 148 CSS keywords (and `transparent`); `color::from_string` and the new
  `color::try_parse(text, &used)` (no exception, reads a prefix) understand `rgb()`, `rgba()`, `hsl()` and `hsla()` in the
  comma and the space/slash forms, with percentages, `deg`/`grad`/`rad`/`turn` hues and clamping, as CSS does.
- **Porter-Duff operators.** `enum class composite_op` (13 operators) and a last parameter `op` of `image::blend` (Python:
  `CompositeOp`, `Image.blend(..., op=)`, `canvas::image_item::composite(op)` / `CanvasImage.composite(op)`, and for shapes `draw_options::composite`, `canvas::outline_item::composite(op)`, `Image.fill_outline/stroke_outline(composite=)`, `CanvasOutline.composite(op)`: there an operator other than `source_over` acts only where the shape is, by its coverage, so `clear` cuts a smooth hole); `blend_mode::exclusion` (`BlendMode.EXCLUSION`).
- **Geometry.** `outline::arc_to` (the SVG endpoint arc, as cubics), `outline::tight_bounds()` (exact box, curve extremes
  instead of control points), `matrix::map_rect` and `matrix::map(const point*, point*, n)`; in Python `Outline.arc_to`,
  `Outline.tight_bounds`, `Matrix.map_rect`, `Matrix.map_points`.
- **GPOS kerning and positioning for Arabic script runs.** The font's `kern`, `dist` lookups are run
  (single and pair adjustment, contextual and chained contextual lookups, extension lookups), chosen by the
  script's features instead of scanning every pair lookup. Vazirmatn (whose Arabic kerning is a chained
  lookup) now equals HarfBuzz glyph for glyph, advance and offset.
- Marks of one letter are ordered like HarfBuzz does (combining class, shadda first), and a mark without a
  letter takes no room.
- **GPOS mark attachment in `font::shape()` and text drawing.** Marks (harakat) go where the font's `mark` and
  `mkmk` features put them (mark to base, to ligature, to mark); a mark without an anchor keeps the centred
  placement. `shape()` and `draw_text` now agree. Test font `libstb-fa-gpos-test.ttf` (`make_mark_test_font.py`).
- **`shaped_glyph::kind` (`glyph_kind`: `normal`, `notdef`, `line_break`)** tells a line break from a missing
  character, which both have glyph 0. Python: `ShapedGlyph.kind`.
- **`font::shape(text, begin, end)`** shapes a part of the text as part of the whole run (joining, ligatures,
  kerning across the edges). Python: `Font.shape(..., begin=0, end=None)`.
- **`data/`: sample pictures and fonts, and examples and tests that use them.** Four photographs (`camera.png`,
  `astronaut.png`, `chelsea.png`, a real JPEG `rocket.jpg`), an RGBA `badge.png` made by `data/make_badge.py`, and
  Vazirmatn Regular and Bold (Persian, Arabic and Latin; GSUB and GPOS), with their licenses in `data/README.md`
  (the "Lena" image is left out on purpose: it is not redistributable). Every Python example and notebook now works on
  these instead of generated gradients, and the C++ examples run without arguments (`LIBSTB_DATA_DIR` is set by the
  build). New examples: `font_text`, `vector_text`, `limits_and_zlib` and `logging_and_threads` (C++), and
  `13_font_glyphs_and_shape.py`, `14_untrusted_input_and_zlib.py` and `15_logging_threads_simd.py` (Python).
  New tests: `test_sample_data.py` and `test_sample_fonts.py` (Python) and `test_sample_data.cpp`: decoding against
  Pillow, every writer, resizing, alpha, SIMD backends and thread counts on real pictures, and Persian shaping
  against HarfBuzz on the real font (588 random texts, no difference in glyphs or advances).
- **Python: the glyph-index API and `Font.shape`.** `Font.glyph_index(char)`, `glyph_count`, `glyph_info(glyph)`
  (`GlyphMetrics`), `design_metrics` (`DesignMetrics`), `outline(glyph)` (the command tuples `Outline` takes, so
  `Outline(font.outline(g))` works), `glyph_kerning(left, right, mode)`, the static `Font.face_count(source)` and
  `Font.shape(text, *, direction, kerning, ligatures)` (a list of `ShapedGlyph`), all in font units with y up, as in
  C++. Together with `Matrix` and `Image.fill_outline` a vector text renderer can be written in Python. An index past
  the glyphs is "nothing", a negative or oversized one is a `ValueError`. `shape` lets go of the GIL for texts of 512
  bytes or more; the cheap calls keep it. The module docstring and `font.hpp` no longer say the font reader is
  stb_truetype: it is libstb's own and checks its bounds (it has not been fuzzed).
- **`font::shape`**: UTF-8 text to positioned glyphs, in visual order and in font units (y up), for a vector
  renderer that draws glyph outlines itself. It makes the decisions `draw_text` makes (joined letter shapes, GSUB
  ligatures, mirrored brackets, the bidirectional order, kerning) and returns, per glyph (`shaped_glyph`), the glyph
  index, the `cluster` (the byte offset of the first character it stands for, carried through joining, ligatures,
  mirroring and reordering), `x_advance` (kerning folded in) and `x_offset` / `y_offset` (the harakat are centred on
  their letter and stacked, as `draw_text` does but unrounded). `shape_options`: `direction`
  (`text_direction::{automatic, ltr, rtl}`, the paragraph direction), `kerning` (`kerning_mode`) and `ligatures`
  (`false` skips the optional `liga` / `clig` features; `rlig` and the joining shapes always run). A `'\n'` is a glyph 0
  with its own cluster. Nothing about `draw_text` changes. The internal shaper type `shaped_glyph` became
  `visual_glyph` (the public name is now taken) and `bidi::resolve` takes an optional forced direction.
- **Operating system log handlers** (`stb/logging_os.hpp`): `syslog_handler` (POSIX syslog), `android_handler`
  (logcat, the tag is the logger's name unless given), `unified_log_handler` (Apple os_log, the category is the
  logger's name), `debug_string_handler` (Windows `OutputDebugStringW`, UTF-8 converted) and `event_log_handler`
  (Windows Event Log). A class exists only where its facility does, and `STB_LOG_HAS_SYSLOG`, `STB_LOG_HAS_ANDROID`,
  `STB_LOG_HAS_UNIFIED`, `STB_LOG_HAS_DEBUGSTRING` and `STB_LOG_HAS_EVENTLOG` say which (1 or 0). Each maps the levels
  to the five severities of its system and leaves the time and level out of the default pattern, which the system
  adds itself. The library now links `advapi32` on Windows and `log` on Android.
- **WOFF 1.0 fonts.** `font::open` and `font::from_memory` (and Python `Font.open`) read a `.woff` file: a WOFF is a
  TrueType/OpenType font with its tables zlib-compressed one by one, and libstb unpacks it, in memory, with its own
  zlib (`src/font/woff.cpp`) into the font file it holds, which the rest of the font code reads unchanged. A WOFF has
  one face, so `font::face_count` is 1 and an `index` other than 0 is a `decode_error`. A damaged file (a wrong length,
  tables out of order, outside the file or on the same bytes, a table that does not unpack to its `origLength`) is a
  `decode_error`; one that would unpack to more than 512 MiB is a `limit_error`, and no memory is reserved on the word
  of a length field. WOFF2 needs Brotli and is refused with a `decode_error` that says so.
- libstb says what it did and why, through its own loggers: `libstb.decode`, `libstb.encode`, `libstb.resize`,
  `libstb.zlib`, `libstb.font`, `libstb.raster`, `libstb.simd`, `libstb.threads`. At `debug`: one record per call (the
  SIMD backend, the threads a resize uses, the size of a decode, encode or zlib call, glyphs a font lacks). At `warning`:
  only when libstb changed what was asked (NaN or infinite coordinates, a coordinate moved to the limit, an unusable dash
  pattern, a NaN stroke width or miter limit, a thread that could not be started). Silent at the default level, and a
  failure is an exception, never also a record. See `docs/adr/0003-logging-policy.md`.
- `docs/adr/`: architecture decision records for the logging module, its Python bridge and the logging policy.
- Python: libstb's log records go to Python's `logging`. `import libstb` installs a bridge, so `logging.getLogger("libstb")`
  and its children (`libstb.simd`, `libstb.threads`) work like any other logger (same level numbers, same hierarchy; the
  level and handlers are yours to set). `libstb.logging_bridge(False)` removes it and libstb returns to its own output
  (`LIBSTB_LOG`, stderr). While the bridge is on, `LIBSTB_LOG`, if set, still decides what libstb emits.
- `stb/logging.hpp` (`stb::logging`): a logging module modelled on Python's `logging`: `get_logger("libstb.simd")`
  with a tree of dotted names, `logger` (levels with the Python numbers, `propagate`, printf-style `debug` / `info` /
  `warning` / `error` / `critical` built on `stb::sformat`), `handler` (`stream_handler`, `file_handler`,
  `callback_handler`, `null_handler`, or your own class), `formatter` (`%(asctime)s [%(levelname)s] %(name)s: %(message)s`
  patterns), `basic_config`. `logger::add_handler` takes the handler by value and owns it, so no `shared_ptr` shows.
  `LIBSTB_LOG=debug|info|warning|error|critical` sets the level of `libstb`. The registry lives in the library.
  The two messages libstb printed on stderr (a bad `LIBSTB_THREADS` or `LIBSTB_SIMD`) now go through it: with no
  handler they still reach stderr, now as `libstb.threads: ...` / `libstb.simd: ...`, and a program can silence or
  redirect them.
- **Vector outlines (issue #2).** `stb/raster.hpp`: anti-aliased `stb::fill` and `stb::stroke` of a `stb::outline` onto an
  image (`fill_rule`, `stroke_style` with width, `line_cap`, `line_join`, `miter_limit`, dashes and a dash offset,
  `draw_options` with matrix, blend mode and opacity), and the low-level `rasterize_fill` / `rasterize_stroke` that
  report coverage `span`s to a callback (`span_sink`) for your own compositing. `outline::dashed` cuts an outline into
  dashes. The rasterizer and stroker are fixed-point ports of the FreeType algorithms (through plutovg), under
  `src/draw/raster/`, and give the same pixels on every platform: `tests/cpp/test_raster_reference.cpp` checks 319
  scenes against plutovg byte for byte (see `tests/tools/raster_reference/`).
- `stb::canvas::add_outline` (`outline_item`: fill, rule, stroke, cap, join, miter_limit, dash, transform, blend, opacity).
- Python: `Matrix`, `Outline` (builder calls, `commands`, `bounds`, `dashed`, `fill_mask`, `stroke_mask`),
  `Image.fill_outline`, `Image.stroke_outline`, `Canvas.add_outline` / `CanvasOutline`.
- `STB_MIN` / `STB_MAX` (in `stb/export.hpp`): `std::min` / `std::max` that `<windows.h>` cannot break (its `min` and
  `max` macros). Used in `draw.hpp` and the library instead of `(std::min)(...)`.
- `stb/outline.hpp`, the geometry half of the vector-drawing layer (issue #2): `stb::outline` (builder calls
  `move_to`, `line_to`, `quad_to`, `cubic_to`, `close`; whole shapes `add_rect`, `add_rounded_rect`,
  `add_ellipse`, `add_circle`; `add_outline` / `add_commands` with an optional matrix, `transform`,
  `current_point`, `bounds`) and `stb::matrix` (SVG convention, fluent `translate` / `scale` / `rotate` /
  `shear`, `then`, `map`, `inverse`). A glyph outline from `font::outline` goes straight into an outline.
  `point`, `outline_op` and `outline_command` moved from `stb/font.hpp` to this header (same names, `font.hpp`
  includes it). Everything under `src/draw` is now built without FMA contraction, not only `shape.cpp`.
- Glyph-index API of `stb::font`, for vector renderers: `outline(glyph)` (also `outline(glyph, out)` to
  reuse a buffer) as `outline_command`s in font units (`move_to`, `line_to`, `quad_to` for TrueType,
  `cubic_to` for CFF, `close`; y up), `glyph_index(codepoint)`, `glyph_count()`, `glyph_info(glyph)`
  (`glyph_metrics`: advance, lsb, ink box, `has_outline`), `design_metrics()` (`font_design_metrics`:
  `units_per_em`, ascent, descent, line gap, font bounding box), `glyph_kerning(left, right, mode)` with
  `kerning_mode::{none, kern_table, automatic}` (`kern_table` ignores GPOS, `automatic` is what `kerning()`
  uses), and `font::face_count(data | path)` for `.ttc` collections. An index past `glyph_count()` gives an
  empty outline and zero metrics, never an exception. New types `point`, `outline_op`, `outline_command`
  (with `point_count(op)` and `==`). Tests use new synthetic fonts: a TrueType one with curves, a
  composite and GPOS plus `kern`, a CFF one, and a `.ttc` (`tests/data/make_outline_test_fonts.py`).
- `stb::sformat`, `stb::vsformat` and `stb::sformat_to` (`stb/util.hpp`): printf for `std::string`.
  One `vsnprintf` into a 256 byte stack buffer in the common case (one allocation, none for a short
  result), a second exactly-sized one for a longer text; `sformat_to` appends to an existing string
  without a temporary. GCC and Clang check the arguments against the format (`-Wformat`). The
  messages that were built with `std::to_string` chains and an `ostringstream` (resize, image, font,
  zlib, canvas, `color::to_string`, the Python bindings) now use it, with the same texts.
- `stb::thread_count`, `stb::set_threads` and `stb::thread_scope` (Python: `libstb.thread_count`,
  `set_threads`, `with libstb.threads(n):`; environment `LIBSTB_THREADS=auto|off|<n>`): how many
  threads one call may use. `image::resize` / `resizer::resize` now share a big resize between
  threads (the engine's output-row splits) and run a small one on the calling thread. The decision is
  by cost: every extra thread must get about 250 microseconds of work, estimated from a floor of the
  measured single-core speed. The default is automatic (the CPUs the process may use: affinity and a
  Linux cgroup CPU limit); the pixels are byte for byte the same on any number of threads. Threads
  last for the call only (no pool). Internals in `src/util/parallel.hpp` (`threads_for`,
  `run_parallel`, `parallel_for`).
- `stb::rgba_to_premultiplied_argb` and `stb::premultiplied_argb_to_rgba` (issue #1): the conversion
  between straight RGBA and the premultiplied ARGB32 words (`0xAARRGGBB`) that rasterizers such as
  novasvg, plutovg, Cairo, Skia and Qt draw into. Free functions on raw buffers (strides in bytes,
  in place allowed), run by the SIMD backend in use (`avx2`, `sse2` / `neon`, `scalar`), and
  bit-exact with the plain `c * a / 255` and `c * 255 / a` formulas (a colour above its alpha, which
  only invalid input has, wraps modulo 256 like a byte store). Blocks of 32 opaque pixels only swap
  R and B. Tests check every (colour, alpha) pair on every backend; `cmake --build <dir> --target
  stb_bench_premultiply` times them.

### Changed
- **The wheel keeps the C++ SDK in `libstb/sdk/`.** Headers, the shared core and the CMake package moved from
  `libstb/include`, `libstb/lib` and `libstb/cmake` to `libstb/sdk/include`, `libstb/sdk/lib` and
  `libstb/sdk/lib/cmake/libstb`: the same tree `cmake --install` makes, so `sdk` also works as a
  `CMAKE_PREFIX_PATH`. `get_include_dir()`, `get_lib_dir()` and `get_cmake_dir()` return the new places; new
  `get_sdk_dir()` and `libstb --sdk-dir`. The extension's rpath is `$ORIGIN/sdk/lib` (`@loader_path/sdk/lib`) and
  Windows registers `sdk/lib` with `os.add_dll_directory`. The old folders are gone.

- **`Image.to_pil()` no longer copies L and RGBA images**: the Pillow image looks at the pixels of the `Image` (it
  keeps it alive), as `img.numpy()` does. Pillow marks it read-only and copies on its first write, so it never writes
  into the `Image`; a change to the `Image`'s pixels (through `numpy()` or the array it wraps) shows in the Pillow
  image. `img.to_pil().copy()` is independent. LA and RGB are still copied (Pillow stores them at 4 bytes per pixel).
  `from_pil` still copies: Pillow gives its pixels out only as `tobytes()`.
- `stb::stroke_style::dashes` has an initializer, so `stroke_style{width}` no longer is a missing-initializer warning for users.
- Names, so that `path` means a file path everywhere: the vector geometry type is now `stb::outline`
  (`stb/outline.hpp`, `src/draw/outline.cpp`), with `outline_op`, `outline_command`, `outline_item`,
  `canvas::add_outline`; Python `Outline`, `CanvasOutline`, `Image.fill_outline` / `stroke_outline`,
  `Canvas.add_outline`. These names were only in this unreleased section, so nothing released breaks. The
  internal fixed-point type is `detail::fixed_outline`.
- `stb/shaping.hpp` is now `stb/rtl.hpp` (same functions: `shape_for_display`, `has_rtl`, `is_arabic_mark`), so
  that the header is named for what it does (right-to-left text), not "shaping", which also means the font's glyph
  shaping (`shape_glyphs`, `src/font/shaper/`). `stb.h` includes it as before.
- Source layout only, no API change: the encoders and the compressor moved to `src/codec/encode/` (next to
  `decode/`), the glyph rasterizer is `src/font/truetype/glyph_raster.cpp`, the Python outline bindings are in
  `bind_outline.cpp`, and the resizer's SIMD vector header is `src/image/resize/simd_vec.hpp`.

### Fixed
- The Python logging bridge no longer keeps its Python objects alive for ever: an `atexit` hook removes the handler
  and frees them while the interpreter is whole.
- **"nanobind: leaked 1 instances!" at the end of a script** that did `shared = np.from_dlpack(img)` and
  `back = Image.from_dlpack(shared)`. The reference an image keeps to what it views was dropped only while
  `Py_IsInitialized()` was true, which turns false when the interpreter starts to shut down, so the array (and
  through its DLPack capsule the first image) was leaked. It now uses `nb::is_alive()`.
- **Docs: the pixel size of text.** `font::metrics(pixel_height)` and `draw_text(size)` take the height from the
  descent to the ascent (hhea ascent minus descent), not the em, and the header said "em-height". The documentation
  now says so, and says to scale outlines by `size / (ascent - descent)` to match `draw_text` (`size /
  units_per_em` makes `size` the em, which is smaller text in most fonts).
- **Docs: the order of matrix calls.** Each call is applied to a point first, so `matrix().translate(40, 40).scale(3, 3)`
  is "scale, then move"; two snippets of the README had it the other way round.

## [0.9.0] - 2026-11-01

libstb no longer vendors any stb header: the decoders, encoders, resizer and font reader are libstb's
own code, derived from stb's algorithms (see `NOTICE.md`). The C++ core is now a shared library, so
the wheel is 0.55 MB instead of 4.3 MB, there is a `libstb` command line, and the image API gained
blend modes, fills, masks and stacking. Several names changed on the way (see Breaking).

### Breaking
- `image::composite` and `image::composite_in_place` are gone: `blend` replaces
  both and changes the image it is called on (it returns `*this`, so calls chain, and a
  temporary comes back by value: `base.copy().blend(logo, 16, 16)` is the old `composite`).
  In Python `Image.composite` is `Image.blend` and still returns a new image. `blend_mode::normal`
  at opacity 1 runs the same integer kernels as before, so its output is byte for byte the old
  one (the 533-case snapshot of the whole library shows 0 differences).
- the channel count is no longer a bare number anywhere in the public API. A new
  `enum class pixel_format { gray, gray_alpha, rgb, rgba }` (Python: `PixelFormat.GRAY`,
  `GRAY_ALPHA`, `RGB`, `RGBA`) replaces it: `image(w, h, format)`, `image::wrap`,
  `convert(format)`, `load_options::format` (an empty `std::optional` keeps the file's format),
  `image_info::format`, and in Python `Image.open(..., format=)`, `imread(..., format=)`,
  `Image.convert(format)`, `ImageInfo.format`. Python also takes the lower case name
  (`"gray"`, `"gray_alpha"`, `"rgb"`, `"rgba"`); a bare number is a `TypeError`. `channels()` /
  `.channels` stay as the plain count. `iminfo()` now returns `(width, height, format)`, and
  `ImageInfo.channels` is a property.
- the CMake target is `libstb::libstb` (was `libstb::core`), both in the build tree
  (an alias of `stb_core`) and in the installed `find_package(libstb)` package.
- public headers merged by cohesion: `stb/resizer.hpp` is part of `stb/image.hpp`,
  `stb/shape.hpp` and `stb/canvas.hpp` became `stb/draw.hpp`, `stb/env.hpp` and `stb/utf8.hpp`
  became `stb/util.hpp`. `<stb.h>` still includes everything.

### Added
- Command line: `libstb` and `python -m libstb` (argparse, no dependencies). `--version`,
  `--cmake-dir`, `--include-dir`, `--lib-dir`; `info FILE... [--json]`; `convert SRC DST` (format
  from the extension, `--resize WxH|Wx|xH`, `--filter`, `--pixel-format`, `--orient`, `--quality`,
  `--compression`, `--no-rle`); `zlib compress|decompress SRC DST` (`-` is stdin or stdout).
- `image::blend(overlay, x, y, mode, opacity)`: blend modes `normal`, `multiply`, `screen`,
  `overlay`, `darken`, `lighten`, `add` and `difference` (`enum class blend_mode`, Python
  `BlendMode` or the lower case name), and an `opacity` between 0 and 1 that scales the overlay's
  alpha. Works with every channel count and with alpha below and above. Python:
  `Image.blend(overlay, x, y, mode="normal", opacity=1.0)`.
- `image::fill(color, x, y, width, height)` (and `fill(color)` for the whole image): overwrites
  pixels with a colour, clipped to the image, stored in the image's format (gray takes the
  luma, alpha is dropped where there is none). Python: `Image.fill(color, x, y, width, height)`.
- `image::alpha()` (the alpha channel as a gray mask) and `image::with_alpha(mask)` (add or
  replace the alpha channel). Python: `Image.alpha()`, `Image.with_alpha(mask)`.
- `image::hstack` and `image::vstack` (also `Image.hstack`, `Image.vstack`): several images side
  by side or one above the other, with `gap`, `background` and `align` (`enum class align`,
  Python `Align` or `"start"`, `"center"`, `"end"`). The result has colour and alpha if any
  input has.
- `canvas::add_image(image, x, y)` with `.blend(mode)` and `.opacity(o)` on the returned
  `image_item` (Python `Canvas.add_image`, `CanvasImage`). The canvas keeps its own copy, and its
  items (images, texts, shapes) are drawn in the order they were added.
- `pixel_format` helpers: `image::format()`, `Image.format`, `channel_count`, `has_alpha`,
  `pixel_format_name`, `pixel_format_from_name`, `pixel_format_from_channels` (the one place a
  number becomes a format).
- `&&` overloads of `image::convert`, `flatten`, `orient`, `rotate90` and `pad`. When the call
  changes nothing (same channel count, no alpha to flatten, orientation 1, whole turns, no border)
  a temporary or `std::move(img)` hands its pixels back instead of copying them; every other case
  and every error is the same as the `const&` version. The `const&` overloads still copy.
- `include/stb/export.hpp` with the `STB_API` macro (see Changed), the `STB_BUILD_EXAMPLES` CMake
  option (off by default) and `examples/cpp/tour.cpp`: one walk through load, inspect, geometry,
  resize, canvas, masks, stacking and encode.

### Changed
- **The C++ core is a shared library** (`libstb.so`, `libstb.dylib`, `stb.dll`) and the Python
  extension links to it, so a wheel holds one copy of the engine instead of two. The wheel went
  from 4.3 MB to 0.55 MB (the old one shipped an LTO `libstb_core.a` next to an extension that
  already contained the same code). `find_package(libstb)` after `pip install libstb` links C++
  programs to that `libstb.so`, with an rpath, so nothing needs `LD_LIBRARY_PATH`. Linux and
  macOS set `$ORIGIN/lib` / `@loader_path/lib` on the extension; on Windows `stb.dll` is installed
  in `libstb/lib` too and `libstb/__init__.py` registers that folder with `os.add_dll_directory`.
- Public headers mark the exported classes and functions with `STB_API` (`dllexport`/`dllimport` on
  Windows, default visibility elsewhere, empty for a static build or for sources compiled into a
  target). The library is built with `-fvisibility=hidden`, so the decoders, the font reader and
  the kernels are not part of its interface.
- musl (musllinux wheels, or a build on Alpine; CMake detects it from the C library): the Python
  extension carries its own statically linked copy of the core instead of using `libstb.so`.
  nanobind links extensions there with `-static-libstdc++ -static-libgcc`, and a dynamic libstdc++
  crashes on the first C++ exception on musl, so the shared core cannot be used from Python there;
  two C++ runtimes (static in the extension, dynamic in `libstb.so`) cannot pass an exception to
  each other either. `libstb.so` is still installed for C++ programs.
- PNG encoder: the best filtered row is kept while the five filters are tried, instead of filtering
  it a second time (one pass less per row, 216 encodes byte for byte identical). BMP and TGA
  encoding is faster.

### Fixed
- musllinux wheels crashed on the first C++ exception (see the musl entry above).
- Windows: `stb.dll` is found when the extension is imported (copied next to the extension in the
  build tree, `os.add_dll_directory` in the installed package).
- A typo in an `assert` and a stream reference in the JPEG and PNG decoders.

### Removed
- `image::composite` and `image::composite_in_place` (use `blend`).
- `src/third_party/`: no stb header is vendored any more. `NOTICE.md` records the lineage and the
  license.

### Internal (refactor, no behaviour change)
- **Decoders:** `stb_image.h` is split into `src/codec/decode/`, one file per format, with `stream`,
  `inflater`, `jpeg_decoder`, `png_decoder` and `gif_decoder` classes; the callback IO, 16-bit and
  linear loaders and unused setters are removed. 23,680 decodes (every format, damaged files too)
  give the same pixels or the same error as the stb build.
- **Encoders and zlib compressor:** libstb's own code in `src/codec/` (`png.cpp`, `jpeg.cpp`,
  `bmp_tga.cpp`, `deflate.cpp`), written as classes with no global state (the old mutex around
  `stb_image_write`'s globals is gone). Output bytes are unchanged (PNG at every level, JPEG, BMP,
  TGA, zlib), and the JPEG encoder still writes the same bytes as upstream `stb_image_write` v1.16
  built with the same compiler.
- **Resizer:** `stb_image_resize2.h` is split by topic into `src/image/resize/` (api, config, srgb,
  simd, filters, coefficients, coders, decode, horizontal, vertical, scanline, sampler, engine, and
  the three template `.inl` files that upstream produced by including its own header again).
  Declarations of functions that no longer exist (`stbir_set_pixel_callbacks`,
  `stbir_set_user_data`, `stbir_set_pixel_layouts`, the subrect setters), the unused
  `stbir_quick_resize_helper` and the documentation of the removed setters are deleted, which also
  silences the matching `-Wunused-function` warnings.
- **Font reader:** `src/font/truetype/` replaces `stb_truetype.h` and `stb_rect_pack.h` (font file,
  cmap, GPOS / kern kerning, TrueType and CFF outlines, the anti-aliasing rasterizer and the atlas
  packer). Every read is bounds-checked and composite glyphs have a recursion limit. Glyph bitmaps,
  metrics, kerning and atlases are byte-for-byte what stb produced on 150 real fonts (TrueType,
  CFF, collections).
- **Source layout:** `src/core/` is gone. One folder per module: `src/util`, `src/image`,
  `src/codec`, `src/zlib`, `src/font` (with `shaper/`), `src/draw`. Internal headers are included
  relative to `src/` (`"image/simd.hpp"`).
- `include/stb/color.hpp` went from 1051 to 240 lines: string parsing (`from_string`, `from_hash`,
  `from_name`), `to_string`, `blend` and the stream operators moved to `src/draw/color.cpp`, and
  `from_name` looks names up in one table (values still come from the named factories, so each
  color is defined once). 40,000 parse, error-message and blend outputs match the previous header
  byte for byte.
- Decoder cleanup: redundant `this->` and local `stream *s` aliases are gone from the JPEG and PNG
  decoders, local macros became `constexpr` values and inline functions (`zfast_bits`, `fast_bits`,
  `marker_none`, `is_restart`, `is_sof`, `png_type`, `div4`, `float2fixed`, `hdr_buflen`, ...), and
  `typedef` became `using` or a plain `struct`. Data members of the stateful decoder classes
  (`jpeg_decoder`, `png_decoder`, `gif_decoder`, `inflater`) end in an underscore like the rest of
  the project, and cryptic ones got real names (`s_` became `stream_`; in the GIF decoder `w_`,
  `h_`, `pal_` and `lpal_` became `width_`, `height_`, `palette_` and `local_palette_`). The renames
  were made with libclang; 23,680 decoder cases are unchanged.
- Shaper complexity (clang-tidy `readability-function-cognitive-complexity`, no warnings left in
  `src/font/shaper`): `bidi::classify_wide` reads an ordered range table, `arabic::joining_forms`
  uses small join helpers, and the GSUB context engine splits `try_rule` into `match_input`,
  `match_side` and `run_records`, with one `read_rule` instead of two copies of the rule layout
  code. `bidi::resolve` is a class with one method per UAX #9 rule, and `text_shaper::shape_line`
  and `gsub_table::make_plan` are split into steps. Verified over every code point (classification,
  mirroring, joining type), 200,000 random Arabic strings (forms, bidi order and levels) and new
  format 2 GSUB tests that also pass on the old code.
- Move semantics and special members reviewed (clang-tidy `performance-*`,
  `bugprone-use-after-move`, `cppcoreguidelines-special-member-functions`): no use-after-move and no
  avoidable by-value copies were found. `canvas`, `simd_scope` and `font_bytes` now say explicitly
  that they are neither copied nor moved, the polymorphic bases `glyph_source` and `subtable`
  cannot be sliced, and two no-op `std::move` calls are gone.
- The three `thumbnail` overloads and the two `Resizer` constructors share their code, constants
  are snake_case.

### Tests and tooling
- `tests/cpp/test_blend.cpp` and `tests/python/test_blend.py` (blend, fill, masks, stacking),
  `tests/python/test_packaging.py` (one `libstb.so` in the wheel, no `.a`, public symbols exported
  and internal ones hidden, an extension never mixes the two C++ runtimes) and
  `tests/python/test_cli.py`.
- Round-trip tests (`tests/python/test_roundtrip.py`, `tests/cpp/test_codec.cpp`): files go through
  `open`, `write` and `open` for png, bmp, tga and jpeg, in a chain of formats and from a loaded
  ppm; new tests for the `&&` overloads and their errors.
- Development checks that are not tests: `tests/tools/snapshot.py` (byte-for-byte comparison of two
  builds), `tests/tools/decode_snapshot.py` (the decoder corpus), and updated
  `tests/tools/jpeg_vs_upstream.py` and `zlib_fuzz.py`.
- The CI consumer test builds a C++ program against the installed package with `find_package`.

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

[Unreleased]: https://github.com/mohammadraziei/libstb/compare/v0.10.0...HEAD
[0.10.0]: https://github.com/mohammadraziei/libstb/compare/v0.9.0...v0.10.0
[0.9.0]: https://github.com/mohammadraziei/libstb/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/mohammadraziei/libstb/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/mohammadraziei/libstb/compare/v0.6.0...v0.7.0
[0.6.0]: https://github.com/mohammadraziei/libstb/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/mohammadraziei/libstb/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/mohammadraziei/libstb/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/mohammadraziei/libstb/compare/__beta__...v0.3.0
