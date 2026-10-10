"""NumPy's __array_interface__, both ways: what Pillow speaks (it has no DLPack and no buffer export)."""

import gc

import numpy as np
import pytest
from PIL import Image as PILImage

import libstb

MODES = {1: "L", 2: "LA", 3: "RGB", 4: "RGBA"}


def rand(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


class OnlyInterface:
    """An object that exposes nothing but __array_interface__ (so the buffer / DLPack routes fail)."""

    def __init__(self, array=None, **fields):
        self.array = array  # keeps the memory alive, like a real exporter
        base = array.__array_interface__ if array is not None else {"version": 3}
        self.__array_interface__ = {**base, **fields}


# ------------------------------------------------------------ libstb -> Pillow


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_pillow_reads_an_image_directly(c):
    a = rand(5, 7, c)
    pil = PILImage.fromarray(libstb.Image(a.copy()))
    assert pil.mode == MODES[c] and pil.size == (7, 5)
    np.testing.assert_array_equal(np.asarray(pil).reshape(5, 7, c), a)


def test_the_pillow_image_is_independent_of_the_libstb_one():
    img = libstb.Image(np.zeros((2, 2, 3), np.uint8))
    pil = PILImage.fromarray(img)
    img.numpy()[:] = 255
    assert pil.getpixel((0, 0)) == (0, 0, 0)


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_the_interface_describes_the_pixels(c):
    img = libstb.Image(rand(5, 7, c))
    ai = img.__array_interface__
    assert ai["version"] == 3 and ai["typestr"] == "|u1" and ai["strides"] is None
    assert ai["shape"] == ((5, 7) if c == 1 else (5, 7, c))  # a gray image is (H, W) here, as Pillow wants
    address, readonly = ai["data"]
    assert address == img.numpy().ctypes.data and readonly is False


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_numpy_still_gets_h_w_c_because_the_buffer_protocol_comes_first(c):
    img = libstb.Image(rand(5, 7, c))
    assert np.asarray(img).shape == (5, 7, c)
    assert img.numpy().shape == (5, 7, c)


# ------------------------------------------------------------ Pillow -> libstb


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_an_image_reads_a_pillow_image_directly(c):
    a = rand(5, 7, c, seed=c)
    pil = PILImage.fromarray(a[..., 0] if c == 1 else a)
    img = libstb.Image(pil)
    assert (img.width, img.height, img.channels) == (7, 5, c)
    np.testing.assert_array_equal(img.numpy(), a)
    np.testing.assert_array_equal(libstb.Image.open(img.to_png()).numpy(), a)


@pytest.mark.parametrize("mode", ["1", "I;16", "I", "F"])
def test_pillow_modes_that_are_not_uint8_are_rejected(mode):
    with pytest.raises(TypeError, match="uint8"):
        libstb.Image(PILImage.new(mode, (3, 2)))


def test_a_pillow_round_trip_through_both_directions():
    a = rand(6, 9, 4, 11)
    back = libstb.Image(PILImage.fromarray(libstb.Image(a.copy())))
    np.testing.assert_array_equal(back.numpy(), a)


# ------------------------------------------------- any exporter of the interface


def test_a_writable_contiguous_address_is_shared_and_kept_alive():
    a = rand(4, 5, 3)
    img = libstb.Image(OnlyInterface(a))
    assert np.shares_memory(img.numpy(), a)
    a[0, 0, 0] = 77
    assert img.numpy()[0, 0, 0] == 77
    expected = a.copy()
    del a
    gc.collect()  # the image holds the exporter, which holds the array
    np.testing.assert_array_equal(img.numpy(), expected)


def test_a_read_only_or_strided_array_is_copied_exactly():
    a = rand(4, 5, 3)
    ro = a.copy()
    ro.flags.writeable = False
    img = libstb.Image(OnlyInterface(ro))
    assert not np.shares_memory(img.numpy(), ro)
    np.testing.assert_array_equal(img.numpy(), ro)
    flipped = libstb.Image(OnlyInterface(a[::-1]))
    np.testing.assert_array_equal(flipped.numpy(), a[::-1])
    transposed = libstb.Image(OnlyInterface(np.ascontiguousarray(a.transpose(1, 0, 2)).transpose(1, 0, 2)))
    np.testing.assert_array_equal(transposed.numpy(), a)
    np.testing.assert_array_equal(libstb.Image(OnlyInterface(a[:, ::2])).numpy(), a[:, ::2])


def test_data_as_a_buffer_is_copied_and_checked():
    a = rand(3, 4, 3)
    fields = dict(shape=(3, 4, 3), typestr="|u1", strides=None, version=3)
    for data in (a.tobytes(), bytearray(a.tobytes()), memoryview(a.tobytes())):
        np.testing.assert_array_equal(libstb.Image(OnlyInterface(data=data, **fields)).numpy(), a)
    with pytest.raises(ValueError, match="shorter"):
        libstb.Image(OnlyInterface(data=a.tobytes()[:-1], **fields))
    # strides and offset into a buffer
    blob = bytes(range(256))
    img = libstb.Image(OnlyInterface(data=blob, shape=(2, 3), typestr="|u1", strides=(10, 2), offset=5, version=3))
    assert img.numpy()[..., 0].tolist() == [[5, 7, 9], [15, 17, 19]]
    flipped = libstb.Image(OnlyInterface(data=blob, shape=(2, 3), typestr="|u1", strides=(-10, 1), offset=10, version=3))
    assert flipped.numpy()[..., 0].tolist() == [[10, 11, 12], [0, 1, 2]]
    with pytest.raises(ValueError, match="shorter"):  # reaches before the start
        libstb.Image(OnlyInterface(data=blob, shape=(2, 3), typestr="|u1", strides=(-10, 1), offset=5, version=3))
    with pytest.raises(ValueError, match="shorter"):  # reaches past the end
        libstb.Image(OnlyInterface(data=blob, shape=(2, 3), typestr="|u1", strides=(300, 1), version=3))


@pytest.mark.parametrize("typestr", ["<u2", "|b1", "<f4", "|i1", "<i4", "|S1"])
def test_other_element_types_are_rejected(typestr):
    with pytest.raises(TypeError, match="uint8"):
        libstb.Image(OnlyInterface(data=bytes(24), shape=(2, 3, 4), typestr=typestr, version=3))


@pytest.mark.parametrize(
    "shape", [(4,), (2, 3, 4, 1), (2, 3, 0), (2, 3, 5), (0, 3), (3, 0), (2**31, 2)]
)
def test_bad_shapes_are_value_errors(shape):
    with pytest.raises(ValueError):
        libstb.Image(OnlyInterface(data=bytes(100), shape=shape, typestr="|u1", version=3))


@pytest.mark.parametrize(
    "fields",
    [dict(typestr="|u1"), dict(shape=(2, 2)), dict(shape=(2, 2), typestr="|u1", strides=(2,)),
     dict(shape=(2, 2), typestr="|u1", data=(0, False))],
)
def test_malformed_interfaces_are_value_errors(fields):
    with pytest.raises(ValueError):
        libstb.Image(OnlyInterface(version=3, **fields))


def test_objects_without_any_protocol_are_still_a_type_error():
    for bad in (object(), 3, "pixels", [[1, 2], [3, 4]], {"shape": (1, 1)}):
        with pytest.raises(TypeError):
            libstb.Image(bad)
