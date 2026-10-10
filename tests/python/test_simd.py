"""Every SIMD backend (avx2 where the CPU has it, sse2 / neon, scalar) gives identical results."""

import numpy as np
import pytest

import libstb

BACKENDS = libstb.simd_backends()


@pytest.fixture(autouse=True)
def restore_simd():
    libstb.set_simd("auto")
    yield
    libstb.set_simd("auto")


def rand(h, w, c, seed):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def under(backend, fn):
    libstb.set_simd(backend)
    try:
        return fn()
    finally:
        libstb.set_simd("auto")


def test_backends_are_listed_best_first_and_end_with_scalar():
    assert BACKENDS[-1] == "scalar"
    assert len(set(BACKENDS)) == len(BACKENDS)
    assert set(BACKENDS) <= {"avx2", "avx512", "avx", "sse2", "neon", "scalar"}
    assert isinstance(BACKENDS, list) and all(isinstance(b, str) for b in BACKENDS)


def test_simd_name_follows_set_simd():
    assert libstb.simd_name() == BACKENDS[0]  # the default is the best one
    for name in BACKENDS:
        libstb.set_simd(name)
        assert libstb.simd_name() == name
    libstb.set_simd("auto")
    assert libstb.simd_name() == BACKENDS[0]


@pytest.mark.parametrize("bad", ["", "AVX2", "avx3", "sse9", "off", "fast"])
def test_set_simd_rejects_unknown_names_and_changes_nothing(bad):
    if bad in BACKENDS:
        pytest.skip("a real backend on this CPU")
    libstb.set_simd("scalar")
    with pytest.raises(ValueError, match="available"):
        libstb.set_simd(bad)
    assert libstb.simd_name() == "scalar"


def test_set_simd_wants_a_name_not_a_bool():
    with pytest.raises(TypeError):
        libstb.set_simd(True)


@pytest.mark.parametrize("backend", BACKENDS)
@pytest.mark.parametrize("src, dst", [(s, d) for s in range(1, 5) for d in range(1, 5) if s != d])
def test_convert_equals_the_scalar_reference(backend, src, dst):
    img = libstb.Image(rand(7, 1003, src, src * 10 + dst))  # 1003 is not a multiple of any vector width
    want = under("scalar", lambda: img.convert(dst).numpy())
    np.testing.assert_array_equal(under(backend, lambda: img.convert(dst).numpy()), want)


@pytest.mark.parametrize("backend", BACKENDS)
@pytest.mark.parametrize("dc", [1, 2, 3, 4])
@pytest.mark.parametrize("sc", [1, 2, 3, 4])
@pytest.mark.parametrize("offset", [(0, 0), (5, 3), (-20, -2), (250, 8)])
def test_composite_equals_the_scalar_reference(backend, dc, sc, offset):
    base = libstb.Image(rand(11, 317, dc, dc * 7 + sc))
    over = libstb.Image(rand(9, 211, sc, dc * 13 + sc + 100))
    want = under("scalar", lambda: base.composite(over, *offset).numpy())
    np.testing.assert_array_equal(under(backend, lambda: base.composite(over, *offset).numpy()), want)


@pytest.mark.parametrize("backend", BACKENDS)
@pytest.mark.parametrize("c", [2, 4])
def test_flatten_equals_the_scalar_reference(backend, c):
    img = libstb.Image(rand(13, 999, c, 5))
    want = under("scalar", lambda: img.flatten((20, 40, 160)).numpy())
    np.testing.assert_array_equal(under(backend, lambda: img.flatten((20, 40, 160)).numpy()), want)


@pytest.mark.parametrize("backend", BACKENDS)
def test_the_alpha_regression_grid_is_right_in_every_backend(backend):
    da, sa = np.meshgrid(np.arange(256), np.arange(1, 256))
    for c in (255, 100, 1):
        base = libstb.Image(np.stack([np.full(da.shape, c)] * 3 + [da], -1).astype(np.uint8))
        over = libstb.Image(np.stack([np.full(da.shape, c)] * 3 + [sa], -1).astype(np.uint8))
        out = under(backend, lambda: base.composite(over).numpy())
        np.testing.assert_array_equal(out[..., :3], c)


