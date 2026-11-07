# libstb guide

The complete reference for libstb: every Python and C++ call with its rules, encoding and
editing, channels and alpha, SIMD, EXIF orientation, Pillow and DLPack interop, resize
filters, errors, design notes, source layout, and build and test. The short version is in the
[README](../README.md).

## Python

```python
import numpy as np
import libstb as stb

img = stb.Image.open("photo.png")                   # or bytes; also: format=stb.PixelFormat.RGBA, flip=True
img.width, img.height, img.channels                 # 640, 480, 3
img.numpy()                                         # uint8 pixels (H, W, C), no copy; np.asarray(img) works too
np.from_dlpack(img)                                 # DLPack: also torch/jax/cupy.from_dlpack(img), zero-copy
stb.Image.from_dlpack(tensor)                       # and back: a torch/jax/cupy/numpy uint8 array -> Image
pixels = stb.imread("photo.png")                    # just the ndarray (same options as Image.open)
stb.imwrite("out.jpg", pixels, quality=80)          # an ndarray or an Image; format from the extension
small = stb.imresize(pixels, 320, 240)              # resize an ndarray, get an ndarray (no Image needed)
stb.ImageInfo.read("photo.png")                     # header only: (width, height, format)
stb.Image(np.zeros((8, 8, 4), np.uint8))            # from your own uint8 array (no copy, see below)
img.copy()                                          # an independent image (deep copy)

img.write("out.jpg")                                 # format from the extension, default settings
img.write_jpg("out.jpg", quality=80)                # or pick the format and its settings
data = img.to_png(compression=9)                    # -> bytes (to_png / to_jpg / to_bmp / to_tga)

small = img.resize(320, 240)                        # new Image; default Resizer()
small = img.resize(320, 240, "cubic")               # filter by name (see the table below)
stb.Resizer("cubic")                                # same name works on Resizer itself
stb.Resizer(stb.Resizer.Filter.MITCHELL, stb.Resizer.Edge.WRAP, srgb=False)
img.resize(320, 240, stb.Resizer("linear", srgb=False))  # name + other options

font = stb.Font.open("font.ttf")                    # trusted fonts only, see below (right-to-left text is shaped for you)
text = font.render("Hello\nworld", 32)              # RenderedText(bitmap: Image (1 channel), origin_x, origin_y)
text.bitmap.write("hello.png")
# several texts and shapes on one copy of the image (draw_text copies it every call, a canvas once); each item can override the defaults:
c = stb.Canvas(img)                                 # same methods as C++ stb::canvas, same pixels
c.set_font(font, 48).set_color("white")             # defaults for the texts added next
c.add_rect(10, 10, 220, 60).fill("#222").radius(12).stroke(2, "white")   # shapes: add_rect, add_ellipse, add_line
c.add_text("Title", 20, 20)
c.add_text("Sub", 20, 90).color("gray").bg_color("#222").padding(8)   # per text: also .font() .size() .align()
out = c.finish()                                    # the texts are drawn here, in order
centred = img.draw_text("Hello\nworld", font, 32, img.width // 2, 10, align="center")  # x = centre of every line ("left" is the default; "right" too)
labeled = img.draw_text("Hello", font, 32, 10, 10, color=(255, 255, 255))   # new Image with the text drawn on; color: int, (r,g,b), (r,g,b,a) or "red" / "#ff8800"
font.metrics(32), font.advance("A", 32), font.kerning("A", "V", 32), font.measure("Hello", 32)
font.render_glyph("A", 32)                          # Glyph(bitmap, x_offset, y_offset, advance)
atlas = font.make_atlas("ABCabc123", 32, 256, 256)  # Atlas: .image and atlas["A"] -> AtlasGlyph
stb.estimate_text("Hello", 16, family="sans")        # TextSize, no font file needed (sans / serif / mono tables)

stb.iminfo("photo.png")                             # shortcut: ImageInfo.read(...)
```

**Encoding.** Every format has a pair of methods with its own settings, all
defaulted: `to_png(compression=8)`, `to_jpg(quality=90)`, `to_bmp()`,
`to_tga(rle=True)` return the file's bytes, and `write_png(path, ...)`,
`write_jpg(path, ...)`, `write_bmp(path)`, `write_tga(path, ...)` do the same
and write it (only after encoding succeeded, so a failure never leaves a
truncated file). `write(path)` is the shortcut: it picks the format from the
extension (`.png .jpg .jpeg .bmp .tga`, case-insensitive; `ValueError` for
anything else) and uses the defaults. For other settings call `write_*`.

`stb.imwrite(path, image, *, quality=None, compression=None, rle=None)` is
the functional form: it takes an `Image` or a uint8 array, picks the format
from the extension, and passes the options that format has (`quality` for jpg,
`compression` for png, `rle` for tga). An option the format does not have is a
`ValueError`, not silently ignored. `stb.imread(source, *, channels, flip,
max_bytes, orient)` is its counterpart and returns an ndarray.

**Editing.** Every operation returns a new `Image` and leaves the source
alone; `ValueError` on a bad argument, `LimitError` if a result would exceed
2 GiB. Results are exact, checked against Pillow's (`FLIP_*`, `ROTATE_*`,
`TRANSPOSE`, `expand`, `convert`).

```python
img.crop(x, y, width, height)         # must lie inside the image
img.flip_horizontal(); img.flip_vertical()
img.rotate90(turns=1)                 # counter-clockwise quarter turns (like np.rot90), negative = clockwise
img.transpose()                       # swap rows and columns
img.pad(left, top, right, bottom, fill=(255, 255, 255))   # fill: an int, or one value per channel
img.thumbnail(256, 256)               # fit inside the box, aspect ratio kept, never enlarges
img.thumbnail(256, 256, "linear")     # same resizer argument as resize()
```

