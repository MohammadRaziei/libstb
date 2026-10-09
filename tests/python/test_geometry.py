"""crop / flips / quarter turns / transpose / pad / thumbnail, checked against Pillow."""

import numpy as np
import pytest
from PIL import Image as PILImage
from PIL import ImageOps

import libstb

T = PILImage.Transpose
MODES = {1: "L", 2: "LA", 3: "RGB", 4: "RGBA"}


def rand(h, w, c, seed=0):
    rng = np.random.default_rng(seed)
    return rng.integers(0, 256, (h, w, c), dtype=np.uint8)


def to_pil(a):
    # The mode is inferred from the shape (passing mode= is deprecated since Pillow 11.3).
    return PILImage.fromarray(a[..., 0] if a.shape[2] == 1 else a)


def from_pil(p):
    a = np.asarray(p)
    return a[..., None] if a.ndim == 2 else a


CASES = [
    ("flip_horizontal", lambda i: i.flip_horizontal(), T.FLIP_LEFT_RIGHT),
    ("flip_vertical", lambda i: i.flip_vertical(), T.FLIP_TOP_BOTTOM),
    ("rotate90", lambda i: i.rotate90(), T.ROTATE_270),  # Pillow's ROTATE_270 is clockwise
    ("rotate90(2)", lambda i: i.rotate90(2), T.ROTATE_180),
    ("rotate90(3)", lambda i: i.rotate90(3), T.ROTATE_90),
    ("rotate90(-1)", lambda i: i.rotate90(-1), T.ROTATE_90),
    ("transpose", lambda i: i.transpose(), T.TRANSPOSE),
]


@pytest.mark.parametrize("channels", [1, 2, 3, 4])
@pytest.mark.parametrize("name, ours, theirs", CASES, ids=[c[0] for c in CASES])
@pytest.mark.parametrize("shape", [(5, 7), (1, 9), (9, 1), (33, 70)])  # incl. > one 32 px tile
def test_transforms_match_pillow_exactly(name, ours, theirs, channels, shape):
    a = rand(*shape, channels)
    got = ours(libstb.Image(a.copy())).numpy()
    np.testing.assert_array_equal(got, from_pil(to_pil(a).transpose(theirs)))


@pytest.mark.parametrize("orientation", range(1, 9))
@pytest.mark.parametrize("channels", [1, 3, 4])
def test_orient_matches_pillows_exif_transpose_table(orientation, channels):
    table = {1: None, 2: T.FLIP_LEFT_RIGHT, 3: T.ROTATE_180, 4: T.FLIP_TOP_BOTTOM, 5: T.TRANSPOSE,
             6: T.ROTATE_270, 7: T.TRANSVERSE, 8: T.ROTATE_90}
    a = rand(6, 9, channels, seed=orientation)
    want = to_pil(a) if table[orientation] is None else to_pil(a).transpose(table[orientation])
    np.testing.assert_array_equal(libstb.Image(a.copy()).orient(orientation).numpy(), from_pil(want))


@pytest.mark.parametrize("bad", [0, 9, -1, 100])
def test_orient_rejects_values_outside_1_to_8(bad):
    with pytest.raises(ValueError):
        libstb.Image(rand(2, 2, 3)).orient(bad)


def test_four_quarter_turns_are_the_identity_and_turns_wrap():
    a = rand(4, 6, 3)
    img = libstb.Image(a.copy())
    np.testing.assert_array_equal(img.rotate90(4).numpy(), a)
    np.testing.assert_array_equal(img.rotate90(0).numpy(), a)
    np.testing.assert_array_equal(img.rotate90(-4).numpy(), a)
    np.testing.assert_array_equal(img.rotate90(5).numpy(), img.rotate90(1).numpy())
    assert img.rotate90().shape == (6, 4, 3)


def test_transforms_return_new_images_and_leave_the_source_alone():
    a = rand(4, 5, 3)
    img = libstb.Image(a.copy())
    out = img.flip_horizontal()
    out.numpy()[:] = 0
    np.testing.assert_array_equal(img.numpy(), a)
    assert not np.shares_memory(img.numpy(), out.numpy())
    for same in (img.rotate90(0), img.orient(1), img.crop(0, 0, 5, 4), img.pad(0, 0, 0, 0), img.convert(3)):
        assert not np.shares_memory(img.numpy(), same.numpy())


@pytest.mark.parametrize("channels", [1, 3, 4])
def test_crop_matches_pillow(channels):
    a = rand(8, 11, channels)
    got = libstb.Image(a.copy()).crop(3, 2, 6, 5).numpy()
    np.testing.assert_array_equal(got, from_pil(to_pil(a).crop((3, 2, 9, 7))))


