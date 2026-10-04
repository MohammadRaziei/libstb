"""The JPEG encoder is lossy but fully deterministic, and libstb's optimized encoder is meant to
write exactly the bytes upstream stb_image_write.h v1.16 writes. These digests were computed
with the UNMODIFIED upstream header, so any change to the encoder (a faster DCT, FMA or AVX
code, a new bit writer) that alters even one byte fails here instead of silently shifting
image quality. Exact comparison: it is a hash of the bytes, not a tolerance.
"""
import hashlib

import numpy as np
import pytest

from libstb import Image


def pattern(h, w, c):
    """Integer-only test image (no random generator, so it never changes between numpy versions)."""
    y, x, k = np.mgrid[0:h, 0:w, 0:c]
    return ((((x * 7 + y * 3 + k * 50) % 256) // 2 + ((x // 5 + y // 3) % 2) * 90 + ((x * y + k * 13) % 23) * 4)
            .clip(0, 255).astype(np.uint8))


# height, width, channels, quality, sha256 of upstream's bytes (4 channels: alpha is ignored, so it
# equals the 3-channel digest; 16x16 and 17x33 hit the full-MCU and edge-padded paths)
GOLDEN = [
    (29, 37, 1, 75, "053c3391018c0d13b391f46f2fb220c92be2626fd93f07fd9a3d124efe8a2e3d"),
    (29, 37, 3, 30, "7ac3ddac88ebe854a49905515aa3fff8c88988652c03fffd92fd51719f0aad64"),
    (29, 37, 3, 75, "bd50c1ef8e01ec4da6a1c60d35457937c1123beb55f97e3bb057bfade791f201"),
    (29, 37, 3, 90, "b7a8a5cdbce68a9679663f7a0dd4aef8745fac28f1c05955f5942e11241508af"),
    (29, 37, 3, 95, "4aedd8548702bf75c430b8755f78f16c6dedf71982538c11385157917209435a"),
    (29, 37, 4, 75, "bd50c1ef8e01ec4da6a1c60d35457937c1123beb55f97e3bb057bfade791f201"),
    (75, 100, 3, 50, "d86dedcc8bd61102433dc00d1c8eb6f568d1b30f571d6ef5aac0cb7fe032c5af"),
    (75, 100, 3, 91, "1b7e56ca30a4b171b925726997c7fd3131e934aa14a88b676acc1e69939e59e2"),
    (75, 100, 1, 95, "27e777c447266d792ca6783095ed5a423b9b9d74d9ce51c2db337c618f18c4cd"),
    (16, 16, 3, 90, "babfbb87967bb0d983a842475fdb357da8647c7999052eed7c66ebf54fe76718"),
    (17, 33, 3, 60, "53a4cffb6d6d010172d63b023046e8cc1c6f7481e87f9c200821297f6280d54c"),
]


@pytest.mark.parametrize("h,w,c,quality,digest", GOLDEN)
def test_jpeg_bytes_are_identical_to_upstream_stb(h, w, c, quality, digest):
    data = Image(pattern(h, w, c)).to_jpg(quality=quality)
    assert hashlib.sha256(data).hexdigest() == digest