**Pixel formats and alpha.** A `PixelFormat` says what one pixel holds: `GRAY`,
`GRAY_ALPHA`, `RGB` or `RGBA` (`img.format`; `img.channels` is the count, 1 to 4).
Everywhere a format is taken you can pass the member or its lower case name
(`"gray"`, `"gray_alpha"`, `"rgb"`, `"rgba"`), never a bare number. `img.convert(format)`
goes between them. Gray is 0.299 R + 0.587 G + 0.114 B on the stored
values (no gamma, as in Pillow), a missing alpha is 255, and a dropped alpha is
simply discarded: use `img.flatten(background=(255, 255, 255))` to blend it onto
a colour first. `img.split()` gives one single-channel `Image` per channel and
`Image.merge(channels)` puts 1 to 4 of them back (in any order).
`base.blend(overlay, x=0, y=0, mode="normal", opacity=1.0)` returns a new image with
`overlay` blended on top (with the usual "over" operator and straight alpha for
`mode="normal"`; `"multiply"`, `"screen"`, `"overlay"`, `"darken"`, `"lighten"`, `"add"` and
`"difference"` are the Photoshop modes of those names, and `opacity` scales the overlay's alpha): the overlay may hang over the edges, may have any
channel count (no alpha means opaque), and the result keeps the base's channel
count. Its alpha equals Pillow's `alpha_composite`; the colour is within one level
wherever the pixel is visible (where alpha is almost 0 the colour is ill-defined,
and it does not matter). `Image.open(..., format=f)` converts inside stb, which
can differ from `convert(f)` by a rounding step.

`img.fill(color, x=0, y=0, width=None, height=None)` returns a new image with the whole
image or that rectangle (clipped to the image) overwritten by `color`: an int (gray),
`(r, g, b)`, `(r, g, b, a)` or a name or hex string. `img.alpha()` is the alpha channel as a
gray image (a mask) and `img.with_alpha(mask)` sets or replaces it, so a mask and `blend`
give "show this overlay only here". `Image.hstack(images, gap=0, background=0, align="start")`
and `Image.vstack` put images side by side or one above the other (the shorter ones sit at
the `"start"`, `"center"` or `"end"`; the result has colour and alpha if any input has).
`Canvas.add_image(img, x, y)` does the same blending inside a canvas, with
`.blend(mode)` and `.opacity(o)` on the returned item. In C++, `blend` and `fill` change the
image they are called on (use `copy()` to keep the original); in Python every method returns
a new `Image`.

**SIMD.** The loops that are limited by arithmetic rather than by memory
(`convert`, `composite`, `flatten`) exist in up to three builds that give
bit-identical results and differ only in speed. `stb.simd_backends()` lists
the ones this CPU can run, best first, e.g. `['avx2', 'sse2', 'scalar']`:

- `"avx2"`: AVX2 kernels, picked at run time when the CPU and OS support them
  (x86-64, GCC or Clang builds). The same wheel therefore runs on any x86-64 CPU.
- `"sse2"` (x86-64) or `"neon"` (arm64): the same loops vectorised by the compiler for
  the platform's baseline SIMD (on x86-64, hand-written SSE2 kernels for the gray
  conversions and RGBA over RGB). This is what arm64, MSVC and older x86 CPUs run.
- `"scalar"`: the same loops with auto-vectorisation off, the plain reference. Always
  available; the tests check every other backend against it.

`stb.simd_name()` is the backend in use on the calling thread (the first by
default). To change it from code:

- `with stb.simd("scalar"):` uses a backend for the calling thread only, for the
  length of the block; other threads are not affected and blocks nest. In C++ the
  same is `stb::simd_scope guard("scalar");`. Prefer this in a library or a thread pool.
- `stb.set_simd("sse2")` switches the whole process (`"auto"` restores the
  default).

