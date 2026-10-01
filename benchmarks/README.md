# libstb benchmarks

Self-contained and independent of the rest of this repository: this
directory installs libstb itself fresh from GitHub
(`pip install git+https://github.com/MohammadRaziei/libstb.git`), not
from `../src`, and manages its own virtualenv. You can copy this
`benchmarks/` directory out on its own and it will still work: same
convention as
[pygixml's benchmark suite](https://github.com/MohammadRaziei/pygixml/tree/main/benchmarks)
and [ctoon's](https://github.com/MohammadRaziei/ctoon/tree/main/benchmarks),
by the same author, which this one is modeled on.

## Running it

```bash
cd benchmarks
cmake -S . -B build
cmake --build build --target libstb_benchmark
```

That's the one target you need: it pulls in every benchmark, in the
right dependency order, and finishes by writing
**`results/report.html`**: a single, standalone HTML file (Chart.js
and every raw JSON result are embedded in it) with a "download JSON"
button per dataset. Open it in a browser; nothing else is required:
no server, no network, no sibling files.

`results/` is the **only** thing meant to be committed from this whole
build: everything else (the venv, the corpus, per-run JSON) lives
under `build/` and is disposable.

The first configure creates a venv and builds libstb from source into
it (scikit-build-core + nanobind), so it needs a C++17 compiler and
takes a few minutes. Later runs reuse the venv.

### Running one piece at a time

Every operation is its own target, independently runnable
(`cmake --build build --target <name>`):

| Target | What it does |
|---|---|
| `libstb_bench_venv` | Create the venv; install numpy/Pillow/OpenCV/imageio/scikit-image/pip-size/jinja2 + libstb itself (from git) |
| `libstb_bench_corpus` | Generate the synthetic multi-genre corpus |
| `libstb_bench_real_corpus` | Convert the real-world photographs (downloaded at configure time) into every format, same manifest shape |
| `libstb_bench_verify` | Check that every (operation, library) pair produces correct output, before anything is timed |
| `libstb_bench_sizes` | Real install footprint via `pip-size` |
| `libstb_bench_throughput` | Speed: decode / encode / resize, every library, every corpus entry |
| `libstb_bench_throughput_memory` | Peak memory for the same operations, libraries and entries |
| `libstb_bench_scaling` | Throughput vs. image size, 0.016 to 16.8 megapixels |
| `libstb_bench_fonts` | `Font` (stb_truetype) vs. Pillow's FreeType binding |
| `libstb_bench_system_info` | Record CPU / RAM / OS / Python |
| `libstb_bench_report` | Render `results/report.html` from all of the above |

The scripts also run by hand, from `python/`, on any machine that has
the requirements installed:

```bash
python corpus.py /tmp/corpus                   # writes the images + manifest.json
python verify.py                               # exits non-zero if any output is wrong
python bench_throughput.py /tmp/corpus/manifest.json out.json --repeats 5
python bench_fonts.py DejaVuSans.ttf fonts.json
```

### Options

```bash
# use a different libstb source (a branch, or a local checkout) instead
# of the default main-branch GitHub install
cmake -S benchmarks -B benchmarks/build -DLIBSTB_BENCH_GITHUB_URL=/path/to/local/checkout

# where report.html ends up
cmake -S benchmarks -B benchmarks/build -DLIBSTB_BENCH_RESULTS_DIR=/some/dir

# a noisier machine wants more repeats
cmake -S benchmarks -B benchmarks/build -DLIBSTB_BENCH_REPEATS=15
```

## What's benchmarked, against what

Each operation is measured on its own, against the libraries people
actually reach for, instead of being collapsed into one number.
`python/runners.py` is the single place that defines what every
(operation, library) pair does, and its module docstring is the
authoritative statement of why each pairing is fair. All four scripts
that measure something (speed, memory, scaling, verification) build
their callables there, so "decode a PNG with Pillow" means exactly one
thing across the suite.

| Operation | What it measures | Compared against |
|---|---|---|
| `info` | width / height / channels from the header, no pixel decoding | Pillow (the only other header-only API) |
| `decode` | encoded bytes in, uint8 array out | Pillow, OpenCV, imageio |
| `load_file` | path in, uint8 array out (file I/O included) | Pillow, OpenCV, imageio |
| `encode_png` / `encode_jpg` / `encode_bmp` / `encode_tga` | raw pixels in, file bytes out, at matched settings | Pillow, OpenCV, imageio |
| `resize_down_*` / `resize_up_*` | half size / double size; linear, cubic, box filters | Pillow, OpenCV, scikit-image |
| fonts: `open` / `measure` / `render` | stb_truetype text | Pillow's FreeType binding |

Formats covered: png, jpg, bmp, tga. OpenCV has no TGA codec, so it
has no TGA entries; that is a gap in the report, not a hidden zero.
scikit-image is benchmarked for resize only, and skipped above 1
megapixel (a single 2048x2048 cubic upscale takes many seconds and
hundreds of MB), which the report states rather than hides.

### Corpus

Two sources, merged into one manifest shape so every script treats
them identically:

- **Synthetic** (`corpus.py`): four genres, each at 64, 256, 1024 and
  2048 pixels square, all seeded so every machine sees identical
  pixels. `gradient` (smooth, the best case for PNG and JPEG), `photo`
  (several octaves of value noise plus hard edges plus sensor noise),
  `noise` (uniform random bytes: the worst case for every codec) and
  `graphics` (flat colour blocks and thin lines with a real alpha
  channel: the only genre that exercises the 4-channel paths). A
  corpus that cannot control the *shape* of the data cannot tell why a
  decoder is fast or slow, which is the reason for having genres at
  all.
- **Real** (`real_corpus_manifest.py`): six real photographs from
  OpenCV's sample data, downloaded at configure time into the
  gitignored `corpus/` folder. They are small (roughly 0.1 to 0.5
  megapixels), so they are there to keep the synthetic images honest,
  not to carry the large-image results.

Every image is written in all four formats by **Pillow**, never by
stb, so the decode benchmarks cannot be biased towards files that one
specific encoder produced. JPEG and BMP files of the `graphics` genre
are RGB: neither format has portable alpha, and `verify.py` caught the
libraries disagreeing about a 32-bit BMP's fourth byte while this
suite was being written.

Nothing about the generators was tuned after seeing numbers: they were
written once, before any measurement was collected.

### Timing

Best (minimum) wall-clock time over 7 repeats per (library, corpus
entry, operation) cell, one untimed warm-up call first: standard
practice for micro-benchmarks, since it's the closest a single run gets
to "no other process happened to interrupt this one."

Everything is single-threaded (`cv2.setNumThreads(1)`): libstb and
Pillow are single-threaded, and a benchmark where one contestant
quietly uses every core is measuring the machine, not the library.

Setup that is not the operation is never timed. `runners.prepare()`
imports the library, reads the file into memory and builds the
library's own native image object from raw pixels (for libstb that is
`Image(array)`, which shares the array's memory; for Pillow,
`Image.fromarray`, which copies), and returns a zero-argument callable.
Only that callable is measured. This is deliberate and it cuts both
ways: a workflow that starts from a NumPy array pays for `fromarray`
with Pillow and not with libstb, and the benchmark does not count
either.

### Matched settings, and why output size is reported

Encoders are run at explicitly matched settings, never at each
library's default (the defaults differ on purpose: the PNG level
is 6 in libstb and Pillow, 1 in OpenCV):

