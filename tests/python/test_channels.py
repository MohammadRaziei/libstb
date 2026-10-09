"""convert / split / merge / composite / flatten, checked against Pillow."""

import numpy as np
import pytest
from PIL import Image as PILImage

import libstb

MODES = {1: "L", 2: "LA", 3: "RGB", 4: "RGBA"}


def rand(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def to_pil(a):
    # The mode is inferred from the shape (passing mode= is deprecated since Pillow 11.3).
    return PILImage.fromarray(a[..., 0] if a.shape[2] == 1 else a)


def from_pil(p):
    a = np.asarray(p)
    return a[..., None] if a.ndim == 2 else a


@pytest.mark.parametrize("src, dst", [(s, d) for s in range(1, 5) for d in range(1, 5) if s != d])
def test_convert_matches_pillow_exactly(src, dst):
    a = rand(7, 9, src, seed=src * 10 + dst)
    got = libstb.Image(a.copy()).convert(dst).numpy()
    np.testing.assert_array_equal(got, from_pil(to_pil(a).convert(MODES[dst])))


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_convert_to_the_same_count_is_an_independent_copy(c):
    a = rand(3, 4, c)
    img = libstb.Image(a.copy())
    out = img.convert(c)
    np.testing.assert_array_equal(out.numpy(), a)
    assert not np.shares_memory(out.numpy(), img.numpy())


def test_convert_round_trips_that_lose_nothing():
    a = rand(5, 5, 3)
    img = libstb.Image(a.copy())
    np.testing.assert_array_equal(img.convert(4).convert(3).numpy(), a)
    g = rand(5, 5, 1)
    gi = libstb.Image(g.copy())
    np.testing.assert_array_equal(gi.convert(3).convert(1).numpy(), g)
    np.testing.assert_array_equal(gi.convert(2).convert(1).numpy(), g)


@pytest.mark.parametrize("bad", [0, 5, -1])
def test_convert_rejects_a_bad_channel_count(bad):
    with pytest.raises(ValueError):
        libstb.Image(rand(2, 2, 3)).convert(bad)


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_split_and_merge_round_trip(c):
    a = rand(6, 5, c)
    parts = libstb.Image(a.copy()).split()
    assert isinstance(parts, tuple) and len(parts) == c
    for k, p in enumerate(parts):
        assert p.channels == 1
        np.testing.assert_array_equal(p.numpy()[..., 0], a[..., k])
    np.testing.assert_array_equal(libstb.Image.merge(parts).numpy(), a)
    np.testing.assert_array_equal(libstb.Image.merge(list(parts)).numpy(), a)
    np.testing.assert_array_equal(libstb.Image.merge(iter(parts)).numpy(), a)


def test_merge_can_reorder_channels():
    a = rand(3, 3, 3)
    r, g, b = libstb.Image(a.copy()).split()
    np.testing.assert_array_equal(libstb.Image.merge([b, g, r]).numpy(), a[..., ::-1])


def test_merge_rejects_wrong_input():
    one = libstb.Image(rand(3, 3, 1))
    with pytest.raises(ValueError):
        libstb.Image.merge([])
    with pytest.raises(ValueError):
        libstb.Image.merge([one] * 5)
    with pytest.raises(ValueError):
        libstb.Image.merge([one, libstb.Image(rand(3, 4, 1))])  # sizes differ
    with pytest.raises(ValueError):
        libstb.Image.merge([one, libstb.Image(rand(3, 3, 3))])  # not single-channel
    with pytest.raises(TypeError):
        libstb.Image.merge([one, rand(3, 3, 1)])  # not an Image


def alpha_composite_reference(base, over, x=0, y=0):
    """Pillow's alpha_composite of `over` placed at (x, y) on `base`, both RGBA arrays."""
    layer = PILImage.new("RGBA", (base.shape[1], base.shape[0]), (0, 0, 0, 0))
    layer.paste(PILImage.fromarray(over), (x, y))
    return np.asarray(PILImage.alpha_composite(PILImage.fromarray(base), layer))


@pytest.mark.parametrize("x, y", [(0, 0), (3, 2), (-2, 1), (5, -3), (-100, 0), (100, 100)])
def test_composite_positions_and_clipping_match_pillow(x, y):
    base, over = rand(8, 9, 4, 1), rand(4, 5, 4, 2)
    got = libstb.Image(base.copy()).composite(libstb.Image(over.copy()), x, y).numpy()
    want = alpha_composite_reference(base, over, x, y)
    np.testing.assert_array_equal(got[..., 3], want[..., 3])
    pm = lambda a: a[..., :3].astype(float) * a[..., 3:] / 255
    assert np.abs(pm(got) - pm(want)).max() <= 1.0
    if (x, y) in [(-100, 0), (100, 100)]:
        np.testing.assert_array_equal(got, base)  # entirely outside: unchanged


def test_composite_alpha_is_pillows_and_colour_is_never_wrong_for_any_pair_of_alphas():
    # Every (destination alpha, overlay alpha) pair, with one colour on both sides: the colour
    # must come back unchanged and the alpha must be exactly Pillow's.
    da, sa = np.meshgrid(np.arange(256), np.arange(1, 256))
    for c in (255, 100, 1):
        base = np.stack([np.full(da.shape, c)] * 3 + [da], -1).astype(np.uint8)
        over = np.stack([np.full(da.shape, c)] * 3 + [sa], -1).astype(np.uint8)
        got = libstb.Image(base.copy()).composite(libstb.Image(over.copy())).numpy()
        np.testing.assert_array_equal(got[..., :3], c)
        np.testing.assert_array_equal(got[..., 3], alpha_composite_reference(base, over)[..., 3])


def test_composite_matches_pillow_where_it_is_visible():
    rng = np.random.default_rng(0)
    base = rng.integers(0, 256, (200, 200, 4), dtype=np.uint8)
    over = rng.integers(0, 256, (200, 200, 4), dtype=np.uint8)
    got = libstb.Image(base.copy()).composite(libstb.Image(over.copy())).numpy().astype(float)
    want = alpha_composite_reference(base, over).astype(float)
    np.testing.assert_array_equal(got[..., 3], want[..., 3])
    premult = lambda x: x[..., :3] * x[..., 3:] / 255  # what actually reaches the screen
    assert np.abs(premult(got) - premult(want)).max() <= 1.0
    visible = want[..., 3] >= 32  # colour is ill-conditioned where alpha is almost 0
    assert np.abs(got[..., :3] - want[..., :3])[visible].max() <= 1.0


def test_composite_onto_an_image_without_alpha_matches_pillow():
    base, over = rand(6, 7, 3, 3), rand(6, 7, 4, 4)
    got = libstb.Image(base.copy()).composite(libstb.Image(over.copy()))
    assert got.channels == 3
    opaque = np.dstack([base, np.full(base.shape[:2], 255, np.uint8)])
    want = alpha_composite_reference(opaque, over)[..., :3]
    assert np.abs(got.numpy().astype(int) - want.astype(int)).max() <= 1


def test_composite_special_alphas_are_exact():
    base = np.full((1, 3, 4), (10, 20, 30, 255), np.uint8)
    over = np.array([[(200, 100, 50, 0), (200, 100, 50, 255), (7, 7, 7, 0)]], np.uint8)
    out = libstb.Image(base.copy()).composite(libstb.Image(over)).numpy()
    assert out[0, 0].tolist() == [10, 20, 30, 255]    # transparent overlay: untouched
    assert out[0, 1].tolist() == [200, 100, 50, 255]  # opaque overlay: replaces
    assert out[0, 2].tolist() == [10, 20, 30, 255]


def test_composite_with_gray_and_mixed_channel_counts():
    rgb = libstb.Image(np.full((1, 1, 3), 50, np.uint8))
    assert rgb.composite(libstb.Image(np.full((1, 1, 1), 77, np.uint8))).numpy()[0, 0].tolist() == [77, 77, 77]
    gray = libstb.Image(np.full((1, 1, 1), 50, np.uint8))
    out = gray.composite(libstb.Image(np.full((1, 1, 3), 90, np.uint8)))
    assert out.channels == 1 and out.numpy()[0, 0, 0] == 90


def test_composite_leaves_both_inputs_alone_and_needs_an_image():
    base, over = libstb.Image(rand(4, 4, 4)), libstb.Image(rand(2, 2, 4, 5))
    b0, o0 = base.numpy().copy(), over.numpy().copy()
    out = base.composite(over, 1, 1)
    np.testing.assert_array_equal(base.numpy(), b0)
    np.testing.assert_array_equal(over.numpy(), o0)
    assert not np.shares_memory(out.numpy(), base.numpy())
    with pytest.raises(TypeError):
        base.composite(rand(2, 2, 4))


def test_flatten_matches_compositing_onto_a_solid_background():
    a = rand(5, 6, 4, 9)
    for bg in [(255, 255, 255), (0, 0, 0), (10, 200, 30)]:
        got = libstb.Image(a.copy()).flatten(bg)
        assert got.channels == 3
        base = np.dstack([np.full((5, 6, 3), bg, np.uint8), np.full((5, 6), 255, np.uint8)])
        want = alpha_composite_reference(base, a)[..., :3]
        assert np.abs(got.numpy().astype(int) - want.astype(int)).max() <= 1


def test_flatten_defaults_to_white_and_handles_gray_alpha_and_no_alpha():
    img = libstb.Image(np.array([[(10, 20, 30, 0)]], np.uint8))
    assert img.flatten().numpy()[0, 0].tolist() == [255, 255, 255]
    ga = libstb.Image(np.array([[(100, 0)]], np.uint8)).flatten(200)
    assert ga.channels == 1 and ga.numpy()[0, 0, 0] == 200
    rgb = rand(3, 3, 3)
    out = libstb.Image(rgb.copy()).flatten()
    np.testing.assert_array_equal(out.numpy(), rgb)
    assert not np.shares_memory(out.numpy(), rgb)
    with pytest.raises(ValueError):
        libstb.Image(rand(2, 2, 4)).flatten((1, 2))