Both raise `ValueError` (C++: `std::invalid_argument` / `false`) for an unknown or
unavailable name. The environment variable `LIBSTB_SIMD=<name>` chooses the initial
backend (`off` means `scalar`) and is for diagnosis, such as comparing two runs of a
program you do not want to edit; a name that is unknown or unavailable on the CPU
prints one line on stderr and falls back to `auto`. None of this is something you need
to set: the default is the fastest backend. How much each helps depends on the loop. On
a 4 MP image (one core of a shared machine: expect other numbers on yours) `convert` to
gray from RGB or RGBA is about 1.9x to 3x faster than scalar with SSE2 and about the
same with AVX2, RGBA `composite` onto RGB is about 2x faster with either, and RGBA
`composite` onto RGBA is about 2.4x faster with SSE2 and 5x with AVX2. The "sse2" backend
runs hand-written SSE2 kernels for the gray conversions and for RGBA over RGB (where the
compiler's vectoriser struggles with 3-byte pixels), and the compiler-vectorised loops for
the other pairs. Flips, turns, `crop` and `pad` only move bytes and are limited by memory bandwidth, so they have no SIMD
variants: AVX2 would not make them faster.

**Threads.** A call with a lot of work in it shares that work between threads of its own;
a call with little work never starts one. So far this is `resize` (`Image.resize`,
`Resizer.resize`): a thumbnail is made on the calling thread, a photograph is split by output
rows. The rule is cost-based: every extra thread must get a few hundred microseconds of work
(starting one costs a few tens), and the estimate errs on the low side, so a doubtful case
stays on one thread. The pixels never depend on the number of threads (the tests compare
byte for byte). Threads live for the duration of the call only: there is no pool, nothing
keeps running afterwards, and `fork()` is safe.

`stb.thread_count()` is the most threads, the calling one included, that a call on the
calling thread may use. It is a ceiling, not a demand: `set_threads(8)` lets a big image use
eight threads and leaves a small one alone. By default it is automatic: the CPUs the process
may run on (its CPU affinity and, on Linux, a cgroup CPU limit such as `docker run --cpus=2`).

- `with stb.threads(1):` sets the ceiling for the calling thread only, for the length of the
  block (`0` is automatic); other threads are not affected and blocks nest. In C++ the same is
  `stb::thread_scope guard(1);`.
- `stb.set_threads(4)` sets it for the whole process (`0` restores the default).

Both raise `ValueError` (C++: `std::invalid_argument` / `false`) for a negative number. The
environment variable `LIBSTB_THREADS=auto|off|<n>` chooses the initial setting. A program that
already runs one worker per CPU, or calls libstb from a thread pool of its own, should
`set_threads(1)` so that libstb does not add threads on top of those.

**EXIF orientation.** Phone photos are stored sideways and carry an EXIF tag
saying how to turn them; stb ignores it. `Image.open(path, orient=True)` (and
`imread(..., orient=True)`) applies it while decoding, for JPEG and PNG (`eXIf`
chunk); width and height may swap, and `flip=True` is applied after it. It is
off by default so decoding keeps returning the stored pixels, and `iminfo` keeps
reporting the stored size. `stb.exif_orientation(source)` returns the 1..8
value (1 when there is none or the data is unreadable; it never throws on bad
input and is safe on untrusted files) and `img.orient(value)` applies one:

```python
img = stb.Image.open("IMG_0001.jpg", orient=True)     # upright
turn = stb.exif_orientation("IMG_0001.jpg")               # or ask first...
img = stb.Image.open("IMG_0001.jpg").orient(turn)         # ...and apply it yourself
```

Only the tag is read (JPEG `APP1`, PNG `eXIf`); other EXIF data and ICC profiles
are not.

**Pillow.** Pillow is not a dependency: it is imported only when you call something that
needs it, like numpy (`pip install "libstb[pillow]"`; `import libstb` never loads it).

```python
pil = img.to_pil()                 # libstb -> Pillow: L, LA, RGB or RGBA by channel count; L and RGBA share the pixels (no copy)
img = stb.Image.from_pil(pil)      # Pillow -> libstb
```

`from_pil` takes `L`, `LA`, `RGB` and `RGBA` as they are and converts the modes that hold
8-bit colour in another form: `1` to `L`, `P` to `RGB` (`RGBA` if the palette has
transparency), `PA` and `RGBa` to `RGBA`, `La` to `LA`, `RGBX`, `CMYK` and `YCbCr` to `RGB`.
Modes with no 8-bit colour (`I`, `I;16`, `F`, `LAB`, `HSV`) raise `TypeError`: convert them
yourself.

Both directions also work without those methods, because Pillow has neither DLPack nor a
buffer export and speaks NumPy's `__array_interface__`, which `Image` implements both ways:
`PILImage.fromarray(img)` and `stb.Image(pil)`. That path is strict on purpose: it takes
only 8-bit `L`, `LA`, `RGB` and `RGBA` and raises `TypeError` otherwise (a palette or CMYK
image would otherwise be read as if its indices or inks were colours, as `np.asarray(pil)`
does). `Image(x)` accepts any object with `__array_interface__` (a writable contiguous
address is shared and kept alive, like a NumPy array; a read-only or strided one, or data
given as bytes, is copied and bounds-checked; an address cannot be checked, so only hand over
objects you trust). The interface reports a gray image as `(H, W)`, which Pillow requires;
`np.asarray(img)` and `img.numpy()` still give `(H, W, 1)`.

**Resize filters by name.** `Image.resize(w, h, x)` and `Resizer(x)` accept a
`Resizer.Filter`, or a case-insensitive name (`-` and space count as `_`). Any
other option (`edge`, `srgb`, `max_bytes`) is a keyword of `Resizer(...)`.
`Image.resize` also takes a ready-made `Resizer`, or `None` for the default.

| name | filter | note |
| :--- | :--- | :--- |
| `auto`, `automatic`, `default` | `DEFAULT` | Catmull-Rom when enlarging, Mitchell when shrinking |
| `nearest`, `point` | `POINT` | nearest neighbour |
| `linear`, `bilinear`, `triangle` | `TRIANGLE` | |
| `cubic`, `bicubic`, `catmull_rom` | `CATMULL_ROM` | sharp; the PIL/OpenCV meaning of "cubic" |
| `bspline`, `cubic_bspline` | `CUBIC_BSPLINE` | smooth, slightly blurry; scipy's meaning of "cubic" |
| `mitchell` | `MITCHELL` | good all-round compromise |
| `box`, `area` | `BOX` | |

An unknown name raises `ValueError` listing the valid ones. There is no
`lanczos`: stb_image_resize2 does not have it.

**Fonts: prefer trusted files.** libstb's font reader (`src/font/truetype/`) checks every read against
the file's bounds and caps composite-glyph recursion, so a damaged font gives wrong glyphs rather than
out-of-range reads, but unlike `stb_image` it has not been fuzzed. Load your own or vetted system fonts
rather than user uploads. (Images: the decoders are stb_image's algorithms, which are fuzzed upstream, and libstb adds dimension and
size limits; the restructuring kept their output identical on a 640-file corpus including damaged files.)

**Right-to-left text (Persian, Arabic, Urdu, Hebrew).** `draw_text`, `measure`, `render` and the
canvas shape and reorder it for you: pass the text in the order you type it, no pre-processing with
`arabic-reshaper` or `python-bidi`. The line is ordered with the Unicode bidirectional algorithm
(UAX #9: the paragraph direction is that of the first strong letter, Latin words and numbers inside
Persian text keep their order, brackets are mirrored), ZWNJ and ZWJ are honoured, each `\n` line is
shaped on its own, and text with no right-to-left letter is drawn exactly as before.

Letter shapes come from the font. If it has a GSUB table for Arabic script (every modern Persian
and Arabic font does), libstb runs it the way an OpenType shaper does: the `isol`, `init`, `medi`,
`fina` shapes, `ccmp`, `rlig` (lam-alef), `calt` and `liga` (so "الله" is the one glyph the font
draws for it). Lookup types 1 to 7 are read (single, multiple, alternate, ligature, context,
chaining context, extension), with lookup flags and GDEF. A font without GSUB but with the Arabic
Presentation Forms in its `cmap` is shaped through those, and a form the font lacks falls back to the
plain letter. On Vazirmatn, Noto Naskh Arabic and Noto Sans Arabic the glyphs are identical to
HarfBuzz's on 18,000 random strings (`Font.shape_glyphs` returns them, for diagnostics). Speed is
about 0.2 microseconds per character, and the GSUB table is parsed the first time right-to-left text
is drawn with a font (about 0.1 ms), not when the font is opened.

What is not done: GPOS mark and anchor positioning, and every GPOS lookup but pair adjustment. Kerning
is the font's GPOS pair adjustment (lookup type 2, glyph pairs and class pairs, the advance of the first
glyph) if the font has a GPOS table, else its legacy `kern` table. Marks (harakat) are placed by a
simple rule rather than the font's anchors: centred on the ink of their letter and stacked above it
(below for kasra and similar signs, shadda first), with no room of their own. That looks like the
real thing on ordinary vowelled text but not pixel for pixel, and text set in a font that depends on
GPOS for its shapes (some Nastaliq fonts) will look wrong. GSUB lookups of type 8 (reverse chaining)
are skipped, only the default language system of the script is used (no `locl` per language), and
`font.advance` and `font.kerning` work on one code point and do no shaping. Not checked: Hebrew,
Urdu-specific letters, text with explicit bidi controls (embeddings and isolates are ignored). A font
is untrusted input: damaged GSUB data cannot read outside the file, and the work and the growth of
the glyph run are limited in proportion to the text (50,000 damaged fonts were shaped under
AddressSanitizer without a crash).

Errors mirror the C++ hierarchy: `stb.Error` (a `RuntimeError`) with
`DecodeError`, `EncodeError`, `LimitError` below it, so a single
`except stb.Error` catches every libstb failure. Bad arguments raise
`ValueError`, unreadable files `OSError` (`FileNotFoundError`, `PermissionError`,
... as the OS says). Images whose decoded size would
exceed `max_bytes` (default 512 MiB) are rejected from the header, before any
pixel memory is allocated.

