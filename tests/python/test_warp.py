"""warp_affine, warp_perspective, Homography and Interpolation. The C++ tests (tests/cpp/test_warp.cpp) work the small cases out by
hand and compare every kernel with a sampler in doubles; here the values are compared with scipy where it has the same thing
(map_coordinates, bilinear and nearest), a linear ramp (which bilinear and Catmull-Rom reproduce exactly), and the Python surface
is checked: Image and float32 array forms, Matrix and number sequences, strings and enums, errors. scipy is optional."""

import math

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb


def rand8(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def randf(h, w, c=None, seed=0):
    shape = (h, w) if c is None else (h, w, c)
    return np.random.default_rng(seed).random(shape, dtype=np.float32)


def turn_about(cx, cy, radians):
    # the last call acts first: the centre to the origin, the turn, and back
    return stb.Matrix().translate(cx, cy).rotate(radians).translate(-cx, -cy)


def source_points(matrix, w, h):
    """For every pixel of a w x h result, the point of the source it reads, in units of pixel centres (x - 0.5 convention)."""
    inv = matrix.inverse()
    ys, xs = np.mgrid[0:h, 0:w]
    px = np.empty((h, w))
    py = np.empty((h, w))
    for y in range(h):
        for x in range(w):
            px[y, x], py[y, x] = inv.map(x + 0.5, y + 0.5)
    return px - 0.5, py - 0.5


def test_bilinear_matches_scipy_map_coordinates():
    ndimage = pytest.importorskip("scipy.ndimage")
    f = randf(21, 25, seed=51)
    m = turn_about(12, 10, 0.4).scale(1.2, 0.9)
    got = stb.warp_affine(f, m, size=(27, 23), interpolation="bilinear", edge="zero")
    px, py = source_points(m, 27, 23)
    want = ndimage.map_coordinates(f.astype(np.float64), [py, px], order=1, mode="grid-constant", cval=0.0)
    assert got.shape == (23, 27) and got.dtype == np.float32
    assert np.abs(got - want).max() < 2e-4
    img = rand8(21, 25, 1, seed=52)
    got8 = np.asarray(stb.warp_affine(stb.Image(img), m, size=(27, 23)))[..., 0].astype(np.float64)
    want8 = ndimage.map_coordinates(img[..., 0].astype(np.float64), [py, px], order=1, mode="grid-constant", cval=0.0)
    assert np.abs(got8 - want8).max() <= 1.0


def test_nearest_reads_the_pixel_the_point_falls_in():
    img = rand8(6, 8, 3, seed=53)
    out = np.asarray(stb.warp_affine(stb.Image(img), stb.Matrix.scaling(2), size=(16, 12), interpolation="nearest", edge="clamp"))
    assert np.array_equal(out, np.repeat(np.repeat(img, 2, axis=0), 2, axis=1))  # every pixel twice, both ways


@pytest.mark.parametrize("interp", ["bilinear", "bicubic"])
def test_a_linear_ramp_is_reproduced_exactly_inside(interp):
    yy, xx = np.mgrid[0:20, 0:24]
    ramp = (0.1 * xx + 0.05 * yy).astype(np.float32)
    out = stb.warp_affine(ramp, stb.Matrix.translation(0.3, 0.7), interpolation=interp, edge="clamp")
    want = 0.1 * (xx - 0.3) + 0.05 * (yy - 0.7)  # the ramp at the point each pixel reads
    assert np.abs(out[3:-3, 3:-3] - want[3:-3, 3:-3]).max() < 1e-4


def test_forms_strings_enums_and_number_sequences_agree():
    img = stb.Image(rand8(14, 18, 4, seed=54))
    m = turn_about(9, 7, 0.3)
    a = np.asarray(stb.warp_affine(img, m, size=(20, 16), interpolation="bicubic", edge="mirror"))
    assert np.array_equal(a, np.asarray(img.warp_affine(m, size=(20, 16), interpolation=stb.Interpolation.BICUBIC, edge=stb.EdgeMode.MIRROR)))
    assert np.array_equal(a, np.asarray(stb.warp_affine(img, tuple(m), size=[20, 16], interpolation="bicubic", edge="mirror")))  # six numbers
    assert np.array_equal(a, np.asarray(stb.warp_perspective(img, m, size=(20, 16), interpolation="bicubic", edge="mirror")))  # a Matrix is a homography
    assert np.array_equal(a, np.asarray(stb.warp_perspective(img, stb.Homography(m), size=(20, 16), interpolation="bicubic", edge="mirror")))
    assert np.array_equal(a, np.asarray(img.warp_perspective(list(stb.Homography(m)), size=(20, 16), interpolation="bicubic", edge="mirror")))
    # the size defaults to the picture's; the identity gives the picture back
    assert np.array_equal(np.asarray(stb.warp_affine(img, stb.Matrix())), np.asarray(img))
    f = randf(10, 12, 3, seed=55)
    assert stb.warp_affine(f, m).shape == (10, 12, 3)
    assert stb.warp_affine(f, m, size=(5, 4)).shape == (4, 5, 3)
    assert np.array_equal(stb.warp_affine(f, stb.Matrix()), f)


def test_outside_is_zero_by_default_and_the_edge_mode_changes_it():
    img = stb.Image(np.full((6, 6, 4), 200, dtype=np.uint8))
    shifted = np.asarray(img.warp_affine(stb.Matrix.translation(3, 0), interpolation="nearest"))
    assert shifted[:, :3].max() == 0 and shifted[:, 3:].min() == 200  # transparent black on the left, an RGBA picture
    clamped = np.asarray(img.warp_affine(stb.Matrix.translation(3, 0), interpolation="nearest", edge="clamp"))
    assert clamped.min() == 200


def test_homography_from_four_points():
    src = [(0, 0), (10, 0), (10, 8), (0, 8)]
    dst = [(1, 2), (12, 1), (9, 10), (-1, 7)]
    h = stb.Homography.from_quad(src, dst)
    for s, d in zip(src, dst):
        x, y = h.map(*s)
        assert abs(x - d[0]) < 1e-3 and abs(y - d[1]) < 1e-3
    back = h.inverse()
    x, y = back.map(*h.map(3.0, 4.0))
    assert abs(x - 3.0) < 1e-3 and abs(y - 4.0) < 1e-3
    x, y = h.then(back).map(5.0, 5.0)
    assert abs(x - 5.0) < 1e-3 and abs(y - 5.0) < 1e-3
    assert len(list(h)) == 9 and h == stb.Homography(list(h)) and stb.Homography() == stb.Homography(stb.Matrix())
    assert repr(stb.Homography()).startswith("Homography(1, 0, 0;")
    with pytest.raises(ValueError):
        stb.Homography.from_quad([(0, 0), (1, 1), (2, 2), (3, 3)], dst)  # three on a line
    with pytest.raises(ValueError):
        stb.Homography.from_quad(src[:3], dst[:3])  # not four points
    assert stb.Homography([0.0] * 9).inverse() is None
    nan_x, nan_y = stb.Homography([0.0] * 9).map(1.0, 1.0)
    assert math.isnan(nan_x) and math.isnan(nan_y)


def test_a_perspective_warp_squeezes_the_top_edge():
    img = stb.Image(np.full((32, 32, 1), 200, dtype=np.uint8))
    h = stb.Homography.from_quad([(0, 0), (32, 0), (32, 32), (0, 32)], [(8, 0), (24, 0), (32, 32), (0, 32)])
    out = np.asarray(stb.warp_perspective(img, h))[..., 0]
    assert out[1, 2] == 0 and out[1, 29] == 0 and out[1, 16] == 200  # the trapezoid
    assert out[30, 2] == 200 and out[30, 29] == 200  # the bottom keeps its width


def test_warp_errors():
    img = stb.Image(rand8(8, 6, 3, seed=56))
    f = randf(8, 6, seed=57)
    for call in (stb.warp_affine, stb.warp_perspective):
        for pic in (img, f):
            with pytest.raises(ValueError):
                call(pic, stb.Matrix.scaling(0, 1))  # no inverse
            with pytest.raises(ValueError):
                call(pic, stb.Matrix(), size=(-1, 4))
            with pytest.raises(ValueError):
                call(pic, stb.Matrix(), size=(100000, 100000))
            with pytest.raises(ValueError):
                call(pic, stb.Matrix(), size=(3,))  # not (width, height)
            with pytest.raises(ValueError):
                call(pic, stb.Matrix(), interpolation="spline")
            with pytest.raises(ValueError):
                call(pic, stb.Matrix(), edge="nope")
            with pytest.raises(ValueError):
                call(pic, "not a transform")
    with pytest.raises(ValueError):
        stb.warp_affine(img, (1, 0, 0, 1, 0))  # five numbers
