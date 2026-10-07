import threading

import numpy as np
import pytest

import libstb
from libstb import Image, Resizer


def gradient(w, h, c):
    y, x, k = np.mgrid[0:h, 0:w, 0:c]
    return ((x * 31 + y * 17 + k * 53) & 0xFF).astype(np.uint8)


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_size_and_channels_preserved(c):
    out = Image(gradient(10, 8, c)).resize(5, 20)
    assert (out.width, out.height, out.channels) == (5, 20, c)
    assert out.numpy().dtype == np.uint8


def test_resize_returns_a_new_image_and_leaves_the_source_alone():
    src = Image(gradient(6, 6, 3))
    before = src.numpy().copy()
    out = src.resize(3, 3)
    assert out is not src
    np.testing.assert_array_equal(src.numpy(), before)


def test_point_upscale_duplicates_pixels():
    src = gradient(2, 2, 3)
    out = Image(src).resize(4, 4, Resizer(Resizer.Filter.POINT)).numpy()
    for y in range(4):
        for x in range(4):
            np.testing.assert_array_equal(out[y, x], src[y // 2, x // 2])


def test_srgb_flag_changes_how_colours_blend():
    src = Image(np.array([[[0], [255]]], np.uint8))
    box = Resizer.Filter.BOX
    linear = src.resize(1, 1, Resizer(box, srgb=False)).numpy()[0, 0, 0]
    srgb = src.resize(1, 1, Resizer(box, srgb=True)).numpy()[0, 0, 0]
    assert 127 <= linear <= 128
    assert 186 <= srgb <= 190


def test_alpha_is_weighted():
    src = Image(np.array([[[255, 0, 0, 255], [0, 255, 0, 0]]], np.uint8))
    px = src.resize(1, 1, Resizer(Resizer.Filter.BOX)).numpy()[0, 0]
    assert px[0] >= 250 and px[1] <= 5 and 126 <= px[3] <= 129


def test_resizer_properties_and_defaults():
    r = Resizer()
    assert r.filter == Resizer.Filter.DEFAULT
    assert r.edge == Resizer.Edge.CLAMP
    assert r.srgb is True
    r2 = Resizer(Resizer.Filter.MITCHELL, Resizer.Edge.WRAP, srgb=False, max_bytes=1000)
    assert (r2.filter, r2.edge, r2.srgb, r2.max_bytes) == (
        Resizer.Filter.MITCHELL, Resizer.Edge.WRAP, False, 1000)


def test_every_filter_and_edge_works():
    src = Image(np.full((16, 16, 3), 100, np.uint8))
    for f in Resizer.Filter.__members__.values():
        for e in Resizer.Edge.__members__.values():
            out = src.resize(8, 8, Resizer(f, e))
            assert out.numpy()[4, 4, 0] == 100


def test_errors():
    src = Image(gradient(2, 2, 3))
    with pytest.raises(ValueError):
        src.resize(0, 4)
    with pytest.raises(ValueError):
        src.resize(4, -1)
    with pytest.raises(libstb.LimitError):
        src.resize(10, 10, Resizer(max_bytes=100))  # 300 bytes needed


def test_concurrent_resizes():
    src = Image(gradient(64, 48, 4))
    ref = src.resize(20, 15).numpy()
    bad = []

    def work():
        for _ in range(50):
            if not np.array_equal(src.resize(20, 15).numpy(), ref):
                bad.append(1)

    ts = [threading.Thread(target=work) for _ in range(8)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert not bad


# --- filters by name -------------------------------------------------------

@pytest.mark.parametrize("name, filt", [
    ("auto", Resizer.Filter.DEFAULT),
    ("default", Resizer.Filter.DEFAULT),
    ("nearest", Resizer.Filter.POINT),
    ("linear", Resizer.Filter.TRIANGLE),
    ("bilinear", Resizer.Filter.TRIANGLE),
    ("cubic", Resizer.Filter.CATMULL_ROM),
    ("bicubic", Resizer.Filter.CATMULL_ROM),
    ("bspline", Resizer.Filter.CUBIC_BSPLINE),
    ("mitchell", Resizer.Filter.MITCHELL),
    ("box", Resizer.Filter.BOX),
    ("area", Resizer.Filter.BOX),
    ("Catmull-Rom", Resizer.Filter.CATMULL_ROM),  # case and separators are forgiving
])
def test_resizer_by_name(name, filt):
    r = Resizer(name)
    assert r.filter == filt
    assert r.edge == Resizer.Edge.CLAMP and r.srgb is True  # other options keep their defaults


def test_resizer_by_name_still_takes_the_other_options():
    r = Resizer("cubic", Resizer.Edge.WRAP, srgb=False, max_bytes=1000)
    assert (r.filter, r.edge, r.srgb, r.max_bytes) == (
        Resizer.Filter.CATMULL_ROM, Resizer.Edge.WRAP, False, 1000)
    r = Resizer("mitchell", edge=Resizer.Edge.ZERO)
    assert r.edge == Resizer.Edge.ZERO


def test_unknown_filter_name_raises_value_error_listing_valid_names():
    with pytest.raises(ValueError, match="lanczos") as e:
        Resizer("lanczos")
    assert "cubic" in str(e.value)
    with pytest.raises(ValueError):
        Image(gradient(4, 4, 3)).resize(2, 2, "nope")


def test_image_resize_accepts_name_filter_resizer_or_none():
    src = Image(gradient(16, 12, 3))
    ref = src.resize(8, 6, Resizer("cubic")).numpy()
    np.testing.assert_array_equal(src.resize(8, 6, "cubic").numpy(), ref)
    np.testing.assert_array_equal(src.resize(8, 6, Resizer.Filter.CATMULL_ROM).numpy(), ref)
    np.testing.assert_array_equal(src.resize(8, 6, None).numpy(), src.resize(8, 6).numpy())
    np.testing.assert_array_equal(src.resize(8, 6).numpy(), src.resize(8, 6, Resizer()).numpy())


def test_nearest_by_name_duplicates_pixels():
    src = gradient(2, 2, 3)
    out = Image(src).resize(4, 4, "nearest").numpy()
    for y in range(4):
        for x in range(4):
            np.testing.assert_array_equal(out[y, x], src[y // 2, x // 2])


def test_resizer_resize_takes_an_image_and_matches_image_resize():
    src = Image(gradient(16, 12, 3))
    r = Resizer("mitchell")
    out = r.resize(src, 8, 6)
    assert isinstance(out, Image) and (out.width, out.height) == (8, 6)
    np.testing.assert_array_equal(out.numpy(), src.resize(8, 6, r).numpy())
    np.testing.assert_array_equal(out.numpy(), src.resize(8, 6, resizer="mitchell").numpy())


def test_resize_rejects_a_bad_filter_argument_type():
    with pytest.raises(TypeError):
        Image(gradient(4, 4, 3)).resize(2, 2, 3.5)
