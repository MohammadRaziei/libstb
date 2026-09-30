import numpy as np
import pytest

import libstb
from libstb import Image, ImageInfo

from test_image import EXPECTED, RGB


def test_open_bytes_and_properties():
    img = Image.open(RGB)
    assert (img.width, img.height, img.channels) == (2, 2, 3)
    assert img.shape == (2, 2, 3)
    assert img.array.dtype == np.uint8
    np.testing.assert_array_equal(img.array, EXPECTED)
    assert repr(img) == "Image(width=2, height=2, channels=3)"


def test_open_path(tmp_path):
    p = tmp_path / "x.png"
    p.write_bytes(RGB)
    np.testing.assert_array_equal(Image.open(p).array, EXPECTED)
    np.testing.assert_array_equal(Image.open(str(p)).array, EXPECTED)


def test_open_options():
    assert Image.open(RGB, channels=4).channels == 4
    np.testing.assert_array_equal(Image.open(RGB, flip=True).array, EXPECTED[::-1])
    with pytest.raises(libstb.LimitError, match="too large"):
        Image.open(RGB, max_bytes=1)
    with pytest.raises(ValueError):
        Image.open(RGB, channels=9)


def test_numpy_interop():
    img = Image.open(RGB)
    np.testing.assert_array_equal(np.asarray(img), EXPECTED)
    assert np.shares_memory(np.asarray(img), img.array)  # no copy by default
    assert not np.shares_memory(np.array(img, copy=True), img.array)
    assert np.asarray(img, dtype=np.float32).dtype == np.float32


def test_constructor_from_array():
    assert Image(np.zeros((3, 4), np.uint8)).shape == (3, 4, 1)  # 2D -> 1 channel
    assert Image(np.zeros((3, 4, 4), np.uint8)).channels == 4
    with pytest.raises(TypeError):
        Image(np.zeros((3, 4, 3), np.float32))
    with pytest.raises(ValueError):
        Image(np.zeros((3, 4, 5), np.uint8))
    with pytest.raises(ValueError):
        Image(np.zeros(5, np.uint8))


def test_image_info_class():
    i = ImageInfo.read(RGB)
    assert isinstance(i, ImageInfo)
    assert i == (2, 2, 3) == libstb.info(RGB)


def test_functional_shortcuts_match_object_api():
    np.testing.assert_array_equal(libstb.load(RGB, flip=True), Image.open(RGB, flip=True).array)


def test_numpy_method():
    img = Image.open(RGB)
    assert np.shares_memory(img.numpy(), img.array)      # view by default
    assert not np.shares_memory(img.numpy(copy=True), img.array)
    np.testing.assert_array_equal(img.numpy(copy=True), EXPECTED)
    f = img.numpy(dtype=np.float32)
    assert f.dtype == np.float32 and f.shape == (2, 2, 3)


def test_array_is_a_writable_view_that_keeps_the_image_alive():
    import gc

    a = Image.open(RGB).array  # the Image object is gone; the view must still be valid
    gc.collect()
    np.testing.assert_array_equal(a, EXPECTED)
    img = Image.open(RGB)
    img.array[0, 0, 0] = 7  # writes through to the image
    assert img.array[0, 0, 0] == 7


def test_constructor_views_a_writable_contiguous_array_without_copying():
    src = EXPECTED.copy()
    img = Image(src)
    assert np.shares_memory(img.array, src)
    src[0, 0, 0] = 99  # the image sees changes to the array...
    assert img.array[0, 0, 0] == 99
    img.array[0, 0, 1] = 5  # ...and the array sees changes to the image
    assert src[0, 0, 1] == 5


def test_the_view_keeps_the_array_alive():
    import gc

    def make():
        a = np.full((4, 5, 3), 9, np.uint8)
        return Image(a)  # `a` is unreachable except through the image

    img = make()
    gc.collect()
    assert (img.array == 9).all()
    view = img.array
    del img
    gc.collect()
    assert (view == 9).all()  # and the view keeps the image alive


def test_constructor_copies_arrays_it_cannot_view():
    flipped = Image(EXPECTED[::-1])  # negative stride
    assert not np.shares_memory(flipped.array, EXPECTED)
    np.testing.assert_array_equal(flipped.array, EXPECTED[::-1])
    transposed = Image(np.ascontiguousarray(EXPECTED).transpose(1, 0, 2))
    np.testing.assert_array_equal(transposed.array, EXPECTED.transpose(1, 0, 2))
    assert Image(np.zeros((3, 4), np.uint8)[:, ::2]).shape == (3, 2, 1)

    ro = EXPECTED.copy()
    ro.flags.writeable = False  # read-only memory must never be written through
    img = Image(ro)
    assert not np.shares_memory(img.array, ro)
    img.array[0, 0, 0] = 1  # allowed: it is the image's own copy
    assert ro[0, 0, 0] == 255


def test_copy_is_explicit_and_independent():
    import copy as copy_module

    src = EXPECTED.copy()
    img = Image(src)
    for dup in (img.copy(), copy_module.copy(img), copy_module.deepcopy(img)):
        assert isinstance(dup, Image)
        assert not np.shares_memory(dup.array, img.array)
        np.testing.assert_array_equal(dup.array, img.array)
        dup.array[0, 0, 0] = 1
        assert src[0, 0, 0] == 255  # neither the original image nor the array changed


def test_decoded_images_own_their_pixels_and_copy_detaches():
    img = Image.open(RGB)
    dup = img.copy()
    dup.array[0, 0, 0] = 0
    np.testing.assert_array_equal(img.array, EXPECTED)


def test_writes_from_a_viewing_image_reach_the_array(tmp_path):
    src = np.zeros((4, 4, 3), np.uint8)
    img = Image(src)
    src[:] = 200
    p = tmp_path / "x.png"
    img.write(p)  # encodes what the array holds now
    np.testing.assert_array_equal(Image.open(p).array, src)
    assert img.resize(2, 2).array.shape == (2, 2, 3)


def test_open_sources_bytes_bytearray_memoryview_ndarray_and_paths(tmp_path):
    p = tmp_path / "x.png"
    p.write_bytes(RGB)
    for src in (RGB, bytearray(RGB), memoryview(RGB), np.frombuffer(RGB, np.uint8), p, str(p)):
        np.testing.assert_array_equal(Image.open(src).array, EXPECTED)


def test_open_missing_file_is_file_not_found_and_not_a_libstb_error(tmp_path):
    with pytest.raises(FileNotFoundError) as e:
        Image.open(tmp_path / "nope.png")
    assert not isinstance(e.value, libstb.Error)
    assert e.value.errno is not None and "nope.png" in str(e.value)


def test_open_rejects_things_that_are_neither_bytes_nor_paths():
    with pytest.raises(TypeError):
        Image.open(12345)
    with pytest.raises(TypeError):
        Image.open(np.zeros((2, 2), np.uint8))
