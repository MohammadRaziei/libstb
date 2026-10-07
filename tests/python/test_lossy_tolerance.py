"""Lossy operations (JPEG, resize) are compared with a tolerance, lossless ones exactly
(test_lossless_exact.py).

Differences are measured as the NORMALIZED mean absolute error, nmae = mean(|a - b|) / 255,
so 1e-4 is 0.0255 of one 8-bit level. Every bound below is a measured worst case plus
headroom, with the measurement in the comment next to it. Two kinds of reference are used:

  * an independent float64 reference written in this file (resize): libstb must match it to
    ~1e-6, which is far tighter than any comparison against another library can be;
  * Pillow / libjpeg (JPEG, plus a loose sanity check on resize): these round differently
    (Pillow rounds to 8 bits between its horizontal and vertical resize pass), so the
    bounds are looser and say why. Every JPEG comparison shares one limit, JPEG_NMAE in
    tolerances.py (5e-4), instead of a limit per case.
"""
import io

import numpy as np
import pytest
from PIL import Image as PILImage

from libstb import Image, Resizer
from tolerances import JPEG_MAX_DIFF, JPEG_NMAE


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


def nmae(a, b):
    """Normalized mean absolute error: 1.0 would be black vs white everywhere."""
    return mean_abs(a, b) / 255


# ----------------------------------------------------------------- JPEG ----

@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("quality", [30, 50, 75, 90, 95])
def test_jpeg_encode_is_as_accurate_as_pillows(c, quality):
    src = photo(97, 131, c)
    mine = Image(src).to_jpg(quality=quality)
    buf = io.BytesIO()
    pil_image(src).save(buf, "JPEG", quality=quality)
    theirs = buf.getvalue()
    # Two different encoders never agree byte for byte, so compare what matters: the error
    # against the source. Measured, libstb minus Pillow, in nmae: -1.5e-3 .. +2.3e-4 (colour at
    # q75; usually under 4e-5). Being better is always allowed.
    assert nmae(pil_array(mine), src) <= nmae(pil_array(theirs), src) + JPEG_NMAE
    if c == 3:
        # measured 0.986-1.033 below q90; q>=90 keeps full-resolution chroma (1.45 at q95)
        ratio = len(mine) / len(theirs)
        assert (0.95 <= ratio <= 1.05) if quality < 90 else (0.95 <= ratio <= 1.6)


@pytest.mark.parametrize("c,subsampling", [
    (1, -1),  # gray: IDCT rounding only (measured 4.0e-5)
    (3, 0),   # 4:4:4: adds the YCbCr->RGB rounding (measured 1.1e-4)
    (3, 2),   # 4:2:0: adds chroma upsampling (measured 3.0e-4)
])
@pytest.mark.parametrize("quality", [50, 90])
def test_jpeg_decode_agrees_with_libjpeg(c, subsampling, quality):
    buf = io.BytesIO()
    pil_image(photo(97, 131, c, seed=2)).save(buf, "JPEG", quality=quality, subsampling=subsampling)
    data = buf.getvalue()
    mine, ref = Image.open(data).numpy().astype(int), pil_array(data)
    assert mine.shape == ref.shape
    assert nmae(mine, ref) <= JPEG_NMAE
    assert np.abs(mine - ref).max() <= JPEG_MAX_DIFF


# --------------------------------------------------------------- resize ----
#
# The reference: separable resize in float64 with the same kernels and sampling rule as
# stb_image_resize2 (pixel centres, kernel stretched by 1/scale when shrinking, weights
# normalized, edges clamped), rounded once at the end. Pillow cannot be this reference:
# it rounds to 8 bits between its two passes, which alone puts it ~1e-3 off the true value.

def _triangle(x):
    x = np.abs(x)
    return np.where(x < 1, 1 - x, 0.0)


def _catmull_rom(x):
    x = np.abs(x)
    return np.where(x < 1, 1.5 * x**3 - 2.5 * x**2 + 1, np.where(x < 2, -0.5 * x**3 + 2.5 * x**2 - 4 * x + 2, 0.0))


KERNELS = {"linear": (_triangle, 1.0), "cubic": (_catmull_rom, 2.0)}


def _resize_axis(a, out, axis, name):
    kernel, support = KERNELS[name]
    n = a.shape[axis]
    scale = out / n
    a = np.moveaxis(a, axis, 0)
    res = np.empty((out,) + a.shape[1:])
    for o in range(out):
        centre = (o + 0.5) / scale
        radius = support / scale if scale < 1 else support
        taps = np.arange(int(np.floor(centre - radius + 0.5)), int(np.floor(centre + radius + 0.5)) + 1)
        w = kernel((taps + 0.5 - centre) * (scale if scale < 1 else 1))
        res[o] = np.tensordot(w / w.sum(), a[np.clip(taps, 0, n - 1)], axes=(0, 0))
    return np.moveaxis(res, 0, axis)


