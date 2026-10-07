"""DLPack: np.from_dlpack(img) (and torch / jax / cupy the same way), zero-copy."""

import gc
import subprocess
import sys
import textwrap

import numpy as np
import pytest

import libstb

SRC = np.arange(2 * 4 * 3, dtype=np.uint8).reshape(2, 4, 3)


def test_device_is_cpu():
    assert libstb.Image(SRC).__dlpack_device__() == (1, 0)


def test_from_dlpack_is_a_zero_copy_writable_view():
    img = libstb.Image(SRC.copy())
    a = np.from_dlpack(img)
    assert a.shape == (2, 4, 3) and a.dtype == np.uint8
    assert a.flags.writeable
    assert np.shares_memory(a, img.numpy())
    a[0, 0, 0] = 99
    assert img.numpy()[0, 0, 0] == 99


@pytest.mark.parametrize("shape", [(5, 7), (5, 7, 1), (5, 7, 2), (5, 7, 4)])
def test_shape_is_height_width_channels(shape):
    img = libstb.Image(np.zeros(shape, np.uint8))
    assert np.from_dlpack(img).shape == img.shape


def test_copy_true_is_independent():
    img = libstb.Image(SRC.copy())
    b = np.from_dlpack(img, copy=True)
    assert not np.shares_memory(b, img.numpy())
    np.testing.assert_array_equal(b, SRC)
    assert b.flags.writeable


def test_the_view_keeps_the_image_alive():
    x = np.from_dlpack(libstb.Image(np.full((2, 2, 3), 7, np.uint8)))
    gc.collect()
    assert (x == 7).all()


def test_both_capsule_versions():
    img = libstb.Image(SRC)
    assert "dltensor" in repr(img.__dlpack__())
    assert "dltensor_versioned" in repr(img.__dlpack__(max_version=(1, 0)))


def test_only_the_cpu_is_supported():
    with pytest.raises(BufferError):
        libstb.Image(SRC).__dlpack__(dl_device=(2, 0))


def test_dlpack_needs_no_numpy():
    r = subprocess.run(
        [sys.executable, "-c", textwrap.dedent("""
            import sys
            sys.modules["numpy"] = None
            import libstb
            img = libstb.Image(memoryview(bytearray(24)).cast("B", shape=[2, 4, 3]))
            assert img.__dlpack_device__() == (1, 0)
            assert "dltensor" in repr(img.__dlpack__())
        """)],
        capture_output=True, text=True,
    )
    assert r.returncode == 0, r.stderr


# ---- the other direction: Image.from_dlpack(x) ----


class Provider:
    """A DLPack provider that is not numpy (stands in for torch / jax / cupy)."""

    def __init__(self, arr):
        self.arr = arr

    def __dlpack__(self, **kwargs):
        return self.arr.__dlpack__(**kwargs)

    def __dlpack_device__(self):
        return self.arr.__dlpack_device__()


def test_from_dlpack_shares_a_writable_array():
    a = SRC.copy()
    img = libstb.Image.from_dlpack(a)
    assert img.shape == (2, 4, 3)
    assert np.shares_memory(img.numpy(), a)
    a[0, 0, 0] = 42
    assert img.numpy()[0, 0, 0] == 42


def test_from_dlpack_takes_any_provider_zero_copy():
    a = SRC.copy()
    img = libstb.Image.from_dlpack(Provider(a))
    assert np.shares_memory(img.numpy(), a)
    np.testing.assert_array_equal(img.numpy(), SRC)


def test_from_dlpack_round_trips_an_image():
    img = libstb.Image(SRC.copy())
    again = libstb.Image.from_dlpack(img)
    assert np.shares_memory(again.numpy(), img.numpy())


def test_from_dlpack_copy_true_is_independent():
    a = SRC.copy()
    img = libstb.Image.from_dlpack(a, copy=True)
    assert not np.shares_memory(img.numpy(), a)
    np.testing.assert_array_equal(img.numpy(), SRC)


def test_from_dlpack_copies_what_it_cannot_share():
    a = SRC.copy()
    a.flags.writeable = False
    img = libstb.Image.from_dlpack(Provider(a))
    assert not np.shares_memory(img.numpy(), a)
    np.testing.assert_array_equal(img.numpy(), SRC)


def test_from_dlpack_accepts_grayscale_2d():
    assert libstb.Image.from_dlpack(np.zeros((3, 5), np.uint8)).shape == (3, 5, 1)


def test_from_dlpack_errors():
    with pytest.raises(TypeError, match="__dlpack__"):
        libstb.Image.from_dlpack(b"not a tensor")
    with pytest.raises(TypeError, match="uint8"):
        libstb.Image.from_dlpack(SRC.astype(np.float32))
    with pytest.raises(ValueError):
        libstb.Image.from_dlpack(np.zeros((2, 2, 2, 3), np.uint8))
    with pytest.raises(TypeError):
        libstb.Image.from_dlpack(SRC, True)  # copy is keyword-only