`stb.Image` is `stb::image` itself, not a wrapper: `open`, `resize`, `to_*` and
`write*` are the C++ members, run without the GIL. `img.numpy()` / `np.asarray(img)`
are numpy views of its pixels (kept alive by the view); `memoryview(img)` is the
same zero-copy view as a plain 3-D `memoryview` (numpy reads it too), and
`img.tobytes()` is a copy, both without numpy.

**DLPack.** `Image` implements `__dlpack__` / `__dlpack_device__`, so
`np.from_dlpack(img)`, `torch.from_dlpack(img)`, `jax.numpy.from_dlpack(img)`
and `cupy.from_dlpack(img)` share the pixels with no copy and no framework
import on libstb's side. The layout is fixed: `uint8`, shape `(height, width,
channels)`, on the CPU (`dl_device` other than the CPU is a `BufferError`).
The view keeps the image alive. It is writable with numpy 2.1 or newer (older
numpy, the last one on Python 3.9 being 2.0.2, imports every DLPack view
read-only and has no `copy=` argument; `img.numpy()` is always writable).
Asking for `copy=True` gives an independent copy. The other way round, `stb.Image.from_dlpack(x)` takes any
DLPack object holding `uint8` pixels of shape `(H, W)` or `(H, W, 1..4)` (a
torch, jax, cupy or numpy array, another `Image`): a writable C-contiguous CPU
array is shared, anything else is copied, and `copy=True` always copies.
(`Image(x)` accepts the same objects; `from_dlpack` additionally insists on
`__dlpack__`, so a stray `bytes` is a clear `TypeError`.) For a different layout or dtype (CHW, float32), convert on
the framework side, e.g. `torch.from_dlpack(img).permute(2, 0, 1).float() / 255`.

Pixels are never copied behind your back. `Image(array)` uses a writable,
C-contiguous uint8 array in place, so the image and the array share their
pixels (changes show on both sides, and the image keeps the array alive). Any
other array (read-only, or strided like `arr[::-1]`) cannot be shared and is
copied. `Image.open` takes ownership of stb's own buffer, no copy either. When
you want an independent image, say so: `img.copy()` (also `copy.copy` and
`copy.deepcopy`).

### Vector outlines

An `Outline` is a shape made of lines and quadratic or cubic curves, in float coordinates (x to the right, y
down). Build it with chained calls, then draw it on an image (every `Image` method returns a copy) or on a
`Canvas`:

```python
p = stb.Outline().move_to(10, 10).line_to(90, 10).quad_to(120, 50, 90, 90).close()
p.add_rounded_rect(20, 20, 60, 40, 8)           # whole shapes: add_rect, add_rounded_rect, add_ellipse, add_circle
p.add_outline(other, stb.Matrix.translation(200, 0))

img.fill_outline(p, "#09f", rule="even_odd")        # rule: "non_zero" (default) or "even_odd"
img.stroke_outline(p, "white", 3, cap="round", join="bevel", miter_limit=4, dashes=[8, 4], dash_offset=2)
img.fill_outline(p, (255, 0, 0, 128), matrix=stb.Matrix().translate(40, 40).rotate(0.3), blend="multiply", opacity=0.8)

c = stb.Canvas(img)                              # or several outlines on one copy, with texts and shapes
c.add_outline(p).fill("#09f").stroke(3, "white").dash([8, 4]).opacity(0.9)
mask = p.fill_mask(img.width, img.height)        # the coverage as a gray Image, for your own colours or gradients
```

What to know:

- Pixel (0, 0) covers [0, 1) x [0, 1): a coordinate is the corner of a pixel.
- `Matrix` follows the SVG convention (`x' = a*x + c*y + e`); `translate`, `scale`, `rotate` and `shear` apply
  to a point first, like the SVG `transform` attribute, and chain.
- A stroke is centred on the line and its width is in outline units, scaled by the matrix (the mean of the two
  axes). Dashes are cut in outline units before the matrix. The geometry is computed in pixels at 1/64 pixel
  precision, so tiny outlines (a few pixels across in outline units) should be scaled up with `matrix=` rather than
  drawn at their own size.
- `dashes` alternate "on" and "off" lengths; an odd count repeats the list once more with on and off swapped.
  A negative entry, or a sum of 0, gives a solid line. `Outline.dashed` gives the same cut as a new outline.
- NaN or infinite coordinates draw nothing; coordinates beyond 1,000,000 are limited to that.
- The rasterizer is a port of the FreeType smooth rasterizer and stroker: the pixels are the same on every
  platform, and are tested against plutovg (see `NOTICE.md` and `tests/tools/raster_reference/`).

## Command line

```
libstb --version
libstb --cmake-dir                 # also --sdk-dir, --include-dir, --lib-dir
libstb info photo.jpg more.png [--json]
libstb convert photo.jpg out.png --resize 320x     # a missing side keeps the aspect ratio
libstb convert in.png out.jpg --quality 85 --pixel-format gray --orient
libstb zlib compress data.bin data.bin.z -l 9
libstb zlib decompress data.bin.z -          # "-" is stdin or stdout
```

`python -m libstb` does the same. The output format of `convert` comes from the extension of the
destination (`.png .jpg .jpeg .bmp .tga`); an option the format does not have is an error, and an
existing destination is overwritten. Exit status: 0 success, 1 a file could not be read or written,
2 a usage error.

## C++ / CMake

```cmake
# after `pip install libstb`
execute_process(COMMAND ${Python3_EXECUTABLE} -m libstb --cmake-dir
    OUTPUT_VARIABLE libstb_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
find_package(libstb CONFIG REQUIRED)
target_link_libraries(app PRIVATE libstb::libstb)
```

The wheel carries the C++ SDK as an ordinary install prefix, `libstb/sdk/` (`include/`, `lib/` and
`lib/cmake/libstb/` inside it; `libstb --sdk-dir` / `libstb.get_sdk_dir()` print it), so
`-DCMAKE_PREFIX_PATH=<sdk>` is an alternative to setting `libstb_DIR`.

The C++ namespace and headers are `stb` (`<stb.h>`, `<stb/*.hpp>`); the CMake
target (`libstb::libstb`) and the Python package (`libstb`) keep the project name.