def reference_resize(arr, w, h, name):
    x = _resize_axis(_resize_axis(arr.astype(np.float64), w, 1, name), h, 0, name)
    return np.floor(x + 0.5).clip(0, 255).astype(int)


def reference_resize_rgba(arr, w, h, name):
    """libstb weights colour by alpha (premultiplied), like Pillow."""
    f = arr.astype(np.float64)
    pm = np.concatenate([f[..., :3] * f[..., 3:] / 255, f[..., 3:]], axis=-1)
    r = _resize_axis(_resize_axis(pm, w, 1, name), h, 0, name)
    alpha = r[..., 3:] / 255
    rgb = np.where(alpha > 0, r[..., :3] / np.where(alpha > 0, alpha, 1), 0)
    return np.floor(np.concatenate([rgb, r[..., 3:]], axis=-1) + 0.5).clip(0, 255).astype(int)


def libstb_resize(arr, w, h, name):
    # srgb=False: resize the stored values directly, as the references do (the default is linear light)
    return Image(arr).resize(w, h, Resizer(name, srgb=False)).numpy().astype(int)


SIZES = [(64, 48), (32, 24), (16, 12), (97, 71), (133, 100), (200, 150)]  # 128x96 source: shrink, odd ratios, enlarge


@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("name", ["linear", "cubic"])
@pytest.mark.parametrize("size", SIZES)
def test_resize_matches_the_float_reference(c, name, size):
    src = photo(96, 128, c, seed=3)
    mine, ref = libstb_resize(src, *size, name), reference_resize(src, *size, name)
    assert nmae(mine, ref) <= 1e-5  # measured worst 3.7e-6: 0.1% of values off by one, at rounding ties
    assert np.abs(mine - ref).max() <= 1


@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("factor", [2, 4, 8])
def test_box_shrink_by_a_whole_factor_is_the_exactly_rounded_mean(c, factor):
    src = photo(96, 128, c, seed=3)
    h, w = src.shape[0] // factor, src.shape[1] // factor
    mean = src.astype(np.float64).reshape(h, factor, w, factor, c).mean(axis=(1, 3))
    np.testing.assert_array_equal(libstb_resize(src, w, h, "box"), np.floor(mean + 0.5).astype(int))  # EXACT


@pytest.mark.parametrize("size", [(64, 48), (16, 12), (60, 45), (200, 150)])
def test_rgba_linear_resize_matches_the_premultiplied_reference(size):
    src = photo(96, 128, 4, seed=3)
    mine, ref = libstb_resize(src, *size, "linear"), reference_resize_rgba(src, *size, "linear")
    assert nmae(mine, ref) <= 1e-5  # measured worst 8.8e-7
    assert np.abs(mine - ref).max() <= 1


@pytest.mark.parametrize("size", [(64, 48), (16, 12), (60, 45), (200, 150)])
def test_rgba_cubic_resize_alpha_is_exact_and_colour_matches_where_it_is_visible(size):
    """Cubic has negative lobes, so where the resized alpha is ~0 un-premultiplying turns
    float noise into huge colour differences (up to 165). Those pixels are invisible, so the
    colour is compared weighted by alpha."""
    src = photo(96, 128, 4, seed=3)
    mine, ref = libstb_resize(src, *size, "cubic"), reference_resize_rgba(src, *size, "cubic")
    assert np.abs(mine[..., 3] - ref[..., 3]).max() <= 1
    weighted = np.abs(mine[..., :3] * mine[..., 3:] - ref[..., :3] * ref[..., 3:]) / 255
    assert weighted.max() <= 1.0  # measured 0.61


# Sanity check against Pillow: a loose bound on purpose. Pillow rounds to 8 bits between
# its horizontal and vertical pass, so it sits ~1e-3 from the true value that libstb hits
# (measured 6e-4 .. 2.8e-3, worst at 8x shrink; see the exact tests above).
def pil_resize(arr, w, h, pil_filter):
    out = np.asarray(pil_image(arr).resize((w, h), resample=pil_filter))
    return (out[..., None] if out.ndim == 2 else out).astype(int)


@pytest.mark.parametrize("c", [1, 3])
@pytest.mark.parametrize("name,pil_filter", [("linear", PILImage.BILINEAR), ("cubic", PILImage.BICUBIC)])
@pytest.mark.parametrize("size", SIZES)
def test_resize_is_close_to_pillow(c, name, pil_filter, size):
    src = photo(96, 128, c, seed=3)
    mine, theirs = libstb_resize(src, *size, name), pil_resize(src, *size, pil_filter)
    assert nmae(mine, theirs) <= 4e-3
    assert np.abs(mine - theirs).max() <= 10  # measured 8 (8x shrink: the kernels' edge handling differs)
