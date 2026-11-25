"""Filters, masks, gradients, histograms and warps: one name for an Image, a uint8 array and a float32 array. The argument is never
changed or converted (any other dtype is a TypeError) and the result is of the kind of the argument, with the shape of the argument."""

import numpy as np
import pytest

import libstb as stb

RNG = np.random.default_rng(9)
RGB8 = RNG.integers(0, 256, (24, 30, 3), dtype=np.uint8)
GRAY8 = RNG.integers(0, 256, (21, 25), dtype=np.uint8)
K3 = [[0.0, 1.0, 0.0], [1.0, -4.0, 1.0], [0.0, 1.0, 0.0]]
ROW, COL = [1.0, 2.0, 1.0], [1.0, 0.0, -1.0]


def f32(a):
    return a.astype(np.float32) / 255


def img(a):
    return stb.Image(a.copy())


def as_array(i):
    """An Image as the array a function of arrays must agree with."""
    return i.numpy() if isinstance(i, stb.Image) else i


# name, arguments after the data: filters whose result is of the kind and the shape of the argument
FILTERS = [
    ("median_filter", (1,)),
    ("bilateral_filter", (2, 1.5, 0.1)),
    ("gaussian_blur", (1.2,)),
    ("box_blur", (2,)),
    ("guided_filter", (2, 0.01)),
    ("convolve", (K3,)),
    ("convolve", (ROW, COL)),
]
FIDS = ["median_filter", "bilateral_filter", "gaussian_blur", "box_blur", "guided_filter", "convolve_2d", "convolve_separable"]


@pytest.mark.parametrize("name,args", FILTERS, ids=FIDS)
def test_a_filter_of_an_image_gives_a_new_image_and_a_method_exists(name, args):
    im = img(RGB8)
    out = getattr(stb, name)(im, *args)
    assert isinstance(out, stb.Image) and out is not im and np.array_equal(im.numpy(), RGB8)
    assert np.array_equal(getattr(im, name)(*args).numpy(), out.numpy())


@pytest.mark.parametrize("name,args", FILTERS, ids=FIDS)
def test_a_filter_of_a_uint8_array_is_a_uint8_array_with_the_values_of_the_image(name, args):
    arr = RGB8.copy()
    out = getattr(stb, name)(arr, *args)
    assert isinstance(out, np.ndarray) and out.dtype == np.uint8 and out.shape == arr.shape
    assert not np.shares_memory(out, arr) and np.array_equal(arr, RGB8)
    assert np.array_equal(out, getattr(stb, name)(img(RGB8), *args).numpy())


@pytest.mark.parametrize("name,args", FILTERS, ids=FIDS)
def test_a_filter_of_a_float32_array_is_a_float32_array_and_keeps_the_shape(name, args):
    for shape in [(24, 30, 3), (24, 30, 1), (24, 30)]:
        arr = np.ascontiguousarray(f32(RGB8)[:, :, :1] if shape[-1:] == (1,) else f32(RGB8)[:, :, 0] if len(shape) == 2 else f32(RGB8))
        keep = arr.copy()
        out = getattr(stb, name)(arr, *args)
        assert out.dtype == np.float32 and out.shape == arr.shape, shape
        assert np.array_equal(arr, keep) and not np.shares_memory(out, arr)


@pytest.mark.parametrize("name,args", FILTERS, ids=FIDS)
def test_a_filter_refuses_what_it_would_have_to_convert(name, args):
    fn = getattr(stb, name)
    for bad in (RGB8.astype(np.float64), RGB8.astype(np.int32), np.asfortranarray(RGB8), RGB8[:, ::2]):
        with pytest.raises(TypeError):
            fn(bad, *args)


def test_guided_filter_with_a_guide_of_the_same_kind():
    guide = RNG.integers(0, 256, RGB8.shape, dtype=np.uint8)
    want = stb.guided_filter(img(RGB8), 2, 0.01, img(guide)).numpy()
    assert np.array_equal(stb.guided_filter(RGB8, 2, 0.01, guide), want)
    assert stb.guided_filter(f32(RGB8), 2, 0.01, f32(guide)).dtype == np.float32
    with pytest.raises(TypeError):
        stb.guided_filter(RGB8, 2, 0.01, f32(guide))
    with pytest.raises(TypeError):
        stb.guided_filter(f32(RGB8), 2, 0.01, guide)


