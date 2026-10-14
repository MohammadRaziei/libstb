"""DLPack with real torch (CPU): torch.from_dlpack(img) and Image.from_dlpack(tensor).

torch is not a dependency of the test extra: it is huge, and has no wheel on several CI
platforms. Whoever has it installed gets this module run, everybody else gets it skipped.
(`test_dlpack.py` covers the same protocol with numpy and a stand-in provider.)
"""

import gc
import subprocess
import sys
import textwrap

import pytest

torch = pytest.importorskip("torch")  # no torch: this whole module is skipped
np = pytest.importorskip("numpy")  # the checks below compare against numpy arrays

import libstb as stb

SRC = np.arange(2 * 4 * 3, dtype=np.uint8).reshape(2, 4, 3)


def ptr(img):
    return img.numpy().ctypes.data


# ---- Image -> torch ----


def test_torch_from_dlpack_is_a_zero_copy_view():
    img = stb.Image(SRC.copy())
    t = torch.from_dlpack(img)
    assert isinstance(t, torch.Tensor)
    assert t.dtype == torch.uint8 and tuple(t.shape) == (2, 4, 3)
    assert t.device.type == "cpu"
    assert t.data_ptr() == ptr(img)
    np.testing.assert_array_equal(t.numpy(), SRC)


def test_torch_view_is_writable_and_shares_memory_both_ways():
    img = stb.Image(SRC.copy())
    t = torch.from_dlpack(img)
    t[0, 0, 0] = 99
    assert img.numpy()[0, 0, 0] == 99
    img.numpy()[1, 1, 1] = 77
    assert t[1, 1, 1].item() == 77


@pytest.mark.parametrize("shape", [(5, 7), (5, 7, 1), (5, 7, 2), (5, 7, 4)])
def test_torch_shape_is_height_width_channels(shape):
    img = stb.Image(np.zeros(shape, np.uint8))
    assert tuple(torch.from_dlpack(img).shape) == img.shape


def test_torch_view_keeps_the_image_alive():
    t = torch.from_dlpack(stb.Image(np.full((2, 2, 3), 7, np.uint8)))
    gc.collect()
    assert (t == 7).all().item()


def test_torch_view_of_a_decoded_image(tmp_path):
    path = tmp_path / "x.png"
    stb.Image(SRC.copy()).write_png(str(path))
    t = torch.from_dlpack(stb.imread(str(path)))
    np.testing.assert_array_equal(t.numpy(), SRC)


def test_torch_copy_true_is_independent():
    img = stb.Image(SRC.copy())

    class CopyingProvider:
        def __dlpack__(self, **kwargs):
            return img.__dlpack__(copy=True, **kwargs)

        def __dlpack_device__(self):
            return img.__dlpack_device__()

    t = torch.from_dlpack(CopyingProvider())
    assert t.data_ptr() != ptr(img)
    np.testing.assert_array_equal(t.numpy(), SRC)


def test_channels_first_float_recipe_from_the_readme():
    img = stb.Image(SRC.copy())
    x = torch.from_dlpack(img).permute(2, 0, 1).float() / 255
    assert tuple(x.shape) == (3, 2, 4)
    np.testing.assert_allclose(x.numpy(), SRC.transpose(2, 0, 1) / 255, rtol=1e-6)


# ---- torch -> Image ----


def test_image_from_a_tensor_shares_its_memory():
    t = torch.from_numpy(SRC.copy())
    img = stb.Image.from_dlpack(t)
    assert img.shape == (2, 4, 3)
    assert ptr(img) == t.data_ptr()
    t[0, 0, 0] = 42
    assert img.numpy()[0, 0, 0] == 42


def test_image_constructor_accepts_a_tensor_too():
    t = torch.from_numpy(SRC.copy())
    img = stb.Image(t)
    np.testing.assert_array_equal(img.numpy(), SRC)


def test_image_from_a_tensor_outlives_the_tensor_name():
    t = torch.from_numpy(SRC.copy())
    img = stb.Image.from_dlpack(t)
    del t
    gc.collect()
    np.testing.assert_array_equal(img.numpy(), SRC)


def test_grayscale_2d_tensor():
    assert stb.Image.from_dlpack(torch.zeros((3, 5), dtype=torch.uint8)).shape == (3, 5, 1)


def test_non_contiguous_tensor_gives_the_same_pixels():
    big = torch.from_numpy(np.arange(4 * 8 * 3, dtype=np.uint8).reshape(4, 8, 3))
    view = big[:, ::2]  # strided: not C-contiguous
    assert not view.is_contiguous()
    img = stb.Image.from_dlpack(view)
    np.testing.assert_array_equal(img.numpy(), view.numpy())


def test_chw_tensor_converted_on_the_torch_side():
    chw = torch.from_numpy(SRC.transpose(2, 0, 1).copy())  # (3, 2, 4)
    img = stb.Image.from_dlpack(chw.permute(1, 2, 0).contiguous())
    np.testing.assert_array_equal(img.numpy(), SRC)


def test_wrong_dtype_and_rank_are_errors():
    with pytest.raises(TypeError, match="uint8"):
        stb.Image.from_dlpack(torch.zeros((2, 4, 3), dtype=torch.float32))
    with pytest.raises(ValueError):
        stb.Image.from_dlpack(torch.zeros((2, 2, 2, 3), dtype=torch.uint8))


def test_round_trip_numpy_torch_image_numpy_keeps_one_buffer():
    a = SRC.copy()
    img = stb.Image.from_dlpack(torch.from_numpy(a))
    back = torch.from_dlpack(img)
    assert back.data_ptr() == a.ctypes.data
    assert np.shares_memory(img.numpy(), a)


def test_editing_a_tensor_backed_image_returns_new_images():
    t = torch.from_numpy(SRC.copy())
    img = stb.Image.from_dlpack(t)
    flipped = img.flip_horizontal()
    assert ptr(flipped) != t.data_ptr()
    np.testing.assert_array_equal(flipped.numpy(), SRC[:, ::-1])
    np.testing.assert_array_equal(t.numpy(), SRC)


# ---- GC: the old "nanobind: leaked 1 instances!" report was only reproduced with a Python
# wrapper around a DLPack object; this is the same situation with the real thing. ----


def test_no_leak_report_at_exit_for_an_image_viewing_a_tensor():
    code = textwrap.dedent("""
        import numpy as np, torch, libstb as stb

        class Holder:
            def __init__(self, t):
                self.t = t
                self.img = stb.Image.from_dlpack(t)

            def total(self):
                return int(self.img.numpy().sum())

        holder = Holder(torch.from_numpy(np.ones((4, 4, 3), np.uint8)))
        holder.cycle = holder  # a reference cycle through the image's tensor
        assert holder.total() == 48
    """)
    r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    assert "leaked" not in r.stderr, r.stderr
