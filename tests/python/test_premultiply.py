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


# ----------------------------------------------------------------------------- PremultipliedImage


def test_premultiplied_image_is_its_own_type_without_image_methods():
    p = stb.PremultipliedImage(3, 2)
    assert (p.width, p.height, p.stride, p.empty) == (3, 2, 12, False)
    assert not isinstance(p, stb.Image)
    for name in ("blend", "to_png", "resize", "fill", "flatten"):
        assert not hasattr(p, name)
    with pytest.raises(ValueError):
        stb.PremultipliedImage(0, 2)
    assert "PremultipliedImage" in repr(p)


def test_from_image_and_to_image_round_trip_and_agree_with_the_array_view():
    c, a = np.meshgrid(np.arange(256), np.arange(256))
    straight = np.stack([c, 255 - c, (c * 7) & 255, a], axis=-1).astype(np.uint8)
    p = stb.PremultipliedImage.from_image(stb.Image(straight))
    words = np.asarray(p)
    assert words.dtype == np.uint32 and words.shape == (256, 256)
    ref = np.asarray(stb.Image(straight).premultiplied()).astype(np.uint32)  # R, G, B, A bytes, premultiplied
    want = (ref[..., 3] << 24) | (ref[..., 0] << 16) | (ref[..., 1] << 8) | ref[..., 2]
    assert np.array_equal(words, want)
    back = np.asarray(p.to_image())
    assert back.shape == (256, 256, 4)
    assert np.array_equal(back, np.asarray(stb.Image(np.asarray(stb.Image(straight).premultiplied())).unpremultiplied()))


def test_other_formats_are_taken_as_opaque_rgba_and_copy_is_independent():
    gray = np.arange(12, dtype=np.uint8).reshape(3, 4)
    p = stb.PremultipliedImage.from_image(stb.Image(gray))
    back = np.asarray(p.to_image())
    assert np.array_equal(back[..., 0], gray) and (back[..., 3] == 255).all()
    before = np.asarray(p).copy()
    q = p.copy()
    np.asarray(q)[:] = 0
    assert before.any() and np.array_equal(np.asarray(p), before)  # the copy did not write through to p


def test_the_array_view_writes_through():
    p = stb.PremultipliedImage(2, 2)
    np.asarray(p)[1, 1] = 0xFF102030
    back = np.asarray(p.to_image())
    assert back[1, 1].tolist() == [0x10, 0x20, 0x30, 0xFF]
