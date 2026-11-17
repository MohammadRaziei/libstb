"""Tone: histogram, equalize, auto_contrast, match_histogram, gamma, levels and look-up tables. The small cases are worked
out by hand (see tests/cpp/test_tone.cpp, which has the same ones); here the point is the Python surface: Image and float32
array forms, strings and ToneMode, inplace, errors."""

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb


def gray_row(values):
    return stb.Image(np.array(values, dtype=np.uint8).reshape(1, len(values), 1))


def rand8(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def randf(h, w, c=None, seed=0):
    shape = (h, w) if c is None else (h, w, c)
    return np.random.default_rng(seed).random(shape, dtype=np.float32)


def test_histogram_counts_each_level_of_each_channel():
    img = stb.Image(np.array([[[10, 20, 30], [10, 25, 30]]], dtype=np.uint8))
    h = stb.histogram(img)
    assert h.dtype == np.int32 and h.shape == (3, 256)
    assert h[0, 10] == 2 and h[1, 20] == 1 and h[1, 25] == 1 and h[2, 30] == 2
    assert h.sum() == 6
    assert np.array_equal(img.histogram(), h)
    # shape (channels, 256) is what np.bincount gives for each channel
    a = rand8(9, 7, 4, seed=3)
    hh = stb.histogram(stb.Image(a))
    for k in range(4):
        assert np.array_equal(hh[k], np.bincount(a[..., k].ravel(), minlength=256))


def test_float_histogram_has_the_bins_asked_for_and_skips_nan():
    f = np.array([[0.0, 0.3, 1.0, 5.0, -2.0, np.nan]], dtype=np.float32)
    h = stb.histogram(f, bins=4)
    assert h.shape == (1, 4)
    assert h[0].tolist() == [2, 1, 0, 2]
    assert stb.histogram(f).shape == (1, 256)
    with pytest.raises(ValueError):
        stb.histogram(f, bins=0)


def test_equalize_matches_the_hand_computed_table():
    out = np.asarray(stb.equalize(gray_row([0, 0, 1, 1, 2, 2, 3, 3])))
    assert out.reshape(-1).tolist() == [0, 0, 85, 85, 170, 170, 255, 255]


def test_auto_contrast_stretches_to_the_ends():
    out = np.asarray(stb.auto_contrast(gray_row([50, 100, 150])))
    assert out.reshape(-1).tolist() == [0, 128, 255]
    ten = gray_row([10, 20, 30, 40, 50, 60, 70, 80, 90, 100])
    cut = np.asarray(stb.auto_contrast(ten, cutoff=20.0)).reshape(-1)
    assert cut.tolist() == [0, 0, 0, 51, 102, 153, 204, 255, 255, 255]


def test_module_function_and_method_agree_and_leave_the_source_alone():
    img = stb.Image(rand8(20, 30, 3, seed=1))
    before = np.asarray(img).copy()
    for fn, args in ((stb.equalize, ()), (stb.auto_contrast, (2.0,)), (stb.gamma, (1.7,)), (stb.levels, (0.1, 0.7))):
        a = fn(img, *args)
        b = getattr(img, fn.__name__)(*args)
        assert np.array_equal(np.asarray(a), np.asarray(b))
        assert not np.array_equal(np.asarray(a), before)
        assert np.array_equal(np.asarray(img), before)  # the source is untouched


def test_inplace_changes_the_image_and_returns_it():
    img = stb.Image(rand8(12, 12, 3, seed=2))
    want = np.asarray(stb.equalize(img)).copy()
    out = img.equalize(inplace=True)
    assert out is img
    assert np.array_equal(np.asarray(img), want)
    for call in (lambda i: i.auto_contrast(1.0, inplace=True), lambda i: i.gamma(1.4, inplace=True),
                 lambda i: i.levels(0.1, 0.9, 1.2, inplace=True), lambda i: i.match_histogram(stb.Image(rand8(5, 5, 3)), inplace=True)):
        before = np.asarray(img).copy()
        assert call(img) is img
        assert not np.array_equal(np.asarray(img), before)


def test_the_mode_is_a_string_or_a_tone_mode_and_changes_the_result():
    img = stb.Image(rand8(30, 30, 3, seed=4))
    luma = np.asarray(stb.equalize(img))
    assert np.array_equal(np.asarray(stb.equalize(img, "luma")), luma)
    assert np.array_equal(np.asarray(stb.equalize(img, stb.ToneMode.LUMA)), luma)
    per = np.asarray(stb.equalize(img, mode="per_channel"))
    assert np.array_equal(per, np.asarray(stb.equalize(img, stb.ToneMode.PER_CHANNEL)))
    assert not np.array_equal(per, luma)
    with pytest.raises(ValueError):
        stb.equalize(img, "bogus")


def test_alpha_is_never_changed():
    a = rand8(8, 8, 4, seed=5)
    img = stb.Image(a)
    for out in (stb.equalize(img), stb.auto_contrast(img, 5.0, "per_channel"), stb.gamma(img, 2.0), stb.levels(img, 0.2, 0.8)):
        assert np.array_equal(np.asarray(out)[..., 3], a[..., 3])


def test_float_arrays_keep_their_shape_and_work_like_the_image():
    for shape in ((16, 20), (16, 20, 1), (16, 20, 3), (16, 20, 4)):
        f = randf(*shape[:2], shape[2] if len(shape) == 3 else None, seed=6)
        for out in (stb.equalize(f), stb.auto_contrast(f, 2.0), stb.gamma(f, 2.0), stb.levels(f, 0.1, 0.9, 1.3),
                    stb.match_histogram(f, randf(5, 6, shape[2] if len(shape) == 3 else None, seed=7))):
            assert out.dtype == np.float32 and out.shape == f.shape
            assert out.min() >= 0.0 and out.max() <= 1.0
    f = np.array([[0.0, 0.25, 1.0, 0.5]], dtype=np.float32)
    assert np.allclose(stb.gamma(f, 2.0)[0], [0.0, 0.5, 1.0, np.sqrt(0.5)], atol=1e-6)
    assert np.allclose(stb.levels(f, 0.25, 0.75)[0], [0.0, 0.0, 1.0, 0.5], atol=1e-6)


def test_gamma_and_levels_tables_and_apply_lut():
    g = stb.gamma_lut(2.0)
    assert g.dtype == np.uint8 and g.shape == (256,)
    assert g[0] == 0 and g[64] == 128 and g[255] == 255
    lv = stb.levels_lut(0.2, 0.8)
    assert lv[51] == 0 and lv[204] == 255
    img = stb.Image(rand8(10, 10, 3, seed=8))
    assert np.array_equal(np.asarray(stb.apply_lut(img, g)), np.asarray(stb.gamma(img, 2.0)))
    assert np.array_equal(np.asarray(img.apply_lut(lv)), np.asarray(stb.levels(img, 0.2, 0.8)))
    a = rand8(6, 6, 4, seed=9)
    invert = np.arange(255, -1, -1, dtype=np.uint8)
    assert np.array_equal(np.asarray(stb.apply_lut(stb.Image(a), invert)), 255 - a)  # all four channels, alpha too
    per = np.stack([np.arange(256, dtype=np.uint8)] * 3 + [invert], axis=1)  # shape (256, 4)
    out = np.asarray(stb.apply_lut(stb.Image(a), per))
    assert np.array_equal(out[..., :3], a[..., :3]) and np.array_equal(out[..., 3], 255 - a[..., 3])


def test_bad_arguments_raise_value_error():
    img = stb.Image(rand8(4, 4, 3))
    for call in (lambda: stb.gamma(img, 0.0), lambda: stb.gamma(img, -1.0), lambda: stb.levels(img, 0.5, 0.5),
                 lambda: stb.auto_contrast(img, 50.0), lambda: stb.gamma_lut(0.0), lambda: stb.levels_lut(0.9, 0.1),
                 lambda: stb.apply_lut(img, np.zeros(255, np.uint8)), lambda: stb.apply_lut(img, np.zeros((256, 2), np.uint8)),
                 lambda: stb.match_histogram(img, stb.Image(rand8(4, 4, 1))),
                 lambda: stb.equalize(np.zeros((4, 4, 5), np.float32))):
        with pytest.raises(ValueError):
            call()


def test_the_result_does_not_depend_on_the_number_of_threads():
    img = stb.Image(rand8(700, 900, 3, seed=11))
    f = randf(400, 500, 3, seed=12)
    with stb.threads(1):
        a = (np.asarray(stb.equalize(img)).copy(), stb.levels(f, 0.1, 0.9, 1.4).copy())
    with stb.threads(4):
        b = (np.asarray(stb.equalize(img)).copy(), stb.levels(f, 0.1, 0.9, 1.4).copy())
    assert np.array_equal(a[0], b[0]) and np.array_equal(a[1], b[1])


# ------------------------------------------------------------------------------------------------------- clahe

def clahe_reference(a, clip_limit, tx, ty):
    """The algorithm of stb::clahe for a gray uint8 picture, written again with numpy (doubles; ties may differ by one level)."""
    h, w = a.shape

    def edge(t, n, tiles):
        return -(-t * n // tiles)  # ceil(t * n / tiles)

    tables = np.zeros((ty, tx, 256))
    for j in range(ty):
        for i in range(tx):
            tile = a[edge(j, h, ty):edge(j + 1, h, ty), edge(i, w, tx):edge(i + 1, w, tx)]
            hist = np.bincount(tile.ravel(), minlength=256).astype(np.int64)
            n = hist.sum()
            if (hist != 0).sum() < 2:
                tables[j, i] = np.arange(256) / 255.0
                continue
            clip = max(1, int(clip_limit * n / 256))
            excess = int(np.maximum(hist - clip, 0).sum())
            hist = np.minimum(hist, clip) + excess // 256
            residual = excess % 256
            if residual:
                step = max(256 // residual, 1)
                for k in range(0, 256, step):
                    if residual == 0:
                        break
                    hist[k] += 1
                    residual -= 1
            tables[j, i] = np.cumsum(hist) / n

    def axis(n, tiles):
        u = (np.arange(n) + 0.5) * tiles / n - 0.5
        a0 = np.zeros(n, dtype=int)
        b0 = np.zeros(n, dtype=int)
        f = np.zeros(n)
        for q in range(n):
            if u[q] <= 0:
                continue
            k = int(u[q])
            if k >= tiles - 1:
                a0[q] = b0[q] = tiles - 1
            else:
                a0[q], b0[q], f[q] = k, k + 1, u[q] - k
        return a0, b0, f

    xa, xb, fx = axis(w, tx)
    ya, yb, fy = axis(h, ty)
    out = np.zeros((h, w))
    yy, xx = np.mgrid[0:h, 0:w]
    lv = a.astype(int)

    def at(j, i):
        return tables[j[:, None], i[None, :], lv]

    top = (1 - fx)[None, :] * at(ya, xa) + fx[None, :] * at(ya, xb)
    bottom = (1 - fx)[None, :] * at(yb, xa) + fx[None, :] * at(yb, xb)
    out = (1 - fy)[:, None] * top + fy[:, None] * bottom
    return np.clip((out * 255 + 0.5).astype(int), 0, 255).astype(np.uint8)


@pytest.mark.parametrize("clip_limit,tx,ty", [(1.0, 1, 1), (2.0, 4, 3), (4.0, 5, 2), (1.0e6, 3, 3)])
def test_clahe_matches_a_numpy_version_of_the_algorithm(clip_limit, tx, ty):
    a = rand8(37, 44, 1, seed=21)[..., 0]
    a[:, :20] //= 4  # darker on the left, so the tiles differ
    got = np.asarray(stb.clahe(stb.Image(a[..., None]), clip_limit, tx, ty))[..., 0]
    want = clahe_reference(a, clip_limit, tx, ty)
    assert np.abs(got.astype(int) - want.astype(int)).max() <= 1


def test_clahe_is_close_to_scikit_images_adaptive_equalisation():
    skimage = pytest.importorskip("skimage")
    from skimage import data, exposure
    a = data.camera()[::4, ::4].copy()
    got = np.asarray(stb.clahe(stb.Image(a[..., None]), 2.0, 8, 8))[..., 0].astype(float)
    # the clip limits are not the same quantity (skimage's is a fraction), so only the look is compared, not the numbers
    want = exposure.equalize_adapthist(a, kernel_size=(16, 16), clip_limit=0.01) * 255
    assert np.corrcoef(got.ravel(), want.ravel())[0, 1] > 0.95
    assert got.std() > a.std()  # it did raise the local contrast


def test_clahe_forms_strings_and_inplace():
    img = stb.Image(rand8(24, 30, 3, seed=22))
    new = img.clahe(2.0, 3, 2)
    assert new is not img and np.asarray(new).shape == (24, 30, 3)
    assert np.array_equal(np.asarray(new), np.asarray(stb.clahe(img, 2.0, 3, 2)))
    assert np.array_equal(np.asarray(img.clahe(2.0, 3, 2, "per_channel")), np.asarray(stb.clahe(img, 2.0, 3, 2, stb.ToneMode.PER_CHANNEL)))
    before = np.asarray(img).copy()
    again = img.clahe(2.0, 3, 2, inplace=True)
    assert again is img and np.array_equal(np.asarray(img), np.asarray(new)) and not np.array_equal(np.asarray(img), before)
    # defaults: clip_limit 2, 8 x 8 tiles (tiles_y 0 is the same as tiles_x)
    big = stb.Image(rand8(32, 40, 1, seed=23))
    assert np.array_equal(np.asarray(stb.clahe(big)), np.asarray(stb.clahe(big, 2.0, 8, 8)))


def test_clahe_on_float_arrays():
    f = randf(20, 26, 3, seed=24)
    out = stb.clahe(f, 3.0, 2, 2)
    assert out.dtype == np.float32 and out.shape == f.shape and out.min() >= 0.0 and out.max() <= 1.0
    g = randf(20, 26, seed=25)  # (H, W)
    assert stb.clahe(g, 3.0, 2).shape == g.shape
    flat = np.full((9, 9), 0.4, dtype=np.float32)
    assert np.array_equal(stb.clahe(flat, 2.0, 3), flat)


def test_clahe_rejects_bad_arguments():
    img = stb.Image(rand8(10, 8, 1, seed=26))
    for args in [(0.5,), (float("nan"),), (2.0, 0), (2.0, 4, -1), (2.0, 9, 1), (2.0, 2, 11)]:
        with pytest.raises(ValueError):
            stb.clahe(img, *args)
