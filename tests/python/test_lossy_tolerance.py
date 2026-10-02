"""Lossy operations (JPEG, resize) cannot match another library bit for bit: encoders and
filters round differently, and libstb makes a few deliberate choices of its own (4:4:4
chroma at JPEG quality >= 90, gray JPEGs stored as 3 components). So these tests pin
MEASURED tolerances: tight enough that a wrong table, filter or channel order fails, loose
enough for rounding. Lossless formats are compared exactly in test_lossless_exact.py.
"""
import io

import numpy as np
import pytest
from PIL import Image as PILImage

from libstb import Image, Resizer


def photo(h, w, c, seed=0):
    """Smooth colour fields, soft noise and one hard-edged block."""
    rng = np.random.default_rng(seed)
    ys, xs = np.linspace(0, 1, h)[:, None], np.linspace(0, 1, w)[None, :]
    out = np.empty((h, w, c), np.float64)
    for k in range(c):
        p = rng.uniform(0, 6.28, 3)
        out[..., k] = (128 + 70 * np.sin(2 * np.pi * (1.5 * xs + 0.7 * ys) + p[0])
                       + 40 * np.sin(2 * np.pi * (4 * xs - 3 * ys) + p[1])
                       + 15 * np.sin(2 * np.pi * (11 * xs + 9 * ys) + p[2]))
    out += rng.normal(0, 2.0, out.shape)
    out[h // 4: h // 2, w // 5: w // 2] = rng.uniform(0, 255, c)
    return np.clip(out, 0, 255).astype(np.uint8)


def pil_image(arr):
    return PILImage.fromarray(arr[..., 0] if arr.shape[2] == 1 else arr)


def pil_array(data):
    a = np.asarray(PILImage.open(io.BytesIO(data)))
    return (a[..., None] if a.ndim == 2 else a).astype(int)


def mean_abs(a, b):
    return float(np.abs(a.astype(int) - b.astype(int)).mean())


# ----------------------------------------------------------------- JPEG ----

@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("quality", [30, 50, 75, 90, 95])
def test_jpeg_encode_is_as_accurate_as_pillows(c, quality):
    src = photo(97, 131, c)
    mine = Image(src).to_jpg(quality=quality)
    buf = io.BytesIO()
    pil_image(src).save(buf, "JPEG", quality=quality)
    theirs = buf.getvalue()
    # measured: libstb is within +0.06 of Pillow at every quality, and better at 95
    assert mean_abs(pil_array(mine), src) <= mean_abs(pil_array(theirs), src) + 0.15
    if c == 3:
        # same file size to within ~4% below q90; q>=90 keeps full-resolution chroma, so it is bigger
        ratio = len(mine) / len(theirs)
        assert (0.9 <= ratio <= 1.1) if quality < 90 else (0.9 <= ratio <= 1.6)


@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("quality", [50, 90])
def test_jpeg_decode_agrees_with_libjpeg(c, quality):
    buf = io.BytesIO()
    pil_image(photo(97, 131, c, seed=2)).save(buf, "JPEG", quality=quality)
    data = buf.getvalue()
    mine, ref = Image.open(data).array.astype(int), pil_array(data)
    assert mine.shape == ref.shape
    # measured: mean 0.01 (gray) / 0.09 (colour), max 1 / 3 (IDCT rounding, chroma upsampling)
    assert np.abs(mine - ref).mean() <= 0.15
    assert np.abs(mine - ref).max() <= 4


# --------------------------------------------------------------- resize ----

def pil_resize(arr, w, h, pil_filter):
    out = np.asarray(pil_image(arr).resize((w, h), resample=pil_filter))
    return (out[..., None] if out.ndim == 2 else out).astype(int)


def libstb_resize(arr, w, h, name):
    # srgb=False: Pillow blends stored values directly, libstb resizes in linear light by default
    return Image(arr).resize(w, h, Resizer(name, srgb=False)).array.astype(int)


@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("name,pil_filter", [("linear", PILImage.BILINEAR), ("cubic", PILImage.BICUBIC)])
@pytest.mark.parametrize("size", [(64, 48), (32, 24), (16, 12), (200, 150), (97, 71)])
def test_resize_stays_within_rounding_of_pillow(c, name, pil_filter, size):
    src = photo(96, 128, c, seed=3)
    d = np.abs(libstb_resize(src, *size, name) - pil_resize(src, *size, pil_filter))
    # measured worst cases: mean 0.70, max 8 (at 8x shrink, where the kernels' edge handling differs)
    assert d.mean() <= 0.9
    assert d.max() <= 10


@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("size", [(64, 48), (32, 24), (16, 12)])
def test_box_resize_matches_pillow_on_whole_number_ratios(c, size):
    # Non-whole ratios are NOT compared: the two libraries place the fractional box windows
    # differently (mean ~2 levels, tens at hard edges), which is a choice, not an error.
    src = photo(96, 128, c, seed=3)
    d = np.abs(libstb_resize(src, *size, "box") - pil_resize(src, *size, PILImage.BOX))
    assert d.max() <= 1
    assert d.mean() <= 0.45


@pytest.mark.parametrize("name,pil_filter", [("linear", PILImage.BILINEAR), ("cubic", PILImage.BICUBIC)])
@pytest.mark.parametrize("size", [(64, 48), (16, 12), (60, 45)])
def test_rgba_resize_agrees_once_alpha_is_accounted_for(name, pil_filter, size):
    """Pillow resizes RGBA premultiplied, so colour at alpha ~ 0 is arbitrary and an
    unpremultiplied comparison can differ by 250. Compare alpha, and colour weighted by alpha."""
    src = photo(96, 128, 4, seed=3)
    mine, ref = libstb_resize(src, *size, name), pil_resize(src, *size, pil_filter)
    assert np.abs(mine[..., 3] - ref[..., 3]).max() <= 6  # measured 5
    weighted = np.abs(mine[..., :3] * mine[..., 3:] / 255 - ref[..., :3] * ref[..., 3:] / 255)
    assert weighted.mean() <= 1.0  # measured 0.74
    assert weighted.max() <= 8  # measured 6.1
