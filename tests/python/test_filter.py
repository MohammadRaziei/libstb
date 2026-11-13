"""Filters: gaussian_blur, box_blur, convolve, sobel, magnitude and the 8-bit <-> float conversions. Checked against
a plain numpy computation (and SciPy when it is installed)."""

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb


def rand8(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def randf(h, w, c=None, seed=0):
    shape = (h, w) if c is None else (h, w, c)
    return np.random.default_rng(seed).random(shape, dtype=np.float32)


def brute_correlate(a, k, mode="edge"):
    """The kernel laid over each pixel as it is, with numpy's padding mode for the outside."""
    kh, kw = k.shape
    pad = np.pad(a, ((kh // 2, kh // 2), (kw // 2, kw // 2)) + ((0, 0),) * (a.ndim - 2), mode=mode)
    out = np.zeros(a.shape, np.float64)
    for y in range(kh):
        for x in range(kw):
            out += k[y, x] * pad[y:y + a.shape[0], x:x + a.shape[1]]
    return out


def test_to_f32_and_back_is_exact_for_every_8_bit_value():
    a = np.arange(256, dtype=np.uint8).reshape(16, 16, 1)
    f = stb.to_f32(stb.Image(a))
    assert f.dtype == np.float32 and f.shape == (16, 16, 1)
    assert f.min() == 0.0 and f.max() == 1.0
    assert np.array_equal(np.asarray(stb.to_8u(f)), a)
    assert np.array_equal(np.asarray(stb.Image(a).to_f32()), f)


def test_to_8u_rounds_clamps_and_turns_nan_into_zero():
    f = np.array([[0.0, 1.0, -0.5, 1.5, np.nan, 0.5]], dtype=np.float32)
    assert np.asarray(stb.to_8u(f)).ravel().tolist() == [0, 255, 0, 255, 0, 128]


def test_gaussian_blur_matches_a_numpy_convolution():
    a = randf(25, 31, 3)
    k1 = np.asarray(stb.gaussian_kernel(1.7)).ravel()
    want = brute_correlate(a, np.outer(k1, k1), "edge")
    got = stb.gaussian_blur(a, 1.7)
    assert got.shape == a.shape and got.dtype == np.float32
    assert np.abs(got - want).max() < 1e-5


def test_2d_arrays_stay_2d_and_images_stay_images():
    assert stb.gaussian_blur(randf(9, 7), 1.0).shape == (9, 7)
    assert stb.box_blur(randf(9, 7, 3), 2).shape == (9, 7, 3)
    img = stb.Image(rand8(9, 7, 4))
    out = stb.gaussian_blur(img, 1.0)
    assert isinstance(out, stb.Image) and (out.width, out.height) == (7, 9) and out.format == stb.PixelFormat.RGBA


def test_functions_and_methods_agree():
    img = stb.Image(rand8(20, 24, 3))
    assert np.array_equal(np.asarray(stb.gaussian_blur(img, 1.2, edge="wrap")), np.asarray(img.gaussian_blur(1.2, edge="wrap")))
    assert np.array_equal(np.asarray(stb.box_blur(img, 2)), np.asarray(img.box_blur(2)))
    k = [[0, -1, 0], [-1, 5, -1], [0, -1, 0]]
    assert np.array_equal(np.asarray(stb.convolve(img, k)), np.asarray(img.convolve(k)))


def test_an_8_bit_filter_is_the_float_filter_rounded_once():
    img = stb.Image(rand8(30, 33, 3))
    want = stb.to_8u(stb.gaussian_blur(stb.to_f32(img), 2.0))
    assert np.array_equal(np.asarray(stb.gaussian_blur(img, 2.0)), np.asarray(want))


def test_box_blur_is_the_mean_of_the_window():
    a = randf(14, 12)
    want = brute_correlate(a, np.full((5, 5), 1 / 25), "edge")
    assert np.abs(stb.box_blur(a, 2) - want).max() < 1e-5
    assert np.array_equal(stb.box_blur(a, 0), a)


@pytest.mark.parametrize("edge,numpy_mode", [("clamp", "edge"), ("reflect", "symmetric"), ("mirror", "reflect"), ("wrap", "wrap"),
                                             ("zero", "constant")])
def test_every_edge_mode_matches_numpy_padding(edge, numpy_mode):
    a = randf(11, 9)
    k = np.random.default_rng(3).random((5, 3)).astype(np.float32)
    want = brute_correlate(a, k, numpy_mode)
    assert np.abs(stb.convolve(a, k, edge=edge) - want).max() < 1e-4


def test_edge_accepts_the_enum_and_rejects_nonsense():
    a = randf(6, 6)
    assert np.array_equal(stb.box_blur(a, 1, edge=stb.EdgeMode.MIRROR), stb.box_blur(a, 1, edge="mirror"))
    assert np.array_equal(stb.box_blur(a, 1, edge="MIRROR"), stb.box_blur(a, 1, edge="mirror"))
    with pytest.raises(ValueError, match="edge must be one of"):
        stb.box_blur(a, 1, edge="nope")


def test_a_separable_convolution_is_the_product_kernel():
    a = randf(16, 20, 2)
    row, col = [1, 2, 1], [[-1], [0], [1]]
    full = np.outer([-1, 0, 1], [1, 2, 1]).astype(np.float32)
    want = np.stack([brute_correlate(a[..., c], full) for c in range(2)], axis=-1)
    assert np.abs(stb.convolve(a, row, col) - want).max() < 1e-4


def test_kernels_may_be_lists_float64_arrays_or_float32_arrays():
    a = randf(10, 10)
    ref = stb.convolve(a, [[0, 1, 0], [1, 2, 1], [0, 1, 0]])
    k = np.array([[0, 1, 0], [1, 2, 1], [0, 1, 0]])
    assert np.array_equal(stb.convolve(a, k), ref)
    assert np.array_equal(stb.convolve(a, k.astype(np.float32)), ref)
    assert np.array_equal(stb.convolve(a, k.astype(np.float32).T.copy().T), ref)  # a non-contiguous view works too


def test_sobel_of_a_step_and_its_magnitude():
    a = np.zeros((6, 8), np.float32)
    a[:, 4:] = 1.0
    dx, dy = stb.sobel(a)
    assert dx.shape == a.shape and np.all(dy == 0)
    assert dx[2].tolist() == [0, 0, 0, 4, 4, 0, 0, 0]
    assert stb.magnitude(dx, dy)[2, 3] == 4.0
    dx8, _ = stb.sobel(stb.Image((a * 255).astype(np.uint8)[..., None]))
    assert dx8.shape == (6, 8, 1) and dx8[2, 3, 0] == 4.0  # an 8-bit input is read as value / 255


def test_bad_arguments_raise_value_error():
    img, a = stb.Image(rand8(8, 8, 3)), randf(8, 8)
    for call in (lambda: stb.gaussian_blur(img, 0.0), lambda: stb.gaussian_blur(a, float("nan")),
                 lambda: stb.box_blur(a, -1), lambda: stb.convolve(a, [[1, 2], [3, 4]]),
                 lambda: stb.convolve(a, [[1, 2, 3], [4, 5, 6], [7, 8, 9]], [1, 2, 1]),
                 lambda: stb.gaussian_blur(np.zeros((0, 3), np.float32), 1.0), lambda: stb.gaussian_blur(randf(2, 2, 2).reshape(2, 2, 2, 1), 1.0),
                 lambda: stb.to_8u(np.zeros((4, 4, 5), np.float32)), lambda: stb.magnitude(a, randf(8, 9))):
        with pytest.raises(ValueError):
            call()


def test_results_do_not_depend_on_the_thread_count():
    a = randf(300, 210, 3)
    with stb.threads(1):
        one = stb.gaussian_blur(a, 3.0), stb.box_blur(a, 5)
    with stb.threads(4):
        four = stb.gaussian_blur(a, 3.0), stb.box_blur(a, 5)
    assert all(np.array_equal(x, y) for x, y in zip(one, four))


def test_results_do_not_depend_on_the_simd_backend():
    a = randf(77, 91, 3)
    results = []
    for be in stb.simd_backends():
        with stb.simd(be):
            results.append((stb.gaussian_blur(a, 2.2), stb.convolve(a, np.ones((5, 5), np.float32) / 25, edge="mirror")))
    assert all(np.array_equal(r[0], results[0][0]) and np.array_equal(r[1], results[0][1]) for r in results)


def test_matches_scipy_when_it_is_installed():
    ndimage = pytest.importorskip("scipy.ndimage")
    a = randf(40, 50)
    k = np.random.default_rng(5).random((5, 5)).astype(np.float32)
    want = ndimage.correlate(a.astype(np.float64), k.astype(np.float64), mode="nearest")
    assert np.abs(stb.convolve(a, k) - want).max() < 1e-4
