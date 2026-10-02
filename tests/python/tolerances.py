"""Shared tolerance for every JPEG comparison against another library (Pillow / libjpeg).

JPEG is lossy and implementations round differently, so no two agree bit for bit. One limit
covers all of those tests (grayscale, 4:4:4, 4:2:0, encode, decode), instead of a limit per
case.

JPEG_NMAE is the normalized mean absolute error: mean(|a - b|) / 255, so 5e-4 is 0.1275 of
one 8-bit level. Measured worst cases against it: 3.0e-4 (4:2:0 decode, the largest),
1.1e-4 (encode quality difference), 7.4e-5 (4:4:4 decode), 1.9e-5 (grayscale decode).
JPEG_MAX_DIFF is the worst single value in levels (measured 3).
"""

JPEG_NMAE = 5e-4
JPEG_MAX_DIFF = 4