`libstb::libstb` is a shared library (`libstb.so`, `libstb.dylib`, `stb.dll`). The wheel carries
exactly one copy, in `libstb/sdk/lib` (on Windows `libstb/__init__.py` registers that folder with
`os.add_dll_directory` before loading the extension), and the Python extension and your program
both load it, so there is one engine per process (on musllinux the Python extension has its own
static copy of the core instead: a dynamic libstdc++ crashes on C++ exceptions there). Your CMake target
gets an rpath to it, so the program runs without setting `LD_LIBRARY_PATH`. Windows has no rpath:
copy `stb.dll` next to your executable (`$<TARGET_FILE:libstb::libstb>` in a post-build step) or put
its folder on `PATH`. Only what the public
headers mark `STB_API` is exported (`stb/export.hpp`: `dllexport`/`dllimport` on Windows, default
visibility elsewhere); decoders, the font reader and the SIMD kernels stay hidden.

```cpp
#include <stb.h>

stb::image img = stb::image::open("in.png");        // throws stb::error subclasses
img(0, 0, 1) = 255;                                       // unchecked pixel access
stb::image dup = img.copy();                              // image is move-only: copies are explicit
img.write("out.jpg");                                      // format from the extension, default settings
img.write_jpg("out.jpg", 80);                             // or pick the format and its settings
std::vector<std::uint8_t> bytes = img.to_png(9);          // to_png / to_jpg / to_bmp / to_tga -> file bytes

stb::image small = img.resize(320, 240);              // default resizer
stb::image crisp = img.resize(320, 240, "cubic");      // filter by name, or by enum:
stb::image soft  = img.resize(320, 240, stb::resize_filter::mitchell);

stb::resize_options o;                                 // full control: filter, edge, srgb, max_bytes
o.edge = stb::resize_edge::wrap;
stb::resizer r(o);                                     // also: resizer("cubic"), resizer(resize_filter::box)
stb::image tile = img.resize(320, 240, &r);            // nullptr = default resizer; r.resize(img, w, h) works too

stb::image part  = img.crop(10, 10, 200, 100);        // geometry: each returns a new image
stb::image turned = img.rotate90();                    // counter-clockwise; also flip_horizontal/vertical, transpose
stb::image framed = img.pad(8, 8, 8, 8, {255, 255, 255, 255});   // fill[c] = value of channel c
stb::image thumb = img.thumbnail(256, 256);            // fits the box, never enlarges (+ resize's filter arguments)
stb::image gray = img.convert(stb::pixel_format::gray);  // gray, gray_alpha, rgb, rgba; flatten() blends alpha onto a colour
std::vector<stb::image> planes = img.split();          // stb::image::merge(planes) is the inverse
stb::image over = img.copy().blend(logo, 16, 16);      // blend() changes the image it is called on: copy() first to keep it
img.blend(shadow, 0, 0, stb::blend_mode::multiply, 0.6f);   // modes and opacity (blend_mode: normal, multiply, screen, ...)
img.fill("red", 10, 10, 50, 20);                       // overwrite a rectangle (no x, y, w, h: the whole image)
stb::image mask = logo.alpha();                        // the alpha channel as a gray image; with_alpha(mask) sets it
stb::image strip = stb::image::hstack({&a, &b, &c}, 8, "white", stb::align::center);   // also vstack

stb::load_options lo;                                  // EXIF orientation: JPEG APP1 / PNG eXIf; lo.format = pixel_format::rgb converts
lo.orient = true;                                      //   upright while decoding (then `flip` goes after it)
stb::image photo = stb::image::open("IMG_0001.jpg", lo);
int turn = stb::exif_orientation(bytes.data(), bytes.size());   // 1..8, 1 if none; never throws
stb::image upright = raw.orient(turn);                 // or apply it yourself

stb::font f = stb::font::open("font.ttf");          // cheap to copy, thread-safe
stb::text_bitmap t = f.render("h\xC3\xA9llo", 32);      // UTF-8 in, 1-channel image out
stb::image labeled = f.draw_text(img, "Hello", 32, 10, 10, {255, 255, 255});  // text onto a copy; colours: {r, g, b[, a]}, "red", "#ff8800", "red"_c, 0xff8800_rgb
stb::image centred = f.draw_text(img, "Hello", 32, img.width() / 2, 10, stb::color(), stb::text_align::center);
stb::canvas c(img);                                      // <stb/draw.hpp>: one copy, many texts
c.set_font(f, 48).set_color("white");                    // defaults for the texts added next
c.add_rect(10, 10, 220, 60).fill("#222").radius(12).stroke(2, "white");   // shapes: add_rect, add_ellipse, add_line
c.add_text("Title", 20, 20);
c.add_text("Sub", 20, 90).color("gray").bg_color("#222").padding(8);               // per text: also .font() .size() .align()
stb::image out = c.finish();                             // the texts are drawn here, in order
stb::atlas atlas = f.make_atlas(U"abc", 32, 256, 256);
stb::text_size est = stb::estimate_text("Hello", 16);    // no font file: fixed sans / serif / mono tables
```

**Font files.** `font::open` and `font::from_memory` read TrueType (`.ttf`), OpenType with CFF outlines (`.otf`),
collections (`.ttc`, the face is the `index` argument) and WOFF 1.0 (`.woff`). A WOFF is unpacked to the font it
holds, in memory, with libstb's own zlib, and has one face. WOFF2 needs Brotli, which libstb does not have: it throws
`decode_error` ("WOFF2 fonts are not supported"); convert it first (for example with `fonttools`). A damaged WOFF is a
`decode_error`, and one that would unpack to more than 512 MiB is a `limit_error`.

**Glyph outlines (vector rendering).** `font` also gives the outline of any glyph as outline commands, in
font units, so a vector renderer can draw text without rasterising it first. The glyph indices are the
font's own, the ones `shape_glyphs` returns; an index past `glyph_count()` is an empty glyph, not an
error. TrueType outlines come as `quad_to`, CFF ones as `cubic_to` (they are never converted), every
contour is `move_to ... close`, and `y` points up like the font.

