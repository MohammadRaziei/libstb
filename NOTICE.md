# NOTICE

libstb is MIT licensed (see LICENSE.txt). Its image decoders, encoders, zlib compressor, font reader,
glyph rasterizer, rectangle packer and resizer are derived from Sean Barrett's stb libraries
(https://github.com/nothings/stb) and from the contributors acknowledged in them. stb is available
under two licenses, public domain or MIT; libstb uses the MIT option, reproduced below.

| libstb | derived from | stb version |
| :--- | :--- | :--- |
| `src/codec/decode/` (decoders and the inflater) | `stb_image.h` | v2.30 |
| `src/codec/` (`png.cpp`, `jpeg.cpp`, `bmp_tga.cpp`, `deflate.cpp`) | `stb_image_write.h` | v1.16 |
| `src/image/resize/` | `stb_image_resize2.h` | v2.18 |
| `src/font/truetype/` | `stb_truetype.h`, `stb_rect_pack.h` | v1.26, v1.01 |

The JPEG encoder is based on Jon Olick's jo_jpeg.cpp (public domain / MIT).

No stb header is vendored: the algorithms are the same, the code is reorganised (one file per format and
topic, classes for stateful decoders, bounds-checked reads). `tests/tools/snapshot.py` and
`decode_snapshot.py` compare libstb with a build of the original stb code, byte for byte.

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
