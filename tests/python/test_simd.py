"""Every SIMD backend (avx2 where the CPU has it, sse2 / neon, scalar) gives identical results."""

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb

BACKENDS = stb.simd_backends()


@pytest.fixture(autouse=True)
def restore_simd():
    stb.set_simd("auto")
    yield
    stb.set_simd("auto")


def rand(h, w, c, seed):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def under(backend, fn):
    stb.set_simd(backend)
    try:
        return fn()
    finally:
        stb.set_simd("auto")


def test_backends_are_listed_best_first_and_end_with_scalar():
    assert BACKENDS[-1] == "scalar"
    assert len(set(BACKENDS)) == len(BACKENDS)
    assert set(BACKENDS) <= {"avx2", "avx512", "avx", "sse2", "neon", "scalar"}
    assert isinstance(BACKENDS, list) and all(isinstance(b, str) for b in BACKENDS)


def test_simd_name_follows_set_simd():
    assert stb.simd_name() == BACKENDS[0]  # the default is the best one
    for name in BACKENDS:
        stb.set_simd(name)
        assert stb.simd_name() == name
    stb.set_simd("auto")
    assert stb.simd_name() == BACKENDS[0]


@pytest.mark.parametrize("bad", ["", "AVX2", "avx3", "sse9", "off", "fast"])
def test_set_simd_rejects_unknown_names_and_changes_nothing(bad):
    if bad in BACKENDS:
        pytest.skip("a real backend on this CPU")
    stb.set_simd("scalar")
    with pytest.raises(ValueError, match="available"):
        stb.set_simd(bad)
    assert stb.simd_name() == "scalar"


def test_set_simd_wants_a_name_not_a_bool():
    with pytest.raises(TypeError):
        stb.set_simd(True)


@pytest.mark.parametrize("backend", BACKENDS)
@pytest.mark.parametrize("src, dst", [(s, d) for s in range(1, 5) for d in range(1, 5) if s != d])
def test_convert_equals_the_scalar_reference(backend, src, dst):
    img = stb.Image(rand(7, 1003, src, src * 10 + dst))  # 1003 is not a multiple of any vector width
    want = under("scalar", lambda: img.convert(stb.PixelFormat(dst)).numpy())
    np.testing.assert_array_equal(under(backend, lambda: img.convert(stb.PixelFormat(dst)).numpy()), want)


@pytest.mark.parametrize("backend", BACKENDS)
@pytest.mark.parametrize("dc", [1, 2, 3, 4])
@pytest.mark.parametrize("sc", [1, 2, 3, 4])
@pytest.mark.parametrize("offset", [(0, 0), (5, 3), (-20, -2), (250, 8)])
def test_composite_equals_the_scalar_reference(backend, dc, sc, offset):
    base = stb.Image(rand(11, 317, dc, dc * 7 + sc))
    over = stb.Image(rand(9, 211, sc, dc * 13 + sc + 100))
    want = under("scalar", lambda: base.composite(over, *offset).numpy())
    np.testing.assert_array_equal(under(backend, lambda: base.composite(over, *offset).numpy()), want)


@pytest.mark.parametrize("backend", BACKENDS)
@pytest.mark.parametrize("c", [2, 4])
def test_flatten_equals_the_scalar_reference(backend, c):
    img = stb.Image(rand(13, 999, c, 5))
    want = under("scalar", lambda: img.flatten((20, 40, 160)).numpy())
    np.testing.assert_array_equal(under(backend, lambda: img.flatten((20, 40, 160)).numpy()), want)


@pytest.mark.parametrize("backend", BACKENDS)
def test_the_alpha_regression_grid_is_right_in_every_backend(backend):
    da, sa = np.meshgrid(np.arange(256), np.arange(1, 256))
    for c in (255, 100, 1):
        base = stb.Image(np.stack([np.full(da.shape, c)] * 3 + [da], -1).astype(np.uint8))
        over = stb.Image(np.stack([np.full(da.shape, c)] * 3 + [sa], -1).astype(np.uint8))
        out = under(backend, lambda: base.composite(over).numpy())
        np.testing.assert_array_equal(out[..., :3], c)


# ---------------------------------------------------------------- per-thread override


def test_simd_context_sets_the_backend_for_the_block_only():
    default = stb.simd_name()
    with stb.simd("scalar"):
        assert stb.simd_name() == "scalar"
    assert stb.simd_name() == default


@pytest.mark.parametrize("backend", BACKENDS)
def test_simd_context_gives_the_same_pixels_as_set_simd(backend):
    img = stb.Image(rand(7, 1003, 4, 3))
    want = under("scalar", lambda: img.convert(stb.PixelFormat.GRAY).numpy())
    with stb.simd(backend):
        np.testing.assert_array_equal(img.convert(stb.PixelFormat.GRAY).numpy(), want)


def test_simd_contexts_nest_and_restore_even_after_an_exception():
    default = stb.simd_name()
    with stb.simd("scalar"):
        with stb.simd("auto"):
            assert stb.simd_name() == BACKENDS[0]
        assert stb.simd_name() == "scalar"
        with pytest.raises(RuntimeError, match="boom"):
            with stb.simd(BACKENDS[0]):
                raise RuntimeError("boom")
        assert stb.simd_name() == "scalar"
    assert stb.simd_name() == default


def test_simd_context_wins_over_set_simd_and_set_simd_still_applies_outside():
    stb.set_simd("scalar")
    with stb.simd("auto"):
        assert stb.simd_name() == BACKENDS[0]
        stb.set_simd("scalar")  # changes the global, not what this block uses
        assert stb.simd_name() == BACKENDS[0]
    assert stb.simd_name() == "scalar"


def test_simd_context_does_not_leak_into_other_threads():
    import threading

    seen = {}

    def other():
        seen["name"] = stb.simd_name()
        with stb.simd("scalar"):
            seen["inside"] = stb.simd_name()

    with stb.simd("scalar"):
        t = threading.Thread(target=other)
        t.start()
        t.join()
        assert stb.simd_name() == "scalar"
    assert seen["name"] == BACKENDS[0]  # the other thread follows the global, not our block
    assert seen["inside"] == "scalar"
    assert stb.simd_name() == BACKENDS[0]  # and its own block did not touch ours


@pytest.mark.parametrize("bad", ["", "AVX2", "avx3", "sse9", "fast"])
def test_simd_context_rejects_unknown_names_when_created(bad):
    if bad in BACKENDS:
        pytest.skip("a real backend on this CPU")
    with pytest.raises(ValueError, match="available"):
        stb.simd(bad)


def test_simd_context_cannot_be_entered_twice_at_once():
    ctx = stb.simd("scalar")
    with ctx:
        with pytest.raises(RuntimeError, match="already active"):
            ctx.__enter__()
    with ctx:  # but it can be reused afterwards
        assert stb.simd_name() == "scalar"


def _run_with_env(value):
    import os
    import subprocess
    import sys

    env = dict(os.environ, LIBSTB_SIMD=value)
    code = "import libstb as stb; print(stb.simd_name(), stb.simd_backends()[0])"
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
    # Compare the child with itself, not with this process: under an emulator (QEMU user mode with
    # -cpu Nehalem) only the parent is emulated, and the child it starts runs on the real CPU.
    name, best = r.stdout.split()
    assert name == (best if value == "auto" else "scalar")