def test_a_kernel_may_be_a_list_or_an_array_and_a_bad_one_is_a_value_error():
    a = stb.convolve(RGB8, K3)
    assert np.array_equal(a, stb.convolve(RGB8, np.array(K3, dtype=np.float64)))
    with pytest.raises(ValueError):
        stb.convolve(RGB8, [])


def test_edge_is_given_by_name_in_any_case():
    assert np.array_equal(stb.box_blur(RGB8, 2, edge="REFLECT"), stb.box_blur(RGB8, 2, edge="reflect"))
    with pytest.raises(ValueError):
        stb.box_blur(RGB8, 2, edge="nonsense")


# ---- masks

def test_threshold_gives_a_gray_image_for_an_image_and_a_uint8_array_for_an_array():
    g = img(GRAY8)
    mask = stb.threshold(g, 100)
    assert isinstance(mask, stb.Image)
    arr = stb.threshold(GRAY8, 100)
    assert isinstance(arr, np.ndarray) and arr.dtype == np.uint8 and arr.shape == GRAY8.shape
    assert np.array_equal(arr, mask.numpy()[:, :, 0]) and np.array_equal(arr, np.where(GRAY8 > 100, 255, 0))
    assert np.array_equal(stb.threshold(GRAY8, 100, invert=True), np.where(GRAY8 > 100, 0, 255))
    assert np.array_equal(g.threshold(100).numpy(), mask.numpy())
    farr = stb.threshold(f32(GRAY8), 0.4)
    assert farr.dtype == np.uint8 and np.array_equal(farr, np.where(f32(GRAY8) > 0.4, 255, 0))
    assert stb.threshold(GRAY8[:, :, None], 100).shape == (21, 25, 1)


@pytest.mark.parametrize("bad", [-1, 256, 100.5])
def test_threshold_of_bytes_wants_a_whole_level_from_0_to_255(bad):
    with pytest.raises(ValueError):
        stb.threshold(GRAY8, bad)


def test_otsu_threshold_is_an_int_for_bytes_and_a_float_for_floats():
    a = stb.otsu_threshold(GRAY8)
    assert type(a) is int and a == stb.otsu_threshold(img(GRAY8))
    b = stb.otsu_threshold(f32(GRAY8))
    assert type(b) is float and abs(b - a / 255) < 0.02


def test_canny_and_adaptive_threshold_follow_the_same_rule():
    g = img(GRAY8)
    for fn, args in ((stb.canny, ()), (stb.adaptive_threshold, (3, 2.0))):
        mask = fn(g, *args)
        arr = fn(GRAY8, *args)
        assert isinstance(mask, stb.Image) and isinstance(arr, np.ndarray) and arr.dtype == np.uint8
        assert np.array_equal(arr, mask.numpy()[:, :, 0])
        assert fn(f32(GRAY8), *args).dtype == np.uint8
    assert np.array_equal(stb.adaptive_threshold(GRAY8, 3, 2.0, "mean"), stb.adaptive_threshold(g, 3, 2.0, "mean").numpy()[:, :, 0])
    with pytest.raises(ValueError):
        stb.adaptive_threshold(GRAY8, 3, method="nonsense")


# ---- gradients, magnitude, to_8u

@pytest.mark.parametrize("name", ["sobel", "scharr"])
def test_gradients_are_a_tuple_of_two_float32_arrays_of_the_shape_of_the_argument(name):
    fn = getattr(stb, name)
    dx, dy = fn(img(GRAY8))
    assert dx.dtype == dy.dtype == np.float32 and dx.shape == dy.shape == (21, 25, 1)  # an Image is (H, W, C)
    ax, ay = fn(GRAY8)
    assert ax.shape == (21, 25) and np.array_equal(ax, dx[:, :, 0]) and np.array_equal(ay, dy[:, :, 0])
    fx, fy = fn(f32(GRAY8)[:, :, None])
    assert fx.shape == (21, 25, 1) and fx.dtype == np.float32
    for bad in (GRAY8.astype(np.float64), GRAY8.astype(np.int32)):
        with pytest.raises(TypeError):
            fn(bad)


def test_magnitude_takes_two_float32_arrays_only():
    dx, dy = stb.sobel(f32(GRAY8))
    out = stb.magnitude(dx, dy)
    assert out.dtype == np.float32 and out.shape == dx.shape
    assert np.allclose(out, np.sqrt(dx * dx + dy * dy), atol=1e-5)
    with pytest.raises(TypeError):
        stb.magnitude(GRAY8, GRAY8)
    with pytest.raises(TypeError):
        stb.magnitude(dx, dy.astype(np.float64))