def test_crop_is_a_copy_not_a_view():
    a = rand(4, 4, 3)
    img = libstb.Image(a.copy())
    c = img.crop(1, 1, 2, 2)
    c.numpy()[:] = 0
    np.testing.assert_array_equal(img.numpy(), a)


@pytest.mark.parametrize(
    "box",
    [(-1, 0, 2, 2), (0, -1, 2, 2), (3, 0, 2, 2), (0, 3, 2, 2), (0, 0, 0, 1), (0, 0, 1, 0), (2**31 - 1, 0, 2**31 - 1, 1)],
)
def test_crop_outside_the_image_is_a_value_error(box):
    with pytest.raises(ValueError):
        libstb.Image(rand(4, 4, 3)).crop(*box)


@pytest.mark.parametrize("channels, fill", [(3, (9, 8, 7)), (4, (1, 2, 3, 4)), (1, 77), (3, 255), (2, (5, 6))])
def test_pad_matches_pillows_expand(channels, fill):
    a = rand(4, 5, channels)
    got = libstb.Image(a.copy()).pad(2, 1, 3, 4, fill=fill).numpy()
    # An int means every channel here; Pillow would put it in the first channel only.
    pil_fill = (fill,) * channels if isinstance(fill, int) and channels > 1 else fill
    want = ImageOps.expand(to_pil(a), (2, 1, 3, 4), fill=pil_fill)
    np.testing.assert_array_equal(got, from_pil(want))


def test_pad_defaults_arguments_and_default_fill_is_zero():
    img = libstb.Image(np.full((2, 2, 4), 255, np.uint8))
    p = img.pad(right=1)
    assert p.shape == (2, 3, 4)
    assert p.numpy()[0, 2].tolist() == [0, 0, 0, 0]  # transparent black
    assert img.pad().shape == (2, 2, 4)
    assert img.pad(1, 2, 3, 4).shape == (8, 6, 4)


@pytest.mark.parametrize("fill", [(1, 2), (1, 2, 3, 4), (), 256, -1, (1, 2, 300)])
def test_pad_rejects_a_bad_fill(fill):
    with pytest.raises(ValueError):
        libstb.Image(rand(2, 2, 3)).pad(1, 1, 1, 1, fill=fill)


def test_pad_rejects_negative_widths_and_huge_results():
    img = libstb.Image(rand(2, 2, 3))
    with pytest.raises(ValueError):
        img.pad(-1)
    with pytest.raises(libstb.LimitError):
        img.pad(2**31 - 1)
    with pytest.raises(libstb.LimitError):
        img.pad(30000, 30000, 30000, 30000)


@pytest.mark.parametrize(
    "size, box, expected",  # size (height, width); box (max_width, max_height); expected (height, width)
    [((50, 100), (20, 20), (10, 20)), ((100, 50), (20, 20), (20, 10)), ((100, 50), (40, 10), (10, 5)),
     ((100, 50), (1000, 25), (25, 13)), ((10, 5), (100, 100), (10, 5)), ((3, 1000), (10, 10), (1, 10))],
)
def test_thumbnail_sizes(size, box, expected):
    h, w = size
    out = libstb.Image(np.zeros((h, w, 3), np.uint8)).thumbnail(*box)
    assert (out.height, out.width) == expected


def test_thumbnail_never_enlarges_and_copies_what_already_fits():
    a = rand(5, 10, 3)
    img = libstb.Image(a.copy())
    out = img.thumbnail(100, 100)
    np.testing.assert_array_equal(out.numpy(), a)
    assert not np.shares_memory(out.numpy(), img.numpy())


def test_thumbnail_takes_the_same_resizer_arguments_as_resize():
    img = libstb.Image(rand(20, 40, 3))
    for r in ("linear", "nearest", libstb.Resizer.Filter.MITCHELL, libstb.Resizer("cubic", srgb=False), None):
        want = img.resize(20, 10, r) if r is not None else img.resize(20, 10)
        got = img.thumbnail(20, 20, r) if r is not None else img.thumbnail(20, 20)
        np.testing.assert_array_equal(got.numpy(), want.numpy())
    with pytest.raises(ValueError):
        img.thumbnail(20, 20, "lanczos")
    with pytest.raises(ValueError):
        img.thumbnail(0, 20)
    with pytest.raises(ValueError):
        img.thumbnail(20, -1)


def test_thumbnail_is_close_to_pillows_thumbnail_in_size():
    for size in [(37, 91), (200, 120), (64, 64)]:
        h, w = size
        ours = libstb.Image(np.zeros((h, w, 3), np.uint8)).thumbnail(30, 30)
        p = PILImage.new("RGB", (w, h))
        p.thumbnail((30, 30))
        assert abs(ours.width - p.width) <= 1 and abs(ours.height - p.height) <= 1
