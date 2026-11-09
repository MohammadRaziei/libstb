"""Image.premultiplied() / unpremultiplied(): straight <-> premultiplied alpha, bytes kept in RGBA order."""

import pytest

import libstb as stb

np = pytest.importorskip("numpy")


def rgba(*pixels):
    return stb.Image(np.array([pixels], dtype=np.uint8))


def test_premultiplied_matches_the_plain_formula_for_every_colour_and_alpha():
    c, a = np.meshgrid(np.arange(256), np.arange(256))  # x: colour value, y: alpha
    straight = np.stack([c, 255 - c, (c * 7) & 255, a], axis=-1).astype(np.uint8)
    got = np.asarray(stb.Image(straight).premultiplied())
    want = straight.copy()
    for k in range(3):
        want[..., k] = (straight[..., k].astype(np.int64) * a // 255).astype(np.uint8)
    assert np.array_equal(got, want)


def test_unpremultiplied_matches_the_plain_formula_for_valid_input():
    c, a = np.meshgrid(np.arange(256), np.arange(1, 256))
    c = np.minimum(c, a)  # a premultiplied colour never exceeds its alpha
    pre = np.stack([c, c, c, a], axis=-1).astype(np.uint8)
    got = np.asarray(stb.Image(pre).unpremultiplied())
    want = pre.copy()
    for k in range(3):
        want[..., k] = (pre[..., k].astype(np.int64) * 255 // a).astype(np.uint8)
    assert np.array_equal(got, want)


def test_special_alphas_and_the_original_is_untouched():
    img = rgba((10, 20, 30, 255), (200, 100, 50, 0), (90, 80, 70, 128))
    before = np.asarray(img).copy()
    pre = np.asarray(img.premultiplied())
    assert np.array_equal(np.asarray(img), before)
    assert pre[0, 0].tolist() == [10, 20, 30, 255]  # opaque: unchanged
    assert pre[0, 1].tolist() == [0, 0, 0, 0]  # clear: zeros
    assert pre[0, 2].tolist() == [45, 40, 35, 128]
    back = np.asarray(stb.Image(pre).unpremultiplied())
    assert back[0, 0].tolist() == [10, 20, 30, 255]
    assert back[0, 1].tolist() == [0, 0, 0, 0]
    assert (back[0, 2, :3] <= before[0, 2, :3]).all()  # precision is lost, never gained


def test_gray_alpha_and_formats_without_alpha():
    ga = stb.Image(np.array([[[200, 255], [200, 0], [200, 128]]], dtype=np.uint8))
    pre = np.asarray(ga.premultiplied())
    assert pre[0].tolist() == [[200, 255], [0, 0], [100, 128]]
    assert np.asarray(stb.Image(pre).unpremultiplied())[0, 2].tolist() == [199, 128]

    rgb = np.arange(2 * 3 * 3, dtype=np.uint8).reshape(2, 3, 3)
    assert np.array_equal(np.asarray(stb.Image(rgb).premultiplied()), rgb)
    gray = np.arange(6, dtype=np.uint8).reshape(2, 3)
    assert np.array_equal(np.asarray(stb.Image(gray).unpremultiplied()), np.asarray(stb.Image(gray)))


@pytest.mark.parametrize("backend", stb.simd_backends())
def test_every_simd_backend_gives_the_same_bytes(backend):
    rng = np.random.default_rng(1)
    img = stb.Image(rng.integers(0, 256, (33, 47, 4), dtype=np.uint8))
    with stb.simd("scalar"):
        want_pre, want_back = np.asarray(img.premultiplied()), np.asarray(img.unpremultiplied())
    with stb.simd(backend):
        assert np.array_equal(np.asarray(img.premultiplied()), want_pre)
        assert np.array_equal(np.asarray(img.unpremultiplied()), want_back)
