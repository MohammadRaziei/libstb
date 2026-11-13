# NOTICE

libstb is MIT licensed (see LICENSE.txt). Its image decoders, encoders, zlib compressor, font reader,
glyph rasterizer, rectangle packer and resizer are derived from Sean Barrett's stb libraries
(https://github.com/nothings/stb) and from the contributors acknowledged in them. stb is available
under two licenses, public domain or MIT; libstb uses the MIT option, reproduced below.

| libstb | derived from | stb version |
| :--- | :--- | :--- |
| `src/codec/decode/` (decoders and the inflater) | `stb_image.h` | v2.30 |
| `src/codec/encode/` (`png.cpp`, `jpeg.cpp`, `bmp_tga.cpp`, `deflate.cpp`) | `stb_image_write.h` | v1.16 |
| `src/image/resize/` | `stb_image_resize2.h` | v2.18 |
| `src/font/truetype/` | `stb_truetype.h`, `stb_rect_pack.h` | v1.26, v1.01 |
| `src/draw/raster/` (the path rasterizer and stroker) | FreeType `ftgrays.c`, `ftstroke.c`, `fttrigon.c`, through plutovg | plutovg 1.x |

The JPEG encoder is based on Jon Olick's jo_jpeg.cpp (public domain / MIT).

No stb header is vendored: the algorithms are the same, the code is reorganised (one file per format and
topic, classes for stateful decoders, bounds-checked reads). `tests/tools/snapshot.py` and
`decode_snapshot.py` compare libstb with a build of the original stb code, byte for byte.

## Path rasterizer and stroker (FreeType)

The anti-aliasing scan converter and the stroker in `src/draw/raster/` (`stb/raster.hpp`) follow the
algorithms of FreeType's smooth rasterizer (`ftgrays.c`), stroker (`ftstroke.c`) and fixed-point
trigonometry (`fttrigon.c`), in the form used by plutovg (https://github.com/sammycage/plutovg,
MIT License, Copyright (c) 2020-2026 Samuel Ugochukwu). Nothing is vendored: the code is rewritten as classes
over `std::vector`, without the memory-pool and conic-curve parts, and `tests/tools/raster_reference/`
checks, with a build of plutovg, that every pixel is the same.

Portions of this software are copyright (c) 2000-2014 The FreeType Project (www.freetype.org). All
rights reserved. They are used under the FreeType License (FTL, https://freetype.org/license.html),
which asks for this credit and for the license text to be available with any redistribution; the
FreeType License is also distributed with plutovg as `FTL.TXT`.

## Filters and superpixels (`stb/filter.hpp`, `stb/superpixel.hpp`)

The Gaussian, box and Sobel filters and the convolution are textbook methods. The superpixel algorithms
follow these papers, and were written for libstb from them:

- SLIC and SLICO: R. Achanta, A. Shaji, K. Smith, A. Lucchi, P. Fua and S. Susstrunk, "SLIC Superpixels
  Compared to State-of-the-Art Superpixel Methods", IEEE TPAMI 34(11), 2012.
- SNIC: R. Achanta and S. Susstrunk, "Superpixels and Polygons using Simple Non-Iterative Clustering",
  CVPR 2017.

No code of any implementation of them is used or vendored (not the authors' reference code, not OpenCV's
`ximgproc`, not the benchmark collections that bundle them: several of those are GPL or non-commercial).
The assignment step is a per-pixel gather over fixed-point sums, not a transcription of the papers'
pseudocode, so that the result does not depend on the number of threads. Whether these methods are
covered by any patent has not been checked.

## Sample data (`data/`)

The pictures and fonts of `data/` are used by the examples and the tests; they are not part of the library or of
the wheel. Each can be redistributed, and `data/README.md` lists what and where from:

- `data/images/camera.png`, `chelsea.png`: CC0 (photographs by Lav Varshney and Stefan van der Walt), from
  scikit-image. `astronaut.png` (NASA) and `rocket.jpg` (SpaceX) are public domain, also from scikit-image.
  `badge.png` is drawn by `data/make_badge.py` with libstb (MIT).
- `data/fonts/Vazirmatn-Regular.ttf`, `Vazirmatn-Bold.ttf`: Copyright 2015 The Vazirmatn Project Authors
  (https://github.com/rastikerdar/vazirmatn), SIL Open Font License 1.1, the unmodified upstream files; the license
  text is `data/fonts/OFL.txt`.

## stb license (MIT option)

/*
------------------------------------------------------------------------------
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2017 Sean Barrett
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/
