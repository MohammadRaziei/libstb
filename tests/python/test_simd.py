"""The AVX2 kernels (picked at run time when the CPU has them) must equal the portable ones."""

import numpy as np
import pytest

import libstb


@pytest.fixture(autouse=True)
def restore_simd():
    libstb.set_simd(True)
    yield
    libstb.set_simd(True)  # whatever the CPU allows


def rand(h, w, c, seed):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def under(enabled, fn):
    libstb.set_simd(enabled)
    try:
        return fn()
    finally:
        libstb.set_simd(True)


def test_simd_name_and_switch():
    assert libstb.simd_name() in ("avx2", "baseline")
    default = libstb.simd_name()
    libstb.set_simd(False)
    assert libstb.simd_name() == "baseline"
    libstb.set_simd(True)
    assert libstb.simd_name() == default


@pytest.mark.parametrize("src, dst", [(s, d) for s in range(1, 5) for d in range(1, 5) if s != d])
def test_convert_is_identical_in_both_modes(src, dst):
    img = libstb.Image(rand(7, 1003, src, src * 10 + dst))  # 1003 is not a multiple of any vector width
    fast = under(True, lambda: img.convert(dst).numpy())
    slow = under(False, lambda: img.convert(dst).numpy())
    np.testing.assert_array_equal(fast, slow)


@pytest.mark.parametrize("dc", [1, 2, 3, 4])
@pytest.mark.parametrize("sc", [1, 2, 3, 4])
@pytest.mark.parametrize("offset", [(0, 0), (5, 3), (-20, -2), (250, 8)])
def test_composite_is_identical_in_both_modes(dc, sc, offset):
    base = libstb.Image(rand(11, 317, dc, dc * 7 + sc))
    over = libstb.Image(rand(9, 211, sc, dc * 13 + sc + 100))
    fast = under(True, lambda: base.composite(over, *offset).numpy())
    slow = under(False, lambda: base.composite(over, *offset).numpy())
    np.testing.assert_array_equal(fast, slow)


@pytest.mark.parametrize("c", [2, 4])
def test_flatten_is_identical_in_both_modes(c):
    img = libstb.Image(rand(13, 999, c, 5))
    fast = under(True, lambda: img.flatten((20, 40, 160)).numpy())
    slow = under(False, lambda: img.flatten((20, 40, 160)).numpy())
    np.testing.assert_array_equal(fast, slow)


def test_the_alpha_regression_grid_is_right_in_both_modes():
    da, sa = np.meshgrid(np.arange(256), np.arange(1, 256))
    for c in (255, 100, 1):
        base = libstb.Image(np.stack([np.full(da.shape, c)] * 3 + [da], -1).astype(np.uint8))
        over = libstb.Image(np.stack([np.full(da.shape, c)] * 3 + [sa], -1).astype(np.uint8))
        for mode in (True, False):
            out = under(mode, lambda: base.composite(over).numpy())
            np.testing.assert_array_equal(out[..., :3], c)
