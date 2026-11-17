"""median_filter, unsharp_mask, pixelize, convert_color and the metrics (mae, mse, psnr, ssim). The C++ tests
(tests/cpp/test_filter.cpp, test_colorspace.cpp, test_metrics.cpp) work the small cases out by hand; here the values are
compared with independent implementations (scipy, scikit-image, colorsys) and the Python surface is checked: Image and
float32 array forms, strings and ColorSpace, inplace, errors. scipy and scikit-image are optional: the tests that need
them are skipped without them."""

import colorsys
import math

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb


def rand8(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def randf(h, w, c=None, seed=0):
    shape = (h, w) if c is None else (h, w, c)
    return np.random.default_rng(seed).random(shape, dtype=np.float32)


# ------------------------------------------------------------------------------------------------ median_filter

SCIPY_MODES = {"clamp": "nearest", "reflect": "reflect", "mirror": "mirror", "wrap": "wrap", "zero": "constant"}


@pytest.mark.parametrize("edge", sorted(SCIPY_MODES))
@pytest.mark.parametrize("radius", [1, 3])
def test_median_filter_matches_scipy(edge, radius):
    ndimage = pytest.importorskip("scipy.ndimage")
    a = rand8(21, 17, 3, seed=1)
    got = np.asarray(stb.median_filter(stb.Image(a), radius, edge=edge))
    for k in range(3):
        want = ndimage.median_filter(a[..., k], size=2 * radius + 1, mode=SCIPY_MODES[edge], cval=0)
        assert np.array_equal(got[..., k], want)
    f = randf(15, 19, 2, seed=2)
    gotf = stb.median_filter(f, radius, edge=edge)
    assert gotf.dtype == np.float32 and gotf.shape == f.shape
    for k in range(2):
        want = ndimage.median_filter(f[..., k], size=2 * radius + 1, mode=SCIPY_MODES[edge], cval=0)
        assert np.array_equal(gotf[..., k], want)


def test_median_filter_forms_and_errors():
    img = stb.Image(rand8(12, 12, 1, seed=3))
    assert np.array_equal(np.asarray(img.median_filter(2)), np.asarray(stb.median_filter(img, 2)))
    assert stb.median_filter(randf(9, 9), 1).shape == (9, 9)  # (H, W) in, (H, W) out
    with pytest.raises(ValueError):
        stb.median_filter(img, 0)
    with pytest.raises(ValueError):
        stb.median_filter(img, 1, edge="nowhere")


# ------------------------------------------------------------------------------------------------ unsharp_mask

def test_unsharp_mask_matches_the_formula_with_scipy_gaussian():
    ndimage = pytest.importorskip("scipy.ndimage")
    f = randf(30, 25, 3, seed=4)
    got = stb.unsharp_mask(f, 2.0, amount=1.5)
    blur = np.stack([ndimage.gaussian_filter(f[..., k], 2.0, truncate=3.0, mode="nearest") for k in range(3)], axis=-1)
    want = np.clip(f + 1.5 * (f - blur), 0.0, 1.0)
    assert np.abs(got - want).max() < 2e-5


def test_unsharp_mask_forms_alpha_and_errors():
    img = stb.Image(rand8(16, 16, 4, seed=5))
    out = stb.unsharp_mask(img, 1.0, 1.2, 0.01)
    assert np.array_equal(np.asarray(out)[..., 3], np.asarray(img)[..., 3])  # alpha is never changed
    same = img.copy()
    assert same.unsharp_mask(1.0, 1.2, 0.01, inplace=True) is same
    assert np.array_equal(np.asarray(same), np.asarray(out))
    assert np.array_equal(np.asarray(stb.unsharp_mask(img, 1.0, 0.0)), np.asarray(img))  # amount 0
    for bad in ({"sigma": 0.0}, {"sigma": 1.0, "amount": -1.0}, {"sigma": 1.0, "threshold": 2.0}):
        with pytest.raises(ValueError):
            stb.unsharp_mask(img, **bad)


# ------------------------------------------------------------------------------------------------ pixelize

def test_pixelize_is_the_mean_of_each_block():
    a = rand8(10, 13, 3, seed=6)
    got = np.asarray(stb.pixelize(stb.Image(a), 4, 3))
    for y0 in range(0, 10, 3):
        for x0 in range(0, 13, 4):
            block = a[y0:y0 + 3, x0:x0 + 4].reshape(-1, 3).astype(np.int64)
            want = (2 * block.sum(axis=0) + len(block)) // (2 * len(block))  # rounded to the nearest, halves up
            assert (got[y0:y0 + 3, x0:x0 + 4] == want).all()


def test_pixelize_float_forms_and_errors():
    f = randf(9, 11, 3, seed=7)
    got = stb.pixelize(f, 3)
    assert got.dtype == np.float32
    assert abs(got[0:3, 0:3].mean() - f[0:3, 0:3].mean()) < 1e-6
    img = stb.Image(rand8(9, 9, 3, seed=8))
    assert np.array_equal(np.asarray(img.pixelize(2, 4)), np.asarray(stb.pixelize(img, 2, 4)))
    same = img.copy()
    assert same.pixelize(3, inplace=True) is same
    with pytest.raises(ValueError):
        stb.pixelize(img, 0)
    with pytest.raises(ValueError):
        stb.pixelize(img, 2, -1)


# ------------------------------------------------------------------------------------------------ convert_color

def test_hsv_and_hsl_match_colorsys():
    f = randf(20, 20, 3, seed=9)
    hsv = stb.convert_color(f, "rgb", "hsv")
    hsl = stb.convert_color(f, stb.ColorSpace.RGB, stb.ColorSpace.HSL)
    for y in range(20):
        for x in range(20):
            r, g, b = (float(v) for v in f[y, x])
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            assert np.allclose(hsv[y, x], (h, s, v), atol=2e-6)
            h2, l, s2 = colorsys.rgb_to_hls(r, g, b)
            assert np.allclose(hsl[y, x], (h2, s2, l), atol=2e-6)


def test_ycbcr_is_the_jpeg_transform():
    f = randf(8, 8, 3, seed=10)
    got = stb.convert_color(f, "rgb", "ycbcr")
    r, g, b = f[..., 0], f[..., 1], f[..., 2]
    y = 0.299 * r + 0.587 * g + 0.114 * b
    want = np.stack([y, 0.5 + (b - y) / 1.772, 0.5 + (r - y) / 1.402], axis=-1)
    assert np.abs(got - want).max() < 2e-6


@pytest.mark.parametrize("space", ["hsv", "hsl", "ycbcr"])
def test_convert_color_round_trips_and_forms(space):
    img = stb.Image(rand8(18, 14, 4, seed=11))
    there = stb.convert_color(img, "rgb", space)
    assert np.array_equal(np.asarray(there)[..., 3], np.asarray(img)[..., 3])  # alpha
    back = np.asarray(stb.convert_color(there, space, "rgb")).astype(int)
    assert np.abs(back - np.asarray(img).astype(int)).max() <= 6
    same = img.copy()
    assert same.convert_color("rgb", space, inplace=True) is same
    assert np.array_equal(np.asarray(same), np.asarray(there))
    assert np.array_equal(np.asarray(img.convert_color(stb.ColorSpace.RGB, space)), np.asarray(there))


def test_convert_color_errors():
    with pytest.raises(ValueError):
        stb.convert_color(stb.Image(rand8(4, 4, 1)), "rgb", "hsv")  # gray has no colour
    with pytest.raises(ValueError):
        stb.convert_color(stb.Image(rand8(4, 4, 3)), "rgb", "cmyk")
    with pytest.raises(ValueError):
        stb.convert_color(randf(4, 4, 2), "rgb", "hsv")


# ------------------------------------------------------------------------------------------------ metrics

def test_error_metrics_match_numpy_and_scikit_image():
    metrics = pytest.importorskip("skimage.metrics")
    a, b = rand8(24, 30, 3, seed=12), rand8(24, 30, 3, seed=13)
    ia, ib = stb.Image(a), stb.Image(b)
    d = a.astype(np.float64) - b.astype(np.float64)
    assert stb.mae(ia, ib) == pytest.approx(np.abs(d).mean(), rel=1e-12)
    assert stb.mse(ia, ib) == pytest.approx((d * d).mean(), rel=1e-12)
    assert stb.psnr(ia, ib) == pytest.approx(metrics.peak_signal_noise_ratio(a, b, data_range=255), rel=1e-9)
    assert stb.psnr(ia, ia) == math.inf
    fa, fb = a.astype(np.float32) / 255, b.astype(np.float32) / 255
    assert stb.psnr(fa, fb) == pytest.approx(stb.psnr(ia, ib), rel=1e-5)
    assert stb.mae(fa, fb) == pytest.approx(stb.mae(ia, ib) / 255, rel=1e-5)


def test_ssim_matches_scikit_image_with_gaussian_weights():
    metrics = pytest.importorskip("skimage.metrics")
    a = rand8(40, 48, 3, seed=14)
    b = np.clip(a.astype(int) + np.random.default_rng(15).integers(-40, 41, a.shape), 0, 255).astype(np.uint8)
    want = metrics.structural_similarity(a, b, data_range=255, channel_axis=-1, gaussian_weights=True, sigma=1.5,
                                         use_sample_covariance=False)
    assert stb.ssim(stb.Image(a), stb.Image(b)) == pytest.approx(want, abs=2e-6)
    fa, fb = a.astype(np.float32) / 255, b.astype(np.float32) / 255
    assert stb.ssim(fa, fb) == pytest.approx(want, abs=2e-5)
    m = stb.ssim_map(stb.Image(a), stb.Image(b))
    assert m.dtype == np.float32 and m.shape == (30, 38, 3)
    assert float(m.mean()) == pytest.approx(stb.ssim(stb.Image(a), stb.Image(b)), abs=1e-6)
    assert stb.ssim_map(fa[..., 0], fb[..., 0]).shape == (30, 38)  # (H, W) in, (H, W) out


def test_metrics_errors():
    a, b = stb.Image(rand8(12, 12, 3)), stb.Image(rand8(12, 13, 3))
    for fn in (stb.mae, stb.mse, stb.psnr, stb.ssim, stb.ssim_map):
        with pytest.raises(ValueError):
            fn(a, b)
    with pytest.raises(ValueError):
        stb.ssim(stb.Image(rand8(10, 12, 1)), stb.Image(rand8(10, 12, 1, seed=1)))  # smaller than 11 x 11


def test_the_number_of_threads_does_not_change_the_results():
    a, b = randf(300, 260, 3, seed=16), randf(300, 260, 3, seed=17)
    img = stb.Image(rand8(300, 260, 3, seed=18))
    with stb.threads(1):
        one = (stb.ssim(a, b), stb.mse(a, b), np.asarray(stb.median_filter(img, 2)), np.asarray(stb.pixelize(img, 7)),
               np.asarray(stb.unsharp_mask(img, 1.5)), np.asarray(stb.convert_color(img, "rgb", "hsl")))
    with stb.threads(4):
        many = (stb.ssim(a, b), stb.mse(a, b), np.asarray(stb.median_filter(img, 2)), np.asarray(stb.pixelize(img, 7)),
                np.asarray(stb.unsharp_mask(img, 1.5)), np.asarray(stb.convert_color(img, "rgb", "hsl")))
    assert one[0] == many[0] and one[1] == many[1]
    for x, y in zip(one[2:], many[2:]):
        assert np.array_equal(x, y)


# ------------------------------------------------------------------------------------------------ bilateral_filter

NUMPY_PADS = {"clamp": "edge", "reflect": "symmetric", "mirror": "reflect", "wrap": "wrap", "zero": "constant"}


def bilateral_reference(a, radius, sigma_space, sigma_range, edge, scale):
    """The formula of stb::bilateral_filter with numpy (doubles). a is (H, W, colour); scale turns its values into 0..1."""
    h, w, c = a.shape
    kw = {"constant_values": 0} if edge == "zero" else {}
    p = np.pad(a.astype(np.float64), ((radius, radius), (radius, radius), (0, 0)), mode=NUMPY_PADS[edge], **kw)
    acc = np.zeros((h, w, c))
    wsum = np.zeros((h, w, 1))
    centre = a.astype(np.float64)
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            nb = p[radius + dy:radius + dy + h, radius + dx:radius + dx + w]
            d2 = (((nb - centre) * scale) ** 2).sum(axis=2, keepdims=True)
            wgt = math.exp(-(dx * dx + dy * dy) / (2 * sigma_space ** 2)) * np.exp(-d2 / (2 * sigma_range ** 2))
            acc += wgt * nb
            wsum += wgt
    return acc / wsum


@pytest.mark.parametrize("edge", sorted(NUMPY_PADS))
@pytest.mark.parametrize("radius", [1, 3])
def test_bilateral_filter_matches_a_numpy_version_of_the_formula(edge, radius):
    a = rand8(19, 23, 4, seed=31)
    got = np.asarray(stb.bilateral_filter(stb.Image(a), radius, 2.0, 0.3, edge=edge)).astype(np.float64)
    want = bilateral_reference(a[..., :3], radius, 2.0, 0.3, edge, 1 / 255.0)
    assert np.abs(got[..., :3] - want).max() <= 0.5 + 1e-3
    assert np.array_equal(got[..., 3], a[..., 3])  # alpha is copied
    f = randf(14, 17, 3, seed=32)
    gotf = stb.bilateral_filter(f, radius, 2.0, 0.3, edge=edge)
    assert gotf.dtype == np.float32 and gotf.shape == f.shape
    assert np.abs(gotf - bilateral_reference(f, radius, 2.0, 0.3, edge, 1.0)).max() < 1e-4


def test_bilateral_filter_keeps_edges_that_a_blur_smears():
    a = np.zeros((20, 20, 1), dtype=np.uint8)
    a[:, 10:] = 200
    rng = np.random.default_rng(33)
    noisy = np.clip(a.astype(int) + rng.integers(-8, 9, a.shape), 0, 255).astype(np.uint8)
    img = stb.Image(noisy)
    bil = np.asarray(stb.bilateral_filter(img, 3, 3.0, 0.1)).astype(int)
    gau = np.asarray(stb.gaussian_blur(img, 2.0)).astype(int)
    assert abs(bil[:, 9, 0] - noisy[:, 9, 0]).mean() < 12  # the dark side next to the step stays dark
    assert gau[:, 9, 0].mean() > bil[:, 9, 0].mean() + 20  # the blur pulled bright values across the step
    assert bil[:, :6, 0].std() < noisy[:, :6, 0].std()  # and the noise inside a flat area went down


def test_bilateral_filter_image_method_radius_zero_and_errors():
    img = stb.Image(rand8(12, 14, 3, seed=34))
    assert np.array_equal(np.asarray(img.bilateral_filter(2, 1.5, 0.2)), np.asarray(stb.bilateral_filter(img, 2, 1.5, 0.2)))
    assert np.array_equal(np.asarray(stb.bilateral_filter(img, 0, 1.0, 0.2)), np.asarray(img))
    for args in [(-1, 1.0, 0.2), (1001, 1.0, 0.2), (1, 0.0, 0.2), (1, 1.0, -1.0), (1, float("nan"), 0.2), (1, 1.0, float("inf"))]:
        with pytest.raises(ValueError):
            stb.bilateral_filter(img, *args)
    with pytest.raises(ValueError):
        stb.bilateral_filter(img, 1, 1.0, 0.2, edge="nope")