```cpp
const stb::font_design_metrics dm = f.design_metrics();      // units_per_em, ascent, descent, line_gap, bbox
const float scale = font_size / float(dm.units_per_em);       // font units -> output units
std::vector<stb::outline_command> cmds;                          // reused: outline() clears it, keeps its capacity
float pen = 0;
std::uint32_t prev = 0;
for (char32_t cp : codepoints) {
    const std::uint32_t g = f.glyph_index(cp);                // 0 = .notdef
    if (prev) pen += f.glyph_kerning(prev, g, stb::kerning_mode::kern_table) * scale;
    f.outline(g, cmds);
    for (const stb::outline_command& c : cmds) {                 // c.p[0 .. stb::point_count(c.op) - 1] are used
        switch (c.op) {
            case stb::outline_op::move_to:  /* c.p[0] */ break;
            case stb::outline_op::line_to:  /* c.p[0] */ break;
            case stb::outline_op::quad_to:  /* control c.p[0], end c.p[1] */ break;
            case stb::outline_op::cubic_to: /* controls c.p[0], c.p[1], end c.p[2] */ break;
            case stb::outline_op::close:    break;
        }
    }
    pen += f.glyph_info(g).advance * scale;                   // advance, lsb and the ink box, in font units
    prev = g;
}
const int faces = stb::font::face_count("fonts.ttc");        // 1 for a .ttf or .woff, the number of faces for a .ttc
```

**Shaped text as data.** `font::shape` does the layout work `draw_text` does, but returns it instead of drawing it:
the glyphs in visual order (left to right), in font units with y up, each with its pen movement and where it came from.

```cpp
stb::shape_options opt;                     // direction: automatic | ltr | rtl, kerning: kerning_mode, ligatures: bool
opt.direction = stb::text_direction::rtl;   // e.g. a Persian label that starts with a Latin word
const std::string text = "\u0633\u0644\u0627\u0645 world\nsecond line";
float pen = 0;
for (const stb::shaped_glyph& g : f.shape(text, opt)) {
    if (text[g.cluster] == '\n') {            // a line break: glyph 0, no advance
        pen = 0;                              // and y moves down by ascent - descent + line_gap
        continue;
    }
    // draw outline(g.glyph) at (pen + g.x_offset, g.y_offset) * scale
    pen += g.x_advance;                       // kerning is already in x_advance; a mark has x_advance 0
    // g.cluster: byte offset in the UTF-8 text of the first character of this glyph (selection, caret, links)
}
```

`cluster` follows each glyph through joining, ligatures, mirrored brackets and the bidirectional reordering, so
in a right-to-left run the offsets go down as the glyphs go right, and a ligature (lam + alef, the Allah glyph)
has the offset of its first letter. `'\n'` is a glyph 0 with its own cluster and no advance; `'\r'` and the invisible
bidi controls of a right-to-left line have no glyph. `ligatures = false` stops only the optional ligature
features (`liga`, `clig`): lam + alef and the joining shapes are required and stay. Marks are placed from the
ink boxes (centred on the letter before them, stacked above or below), not from the GPOS mark attachment, so
they can sit up to a pixel from where `draw_text`, which snaps them to its pixel grid, puts them. As in
`draw_text`, only right-to-left scripts go through the font's GSUB table so far; other text is one glyph per
character.

`kerning_mode::automatic` (the default) is what `kerning()` and text rendering use: GPOS pair
adjustment if the font has GPOS, else the legacy `kern` table. `kern_table` looks at the `kern` table
alone, for code that must match a renderer that ignores GPOS; `none` is no kerning.

**From Python.** The same glyph-index API is on `libstb.Font`, in font units with y up, so a vector renderer can be
written in Python. The glyph numbers are the font's own, an index past `glyph_count` is "nothing" (an empty outline,
zero metrics), and a negative index or one above 2**32 - 1 is a `ValueError`.

```python
font = stb.Font.open("fonts.ttc", index=0)       # stb.Font.face_count("fonts.ttc") -> how many faces
font.glyph_index("A")                            # the font's glyph, 0 (.notdef) if it has none; font.glyph_count
font.glyph_info(g)                               # GlyphMetrics(advance, lsb, x_min, y_min, x_max, y_max, has_outline)
font.design_metrics                              # DesignMetrics(units_per_em, ascent, descent, line_gap, x_min, ...)
font.outline(g)                                  # [("M", x, y), ("L", ...), ("Q", ...), ("C", ...), ("Z",)] = Outline commands
font.glyph_kerning(left, right, "automatic")     # or "kern_table" or "none"

s = size / font.design_metrics.units_per_em      # font units -> pixels
pen = 0.0
for g in font.shape("\u0633\u0644\u0627\u0645 world", direction="auto", kerning="automatic", ligatures=True):
    # ShapedGlyph(glyph, cluster, x_advance, x_offset, y_offset); glyph 0 is a line break
    if g.glyph:
        m = stb.Matrix.translation(x + (pen + g.x_offset) * s, baseline).scale(s, -s)   # Outline's y points down
        img = img.fill_outline(stb.Outline(font.outline(g.glyph)), "black", matrix=m)
    pen += g.x_advance
```

`Outline(font.outline(g))` takes the commands as they are. The cheap calls (`glyph_index`, `glyph_info`,
`outline`, `glyph_kerning`) keep the GIL: each takes microseconds, less than letting go of it and taking it back
costs. `shape` lets go of it for a text of 512 UTF-8 bytes or more (about 100 microseconds), so other threads run
while a long text is shaped; a `Font` is immutable, so many threads can share one.

Design: stb never appears in a public header. Where a class would hold stb
state, it is hidden behind a pimpl: `font` keeps the font bytes and the
parsed face in an opaque `font::impl` (shared and immutable, so copies are
cheap and threads can share one). `image` and `resizer` carry no stb state at
all (plain ints/enums), so a pimpl there would only add indirection. `image` is
move-only: its pixels are a `shared_ptr<uint8_t>` whose deleter (or aliased
owner) decides who frees them, so the same class holds memory it allocated,
stb's decode buffer (adopted, not copied) or memory somebody else owns
(`image::wrap`, which is how a numpy array becomes an image without a copy).
A copy is always spelled `copy()`. The four output formats are a closed set, so there
is no encoder class hierarchy: each format is a `to_*` / `write_*` pair on
`image` with its own defaulted settings; the format code itself is in `src/codec/` and knows nothing
about `image`.
The editing operations are plain loops over the pixel buffer in `src/image/ops.cpp`
(no stb involved; the arithmetic-heavy kernels are `src/image/kernels.hpp`, instantiated
three times - AVX2, baseline SIMD, and `kernels_scalar.cpp` with auto-vectorisation off -
and the backend is chosen in `src/image/simd.cpp`; build with `-DSTB_NO_SIMD_DISPATCH` to leave
the AVX2 ones out) and the EXIF reader is `src/image/exif.cpp`: it looks only at the
Orientation tag and checks every offset against the buffer.
Runtime failures derive from
`stb::error` (`decode_error`, `encode_error`, `limit_error`, `io_error`);
programmer errors throw `std::invalid_argument`.

### Vector outlines