def test_to_8u_gives_a_uint8_array_of_the_shape_of_the_argument():
    a = np.array([[0.0, 0.5, 1.0], [-1.0, 2.0, float("nan")]], dtype=np.float32)
    out = stb.to_8u(a)
    assert out.dtype == np.uint8 and out.shape == (2, 3)
    assert out.tolist() == [[0, 128, 255], [0, 255, 0]]
    assert stb.to_8u(a[:, :, None]).shape == (2, 3, 1)
    with pytest.raises(TypeError):
        stb.to_8u(GRAY8)


def test_ssim_map_takes_two_values_of_the_same_kind():
    other = RNG.integers(0, 256, RGB8.shape, dtype=np.uint8)
    m = stb.ssim_map(img(RGB8), img(other))
    assert m.dtype == np.float32 and m.shape == (14, 20, 3)
    assert np.array_equal(stb.ssim_map(RGB8, other), m)
    assert stb.ssim_map(np.ascontiguousarray(f32(RGB8)[:, :, 0]), np.ascontiguousarray(f32(other)[:, :, 0])).shape == (14, 20)
    assert stb.ssim_map(f32(RGB8), f32(other)).dtype == np.float32
    with pytest.raises(TypeError):
        stb.ssim_map(RGB8, f32(other))
    with pytest.raises(TypeError):
        stb.ssim_map(img(RGB8), f32(other))


# ---- histogram

def test_histogram_of_bytes_and_of_floats():
    h = stb.histogram(img(RGB8))
    assert h.dtype == np.int32 and h.shape == (3, 256) and h.sum(axis=1).tolist() == [24 * 30] * 3
    assert np.array_equal(stb.histogram(RGB8), h) and np.array_equal(img(RGB8).histogram(), h)
    hf = stb.histogram(f32(RGB8), 16)
    assert hf.shape == (3, 16) and hf.sum(axis=1).tolist() == [24 * 30] * 3
    assert stb.histogram(f32(RGB8)).shape == (3, 256)
    with pytest.raises(TypeError):
        stb.histogram(RGB8.astype(np.float64))
    with pytest.raises(TypeError):
        stb.histogram(RGB8, 16)  # the number of bins is for floats


# ---- warps

SHIFT = [1.0, 0.0, 0.0, 1.0, 2.0, 3.0]  # a, b, c, d, e, f of an affine transform: a shift of (2, 3)
IDENT3 = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]  # a homography: 9 numbers by rows


@pytest.mark.parametrize("fn,m", [("warp_affine", SHIFT), ("warp_perspective", IDENT3)])
def test_a_warp_gives_a_new_value_of_the_kind_and_the_shape_of_the_argument(fn, m):
    f = getattr(stb, fn)
    im = img(RGB8)
    out = f(im, m, interpolation="nearest")
    assert isinstance(out, stb.Image) and np.array_equal(im.numpy(), RGB8)
    assert np.array_equal(getattr(im, fn)(m, interpolation="nearest").numpy(), out.numpy())
    arr = f(RGB8, m, interpolation="nearest")
    assert arr.dtype == np.uint8 and arr.shape == RGB8.shape and np.array_equal(arr, out.numpy())
    fl = f(np.ascontiguousarray(f32(RGB8)[:, :, :1]), m, interpolation="nearest")
    assert fl.dtype == np.float32 and fl.shape == (24, 30, 1)
    assert f(np.ascontiguousarray(RGB8[:, :, 0]), m, interpolation="nearest").shape == (24, 30)
    assert f(RGB8, m, 10, 12, interpolation="nearest").shape == (12, 10, 3)
    with pytest.raises(TypeError):
        f(RGB8.astype(np.float64), m)
    with pytest.raises(ValueError):
        f(RGB8, m, interpolation="nonsense")


def test_the_identity_warp_changes_nothing_and_a_shift_moves_the_picture():
    assert np.array_equal(stb.warp_affine(RGB8, [1, 0, 0, 1, 0, 0], interpolation="nearest"), RGB8)
    moved = stb.warp_affine(RGB8, SHIFT, interpolation="nearest")
    assert np.array_equal(moved[3:, 2:], RGB8[:-3, :-2]) and not moved[:3].any()
