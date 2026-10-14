"""An Image that views a Python object holds a reference Python cannot see by itself; the
garbage collector must be able to follow it (tp_traverse) and break it (tp_clear), or an
object that reaches the image it is viewed by is never freed (and nanobind reports a leak at
interpreter exit)."""

import gc
import os
import subprocess
import sys
import textwrap
import weakref

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb


class Wrapper:
    """A DLPack producer that is not numpy (like a tensor of another framework)."""

    def __init__(self, a):
        self.a = a

    def __dlpack__(self, *args, **kw):
        return self.a.__dlpack__(*args, **kw)

    def __dlpack_device__(self):
        return self.a.__dlpack_device__()


class Box(np.ndarray):
    """An ndarray subclass: instances can carry attributes."""


def pixels():
    return np.arange(5 * 7 * 3, dtype=np.uint8).reshape(5, 7, 3).copy()


def test_a_cycle_through_an_image_viewing_a_dlpack_object_is_collected():
    obj = Wrapper(pixels())
    obj.image = stb.Image.from_dlpack(obj)  # obj -> image -> (hidden) obj
    ref = weakref.ref(obj)
    del obj
    gc.collect()
    assert ref() is None


def test_a_cycle_through_an_image_viewing_a_numpy_subclass_is_collected():
    arr = pixels().view(Box)
    arr.image = stb.Image(arr)
    ref = weakref.ref(arr)
    del arr
    gc.collect()
    assert ref() is None


def test_collecting_does_not_touch_images_that_are_still_in_use():
    keep = []
    for i in range(20):
        obj = Wrapper(pixels() + np.uint8(i))
        keep.append((i, stb.Image.from_dlpack(obj), obj))
    cyc = Wrapper(pixels())
    cyc.image = stb.Image.from_dlpack(cyc)
    del cyc
    for _ in range(3):
        gc.collect()
    for i, img, obj in keep:
        np.testing.assert_array_equal(img.numpy(), pixels() + np.uint8(i))
    # an image that is the only holder of the object it views keeps it alive through a collection
    img = stb.Image.from_dlpack(Wrapper(pixels()))
    gc.collect()
    np.testing.assert_array_equal(img.numpy(), pixels())


def test_decoded_and_copied_images_are_unaffected_by_collection():
    png = stb.Image(pixels()).to_png()
    img = stb.Image.open(png)
    other = img.copy()
    gc.collect()
    np.testing.assert_array_equal(img.numpy(), pixels())
    np.testing.assert_array_equal(other.numpy(), pixels())


def _run(code, **env):
    full = dict(os.environ, **env)
    return subprocess.run([sys.executable, "-c", textwrap.dedent(code)], env=full, capture_output=True, text=True, timeout=120)


@pytest.mark.parametrize("keep_global", ["img", "obj_and_img"])
def test_no_leak_report_at_exit_when_the_image_outlives_the_script(keep_global):
    # The module defines the wrapper class itself, so its methods reach the module globals
    # that hold the image: the exact shape that used to print "nanobind: leaked 1 instances!".
    r = _run(
        f"""
        import numpy as np, libstb as stb
        class W:
            def __init__(self, a): self.a = a
            def __dlpack__(self, *args, **kw): return self.a.__dlpack__(*args, **kw)
            def __dlpack_device__(self): return self.a.__dlpack_device__()
        a = np.zeros((4, 4, 3), np.uint8)
        {'obj = W(a); img = stb.Image.from_dlpack(obj)' if keep_global == 'obj_and_img' else 'img = stb.Image.from_dlpack(W(a))'}
        """
    )
    assert r.returncode == 0
    assert r.stderr == ""