`<stb/outline.hpp>` has `stb::matrix` and `stb::outline`; `<stb/raster.hpp>` draws them:

```cpp
stb::outline p;
p.move_to(10, 10).line_to(90, 10).quad_to(120, 50, 90, 90).close();
p.add_rounded_rect(20, 20, 60, 40, 8);                       // add_rect, add_ellipse, add_circle
p.add_commands(font.outline(glyph), stb::matrix::translation(10, 40));   // a glyph outline goes straight in

stb::fill(img, p, stb::color("#09f"), stb::fill_rule::even_odd);
stb::stroke_style st{3.f};                                   // width in outline units
st.cap = stb::line_cap::round; st.join = stb::line_join::bevel; st.dashes = {8, 4};
stb::draw_options o;                                         // matrix, blend mode, opacity
o.transform = stb::matrix().translate(40, 40).rotate(0.3f);
stb::stroke(img, p, stb::color("white"), st, o);

// Your own compositing: coverage spans (y, x, len, coverage 1..255), top to bottom, left to right.
stb::rasterize_fill(p, stb::fill_rule::non_zero, {}, stb::clip_rect{0, 0, img.width(), img.height()},
                    [&](stb::span_view spans) { for (const stb::span& s : spans) { /* ... */ } });

stb::canvas c(img);
c.add_outline(p).fill("#09f").stroke(3, "white").dash({8, 4});  // with texts and shapes, drawn in order
```

