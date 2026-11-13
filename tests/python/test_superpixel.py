"""Superpixels: SLIC, SLICO and SNIC through Image.superpixels / libstb.superpixels and the SuperpixelMap."""

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb

METHODS = ["slic", "slico", "snic"]


def noise(h, w, c=3, seed=0):
    return stb.Image(np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8))


def quadrants(n=80):
    a = np.zeros((n, n, 3), np.uint8)
    a[:n // 2, :n // 2] = (200, 30, 30)
    a[:n // 2, n // 2:] = (30, 200, 30)
    a[n // 2:, :n // 2] = (30, 30, 200)
    a[n // 2:, n // 2:] = (220, 220, 40)
    return stb.Image(a)


def pieces(labels):
    """How many 4-connected pieces each label has."""
    h, w = labels.shape
    seen = np.zeros((h, w), bool)
    count = {}
    for y0 in range(h):
        for x0 in range(w):
            if seen[y0, x0]:
                continue
            l = labels[y0, x0]
            count[l] = count.get(l, 0) + 1
            seen[y0, x0] = True
            stack = [(y0, x0)]
            while stack:
                y, x = stack.pop()
                for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                    if 0 <= ny < h and 0 <= nx < w and not seen[ny, nx] and labels[ny, nx] == l:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
    return count


@pytest.mark.parametrize("method", METHODS)
def test_labels_cover_the_image_with_connected_regions_near_the_requested_count(method):
    sp = noise(60, 70).superpixels(count=100, method=method)
    labels = sp.labels
    assert labels.shape == (60, 70) and labels.dtype == np.int32 and not labels.flags.writeable
    assert 50 <= sp.count <= 200 and labels.min() == 0 and labels.max() == sp.count - 1
    assert set(pieces(labels).values()) == {1}


@pytest.mark.parametrize("method", METHODS)
def test_regions_do_not_cross_borders_between_flat_colours(method):
    img = quadrants()
    sp = stb.superpixels(img, count=64, method=method)
    assert np.array_equal(np.asarray(sp.mean_color(img)), np.asarray(img))


def test_the_function_and_the_method_agree_and_the_enum_works():
    img = noise(40, 40)
    a = stb.superpixels(img, count=30, method="snic")
    b = img.superpixels(count=30, method=stb.SuperpixelMethod.SNIC)
    assert np.array_equal(a.labels, b.labels)


def test_the_result_does_not_depend_on_the_thread_count():
    img = noise(160, 120)
    with stb.threads(1):
        one = img.superpixels()
    with stb.threads(4):
        four = img.superpixels()
    assert np.array_equal(one.labels, four.labels)


def test_gray_images_work_and_the_alpha_channel_is_ignored():
    rgb = np.random.default_rng(1).integers(0, 256, (30, 40, 3), dtype=np.uint8)
    rgba = np.dstack([rgb, np.tile(np.arange(40, dtype=np.uint8) * 6, (30, 1))])
    assert np.array_equal(stb.Image(rgb).superpixels(count=30).labels, stb.Image(rgba).superpixels(count=30).labels)
    assert noise(30, 40, 1).superpixels(count=20).count >= 1


def test_boundaries_overlay_and_mean_color():
    img = quadrants(40)
    sp = img.superpixels(count=16)
    b = np.asarray(sp.boundaries((10, 20, 30, 255)))
    assert b.shape == (40, 40, 4) and set(np.unique(b[..., 3])) <= {0, 255}
    ov = np.asarray(sp.overlay(img, "#ffffff"))
    assert ov.shape == (40, 40, 3) and (ov == 255).all(axis=2).any()
    gray = stb.Image(np.arange(1600, dtype=np.uint8).reshape(40, 40, 1))
    assert np.asarray(sp.overlay(gray)).shape == (40, 40, 3)  # gray becomes RGB so that the colour shows
    assert np.asarray(sp.mean_color(gray)).shape == (40, 40, 1)
    with pytest.raises(ValueError):
        sp.mean_color(noise(41, 40))


def test_the_labels_view_keeps_the_map_alive():
    labels = noise(30, 30).superpixels(count=9).labels
    import gc
    gc.collect()
    assert labels.shape == (30, 30) and labels.max() >= 0


def test_label_lookup_and_repr():
    sp = noise(20, 30).superpixels(count=6)
    assert sp.label(0, 0) == sp.labels[0, 0] and sp.label(29, 19) == sp.labels[19, 29]
    assert "30x20" in repr(sp)
    with pytest.raises(IndexError):
        sp.label(30, 0)


@pytest.mark.parametrize("bad", [dict(count=0), dict(compactness=0.0), dict(iterations=0), dict(method="nope")])
def test_bad_options_raise_value_error(bad):
    with pytest.raises(ValueError):
        noise(20, 20).superpixels(**bad)