- PNG: level 6 everywhere (libstb's default too).
- JPEG: quality 90 everywhere.
- TGA: uncompressed everywhere (libstb with `rle=False`).

Matching the *setting* does not match the *output*. libstb deflates PNG
with a vendored libdeflate while Pillow and OpenCV use zlib, and its JPEG
encoder is stb's (a simple baseline encoder, vectorized but not SIMD-tuned)
against libjpeg-turbo's hand-written SIMD. So each encode operation also records the output size in
bytes, and the report shows E[size / Pillow's size] next to the speed:
a faster encoder that writes a bigger file is making a trade, not
winning.

### Resize: two things that make a naive comparison wrong

- **Antialiasing.** libstb, Pillow and scikit-image prefilter when
  shrinking. `cv2.INTER_LINEAR` and `INTER_CUBIC` do not: they are
  faster and they alias. OpenCV's antialiasing counterpart is
  `INTER_AREA`, which is what the box-filter row uses. Read the
  OpenCV linear/cubic downscale numbers with that in mind.
- **Colour space.** `stb_image_resize2` resizes in linear light by
  default (sRGB-correct). Pillow and OpenCV blend the stored values
  directly. The like-for-like pairing is `Resizer(filter, srgb=False)`
  and that is what is labelled "libstb". The library's real default is
  benchmarked as a separate series, "libstb (sRGB-correct)", so the
  price of correct behaviour is visible instead of hidden.

Only 3-channel images take part in resize: alpha means different
things in different libraries (premultiplied or not), which would not
be a like-for-like comparison.

### Correctness before speed

`verify.py` runs before any timing and stops the build if it fails. On
small **non-square** images (so a swapped width and height cannot hide)
it checks that every operation produces the right thing: lossless
formats must decode to the original pixels *exactly* (channel order
included), JPEG must agree with Pillow's decode within a small mean
error, encoder output is decoded back with Pillow, and resize output
must have exactly the requested shape and be close to Pillow's result
for the same filter on a smooth image. A speed number for an operation
that produced wrong pixels is worse than no number.

### The memory benchmark

`bench_op_memory_driver.py` spawns a fresh Python process per
(corpus entry, operation, library) via `bench_op_memory_one.py`, and
records two numbers from that process: the peak RSS before the
operation (after imports, input loaded, native object built) and after
it. The **delta** is what the report charts. The absolute peak alone
would mostly measure how heavy each library is to import (OpenCV alone
is tens of MB), which says nothing about a 2-megapixel resize.

It carries the same one real gotcha as pygixml's memory benchmark: on
Linux, `ru_maxrss` is **not** reset by `execve()`, so a measurement
subprocess spawned from a driver that had already inflated itself with
image data would inherit that inflation as its own reported "peak".
The driver therefore never opens an image: it reads only the
manifest's metadata (paths and byte counts) and hands each entry to a
fresh child.

What it does not measure, on purpose: images under 256x256 (the whole
operation fits inside allocator noise), `info` (it allocates nothing
worth measuring), and bmp/tga decoding (uncompressed: the memory story
is the file size). There is no separate size-sweep for memory as there
is in pygixml: the per-entry measurement already spans 0.065 to 4
megapixels, and the report plots memory against image size for every
operation.

## Requirements

- CMake >= 3.18
- Python 3.9+ (used to create the venv; nothing needs to be
  pre-installed into your system Python)
- A C++17 compiler (libstb is built from source into the venv)
- Internet access (competitors + libstb from PyPI/GitHub, the real
  photographs, a real TrueType font, and Chart.js are all fetched at
  build time: nothing is vendored in this repository). Every download
  of a corpus file is best-effort: if one fails, the suite continues on
  what it has, and without the font it skips the font benchmark.

## On fairness

Every library in these operations does the same class of work: one
pass over the pixels (decode, encode, resize) or one pass over the
glyphs (text). There is no O(1)-vs-O(n) story as in pygixml's
streaming layer, so ratios are fair here, and the report computes
them: it picks whichever library wins the most comparisons as the
**reference**, then for every other library computes
**E[target/ref]**: the ratio on *each* corpus entry individually,
averaged across entries, ratio-then-average and not
average-then-ratio.

```
E[time_x / time_ref] = mean( time_x_i / time_ref_i  for each corpus entry i )
```

not `median(time_x) / median(time_ref)` and not
`mean(time_x) / mean(time_ref)`. Corpus entries here span three orders
of magnitude in pixel count, and averaging the raw numbers first lets
the 2048x2048 images dominate; ratio-then-average weighs every entry's
*relative* performance equally. See `_e_ratio()` in
`report/generate_report.py`.

That the reference is "whoever wins most often" means the report can
end up with a reference other than libstb, and the summary line will
say so; nothing in it is hard-coded.

## What these benchmarks do not say

- **Security.** stb is written for trusted input. Its own readme is
  explicit that stb_image and stb_truetype are not hardened against
  malicious files, while libjpeg-turbo, libpng and FreeType are
  actively fuzzed. That is a real difference between libstb and every
  competitor here that no speed number captures. If you decode
  untrusted uploads, this matters more than any chart.
- **Features.** libstb is deliberately small. It does not do CMYK,
  16-bit-per-channel PNG pipelines, ICC profiles, EXIF orientation,
  animated formats, or text shaping. The install-size table is there
  to give context, and its last column says what each package is, but
  "smaller and faster" only counts for the subset both sides do.
- **Multi-threading.** Everything is pinned to one thread. OpenCV's
  and NumPy's threaded paths would change the resize story on a
  many-core machine.
- **One machine.** Numbers are from the specific machine recorded in
  the report's Environment section. Ratios travel better than
  absolute times, but re-run it on yours.

## What the report doesn't say (on purpose)

`results/report.html` is the results, not the write-up: it doesn't
explain how the corpus was built, why timing uses best-of-N, or what
went wrong along the way: that's this file. The exceptions are the
machine it ran on (CPU model, core count, RAM, OS, Python version),
which appears in the report's Environment section via
`system_info.py`, and the count of correctness checks that passed
before timing began, since both are facts about those specific numbers
rather than about the methodology in general.