`fill` and `stroke` blend through `image::blend` (a layer the size of the outline's box), so the blend modes and
opacity work as everywhere else. The coordinate, matrix, dash and precision rules are the ones listed
under "Vector outlines" in the Python section above. The geometry is fixed-point (`src/draw/raster/`), so there is
no platform-dependent rounding.

### Logging

`<stb/logging.hpp>` is a small copy of Python's `logging`: the same names, the same level numbers, the same tree of
dotted logger names. libstb itself logs through `libstb` and `libstb.<part>` (`libstb.decode`, `libstb.encode`, `libstb.resize`,
`libstb.zlib`, `libstb.font`, `libstb.raster`, `libstb.simd`, `libstb.threads`).

**What libstb logs.** Nothing at the default level, unless it changed something you asked for. At `debug` it says
what it decided or did, one record per call: the backend chosen, the threads a resize will use, the size of a
decode, an encode or a zlib call, the glyphs a font lacks. At `warning` it says when it deviated from the request:
an ignored `LIBSTB_SIMD` or `LIBSTB_THREADS`, a NaN or infinite coordinate (the outline is ignored), a coordinate
moved to the 1,000,000 limit, a dash pattern it cannot use (the line is solid), a thread it could not start. A failure
is an exception and never also a record. The rules and the full table are in
[ADR-0003](adr/0003-logging-policy.md). A disabled `debug` call costs one level check; with the Python bridge on
and Python's level above `debug`, a logged call costs about 0.6 microseconds (see [ADR-0002](adr/0002-python-logging-bridge.md)),
which only matters for tiny images in a hot loop: `libstb.logging_bridge(False)` removes it.

```cpp
auto& lg = stb::logging::get_logger("libstb.simd");           // logging.getLogger("libstb.simd")
lg.warning("backend %s is not available, using %s", want, got);   // printf style: stb::sformat, checked by -Wformat

stb::logging::basic_config(stb::logging::level::info);        // logging.basicConfig: a handler on stderr
auto& libstb = stb::logging::get_logger("libstb");
libstb.set_level(stb::logging::level::debug);
auto& fh = libstb.add_handler(stb::logging::file_handler("stb.log"));   // the logger owns it: no shared_ptr to write
fh.set_formatter(stb::logging::formatter("%(asctime)s [%(levelname)-8s] %(name)s: %(message)s"));
libstb.remove_handler(fh);
```

- **Levels** are `debug` 10, `info` 20, `warning` 30, `error` 40, `critical` 50 (any number in between works), and
  `notset` for "use the parent's". A logger's effective level is the first one set going up the tree; the root
  is `warning`. `LIBSTB_LOG=debug|info|warning|error|critical|<number>` sets the level of `libstb` at start.
- **Handlers** are `stream_handler` (stderr by default, colored by level on a terminal), `file_handler`,
  `callback_handler` (a function that gets each `record`), `null_handler`, or your own class derived from
  `stb::logging::handler` that overrides `emit`. A record goes to the logger's handlers and then, while
  `propagate()` is true (the default), to those of its parents. A handler has its own level, a formatter and filters.
- **Operating system logs** (`<stb/logging_os.hpp>`): `syslog_handler` (POSIX), `android_handler` (logcat),
  `unified_log_handler` (macOS and iOS), `debug_string_handler` (Windows debugger output) and `event_log_handler`
  (Windows Event Log). A class exists only on the platforms that have its facility, and `STB_LOG_HAS_SYSLOG`,
  `STB_LOG_HAS_ANDROID`, `STB_LOG_HAS_UNIFIED`, `STB_LOG_HAS_DEBUGSTRING` and `STB_LOG_HAS_EVENTLOG` (1 or 0) say which, so
  portable code writes `#if STB_LOG_HAS_SYSLOG ... #endif`. They map the levels to the five severities every system
  has, and their default pattern leaves out what the system adds itself (time, level, process). syslog has one
  connection per process, so use one `syslog_handler` per program.

  ```cpp
  #if STB_LOG_HAS_SYSLOG
  stb::logging::get_logger("libstb").add_handler(stb::logging::syslog_handler("myapp"));
  #elif STB_LOG_HAS_ANDROID
  stb::logging::get_logger("libstb").add_handler(stb::logging::android_handler());  // tag = logger name: adb logcat libstb.simd:D *:S
  #endif
  ```
- **Formatter** patterns use Python's `%(field)s` syntax: `asctime`, `created`, `msecs`, `levelname`, `levelno`,
  `name`, `message`, `thread`, `process`, with widths and alignment (`%(levelname)-8s`).
- **No handler anywhere:** warnings and worse still reach stderr as `<logger>: <message>` (Python's "last resort"),
  so the messages libstb prints today (a bad `LIBSTB_THREADS` or `LIBSTB_SIMD`) are not lost, and a program can
  silence or redirect them with a handler or a level.
- **Python:** `import libstb` connects libstb to Python's `logging`: the records of `libstb`, `libstb.simd`,
  `libstb.threads` arrive in `logging.getLogger(<that name>)` with the same level numbers, so
  `logging.basicConfig(level=logging.DEBUG)` or `logging.getLogger("libstb").setLevel(...)` is all a program needs.
  With no handler configured, Python's own last resort prints warnings and worse. `libstb.logging_bridge(False)`
  disconnects it (libstb then writes as described above) and `logging_bridge(True)` reconnects it. If `LIBSTB_LOG`
  is set it keeps deciding what libstb emits, whatever the Python level.
- **Threads and failures:** every call is thread safe, a handler writes one record at a time, and logging never
  throws (an exception from a handler is swallowed, as in Python). The loggers live in the library and are never
  destroyed, so logging during program exit is safe.

Or `cmake --install build` and `find_package(libstb)` as usual.

## What is from stb, and what is not

libstb is not a wrapper around stb. Its image decoders, encoders, resizer and font reader are
**derived from stb's algorithms and rewritten as libstb's own code**; everything around them is
new. Each derived file says so in its header, and [`NOTICE.md`](../NOTICE.md) has the lineage and
the license (stb is public domain or MIT; libstb uses the MIT option).

**Descended from stb** (same algorithms, reorganised: one file per format or topic, classes for
stateful code, bounds-checked reads, no global state):

| Area | From | Lives in |
| :--- | :--- | :--- |
| Decoders: PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PIC, PNM, and the inflater | `stb_image` v2.30 | `src/codec/decode/` |
| Encoders: PNG, JPEG, BMP, TGA, and the zlib compressor | `stb_image_write` v1.16 (the JPEG encoder is based on jo_jpeg) | `src/codec/encode/` |
| Resizer, all filters | `stb_image_resize2` v2.18 | `src/image/resize/` |
| Font file, glyph outlines (TrueType and CFF), rasterizer, atlas packer | `stb_truetype` v1.26, `stb_rect_pack` v1.01 | `src/font/truetype/` |

**Written for libstb** (no stb counterpart):

| Area | What |
| :--- | :--- |
| Public API | `stb::image`, `stb::font`, `stb::resizer`, `stb::canvas`, typed errors, explicit ownership; the whole Python package (nanobind) |
| Editing | crop, pad, flips, rotation, thumbnail, channel conversion, flatten, `blend` modes, `fill`, alpha masks, `hstack` / `vstack` |
| SIMD kernels | `convert`, `flatten` and `blend` with AVX2, SSE2 and scalar backends, picked at run time (`src/image/`) |
| EXIF | orientation reader for JPEG and PNG, written for untrusted input |
| Text | GSUB shaping, OpenType layout, bidi (UAX #9), Arabic joining, `draw_text`, `estimate_text` (`src/font/shaper/`) |
| Drawing | colours, shapes and the canvas (`src/draw/`) |
| zlib | the public `zlib_compress` / `zlib_decompress` API on top of the codec code |
| Tooling | the command line, the CMake package, the tests and the benchmarks |

**What changed on the inherited parts:**

- **Faster encoders.** The PNG deflate uses 4-byte hash chains, lazy matching and dynamic Huffman
  blocks; the JPEG encoder has a buffered bit writer, a vectorisable DCT and an SSE2 colour
  conversion. The compressor measured 1.2x to 1.9x faster than zlib level 6 with 0.4% to 3.3%
  larger output on the project's own samples (source code, a binary, image pixels). Measure on
  your data.
- **Inflate is not faster.** The decompressor is the stb_image-derived inflater: about 2.5x
  slower than zlib on very compressible text, so use zlib if inflate speed is what you need.
- **Safer.** Header-based size limits, bounds-checked reads in the font reader (stb_truetype did
  not check), and typed errors. The font reader has not been fuzzed: prefer fonts you trust.
- **Refactor checks.** `tests/tools/snapshot.py` and `decode_snapshot.py` compare a new build
  against an old one operation by operation, and `jpeg_vs_upstream.py` compares the JPEG encoder
  with upstream.

Performance numbers against Pillow and OpenCV, including where libstb is slower (JPEG encoding
is one), are in [`benchmarks/`](../benchmarks/README.md). They depend on the machine, so run them
on yours.

## Layout (mirrors httpp / ctoon)

```
include/stb.h               umbrella header, owns LIBSTB_VERSION_* (single source of truth)
include/stb/*.hpp           public API (error, util, color, image, outline, raster, font, rtl, draw, zlib, logging); never includes stb
src/util/                   file I/O helpers, UTF-8, logging (logging.cpp)
src/image/                  image, editing ops, resize, EXIF, SIMD kernels and their dispatch
src/codec/                  every format: decode/ (one file per format, from stb_image), encode/ (PNG / JPEG / BMP /
                            TGA encoders, deflate)
src/zlib/                   the public zlib API (on top of src/codec)
src/font/                   fonts, text rendering, atlases; truetype/ (font file, CFF, rasterizer, packer) and
                            shaper/ (arabic, bidi, gsub, otl)
src/draw/                   shapes, colours, outlines; raster/ holds the scan converter, stroker and fixed-point math
src/image/resize/           the resizer engine, split by topic from stb_image_resize2 (see its README)
src/bindings/python/        nanobind module (bind_*.cpp, one per area) + the `libstb` Python package
cmake/                      DynamicVersion, Startup, Optimize, package config
tests/{cpp,python,cmake}    unit tests + find_package consumer test, all run by ctest
tests/tools/                dev checks that are not tests (snapshot.py, jpeg_vs_upstream.py, zlib_fuzz.py,
                            raster_reference/ = plutovg scenes and hashes for the rasterizer test)
tests/data/                 synthetic test font (solid-rectangle glyphs) + the script that makes it
```

Each stb header is compiled with `STB_*_STATIC`, so no `stbi_*` symbol leaks
from `stb_core`: it links fine next to your own copy of stb.

## Build & test

```sh
pip install -r requirements-dev.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/python/test_memory.py` repeats each operation on a real image and fails if memory
allocated by it (native allocations included) is still alive afterwards, and checks that
`Image(array)`, `from_dlpack`, `crop` and `convert` do not make hidden copies. The limits
are on by default whenever `pytest-memray` can be imported (it is in the `test` extra on the platforms that have a wheel for it; `tests/python/conftest.py` switches
`--memray` on; `LIBSTB_NO_MEMRAY=1` turns it off), and the module is skipped when the plugin is
missing, as on Windows.

Wheels: abi3 (`cp312-abi3`, covers 3.12+) plus one wheel each for cp39-cp311;
see `.github/workflows/wheels.yml`. Free-threaded builds are not supported.

## License

MIT for libstb. The code derived from stb is MIT / public domain (see `NOTICE.md`).