# ---------------------------------------------------------------- per-thread override


def test_simd_context_sets_the_backend_for_the_block_only():
    default = libstb.simd_name()
    with libstb.simd("scalar"):
        assert libstb.simd_name() == "scalar"
    assert libstb.simd_name() == default


@pytest.mark.parametrize("backend", BACKENDS)
def test_simd_context_gives_the_same_pixels_as_set_simd(backend):
    img = libstb.Image(rand(7, 1003, 4, 3))
    want = under("scalar", lambda: img.convert(1).numpy())
    with libstb.simd(backend):
        np.testing.assert_array_equal(img.convert(1).numpy(), want)


def test_simd_contexts_nest_and_restore_even_after_an_exception():
    default = libstb.simd_name()
    with libstb.simd("scalar"):
        with libstb.simd("auto"):
            assert libstb.simd_name() == BACKENDS[0]
        assert libstb.simd_name() == "scalar"
        with pytest.raises(RuntimeError, match="boom"):
            with libstb.simd(BACKENDS[0]):
                raise RuntimeError("boom")
        assert libstb.simd_name() == "scalar"
    assert libstb.simd_name() == default


def test_simd_context_wins_over_set_simd_and_set_simd_still_applies_outside():
    libstb.set_simd("scalar")
    with libstb.simd("auto"):
        assert libstb.simd_name() == BACKENDS[0]
        libstb.set_simd("scalar")  # changes the global, not what this block uses
        assert libstb.simd_name() == BACKENDS[0]
    assert libstb.simd_name() == "scalar"


def test_simd_context_does_not_leak_into_other_threads():
    import threading

    seen = {}

    def other():
        seen["name"] = libstb.simd_name()
        with libstb.simd("scalar"):
            seen["inside"] = libstb.simd_name()

    with libstb.simd("scalar"):
        t = threading.Thread(target=other)
        t.start()
        t.join()
        assert libstb.simd_name() == "scalar"
    assert seen["name"] == BACKENDS[0]  # the other thread follows the global, not our block
    assert seen["inside"] == "scalar"
    assert libstb.simd_name() == BACKENDS[0]  # and its own block did not touch ours


@pytest.mark.parametrize("bad", ["", "AVX2", "avx3", "sse9", "fast"])
def test_simd_context_rejects_unknown_names_when_created(bad):
    if bad in BACKENDS:
        pytest.skip("a real backend on this CPU")
    with pytest.raises(ValueError, match="available"):
        libstb.simd(bad)


def test_simd_context_cannot_be_entered_twice_at_once():
    ctx = libstb.simd("scalar")
    with ctx:
        with pytest.raises(RuntimeError, match="already active"):
            ctx.__enter__()
    with ctx:  # but it can be reused afterwards
        assert libstb.simd_name() == "scalar"


def _run_with_env(value):
    import os
    import subprocess
    import sys

    env = dict(os.environ, LIBSTB_SIMD=value)
    code = "import libstb; print(libstb.simd_name(), libstb.simd_backends()[0])"
    return subprocess.run([sys.executable, "-c", code], env=env, capture_output=True, text=True, timeout=120)


def test_an_unknown_env_backend_warns_once_on_stderr_and_falls_back_to_auto():
    r = _run_with_env("avx9000")
    assert r.returncode == 0
    name, best = r.stdout.split()
    assert name == best
    assert r.stderr.count("\n") == 1 and "LIBSTB_SIMD=avx9000" in r.stderr and "available" in r.stderr


@pytest.mark.parametrize("value", ["scalar", "off", "auto"])
def test_a_valid_env_backend_prints_nothing(value):
    r = _run_with_env(value)
    assert r.returncode == 0 and r.stderr == ""
    assert r.stdout.split()[0] == ("scalar" if value != "auto" else BACKENDS[0])
