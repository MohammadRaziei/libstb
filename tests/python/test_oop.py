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
    assert np.asarray(img) is img.array  # no copy by default
    assert np.array(img, copy=True) is not img.array
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
    assert img.numpy() is img.array                      # view by default
    assert img.numpy(copy=True) is not img.array
    np.testing.assert_array_equal(img.numpy(copy=True), EXPECTED)
    f = img.numpy(dtype=np.float32)
    assert f.dtype == np.float32 and f.shape == (2, 2, 3)
