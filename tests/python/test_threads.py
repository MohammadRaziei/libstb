"""libstb.thread_count / set_threads / threads(): how many threads one call may use. A call with a
lot of work (so far resize) shares it between threads; the pixels never depend on the number."""

import threading

import pytest

np = pytest.importorskip("numpy")  # the pixel comparisons below need numpy

import libstb as stb


@pytest.fixture(autouse=True)
def restore_threads():
    stb.set_threads(0)
    yield
    stb.set_threads(0)


def photo(w, h, c, seed=0):
    return stb.Image(np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8))


# ---------------------------------------------------------------------- the setting


def test_thread_count_is_at_least_one_by_default():
    assert stb.thread_count() >= 1


def test_set_threads_is_a_ceiling_for_the_process():
    stb.set_threads(1)
    assert stb.thread_count() == 1
    stb.set_threads(6)
    assert stb.thread_count() == 6
    stb.set_threads(0)  # automatic: the CPUs this process may use
    assert stb.thread_count() >= 1


@pytest.mark.parametrize("bad", [-1, -100])
def test_set_threads_rejects_negative_numbers_and_changes_nothing(bad):
    stb.set_threads(3)
    with pytest.raises(ValueError, match="threads"):
        stb.set_threads(bad)
    assert stb.thread_count() == 3


@pytest.mark.parametrize("bad", [True, False, 2.0, "4", None, [2]])
def test_set_threads_wants_an_int_not_a_bool_or_anything_else(bad):
    stb.set_threads(3)
    with pytest.raises(TypeError, match="int"):
        stb.set_threads(bad)
    assert stb.thread_count() == 3


def test_set_threads_rejects_a_number_too_large_for_an_int():
    with pytest.raises(ValueError):
        stb.set_threads(2**40)
    with pytest.raises(ValueError):
        stb.set_threads(2**70)


# ---------------------------------------------------------------- per-thread override


def test_threads_context_sets_the_ceiling_for_the_block_only():
    stb.set_threads(5)
    with stb.threads(2):
        assert stb.thread_count() == 2
        with stb.threads(1):
            assert stb.thread_count() == 1
            stb.set_threads(9)  # the context still wins
            assert stb.thread_count() == 1
        assert stb.thread_count() == 2
    assert stb.thread_count() == 9


def test_threads_context_restores_the_ceiling_when_the_block_raises():
    stb.set_threads(4)
    with pytest.raises(RuntimeError):
        with stb.threads(1):
            raise RuntimeError("boom")
    assert stb.thread_count() == 4


def test_threads_context_changes_the_calling_thread_only():
    stb.set_threads(4)
    seen = {}

    def other():
        seen["elsewhere"] = stb.thread_count()  # follows the process setting
        with stb.threads(3):
            seen["own"] = stb.thread_count()

    with stb.threads(2):
        t = threading.Thread(target=other)
        t.start()
        t.join()
        assert stb.thread_count() == 2
    assert seen == {"elsewhere": 4, "own": 3}


def test_threads_context_rejects_negative_numbers():
    with pytest.raises(ValueError, match="threads"):
        stb.threads(-1)


@pytest.mark.parametrize("bad", [True, 1.5, "2", None])
def test_threads_context_wants_an_int(bad):
    with pytest.raises(TypeError, match="int"):
        stb.threads(bad)


def test_threads_context_cannot_be_entered_twice_at_once():
    ctx = stb.threads(2)
    with ctx:
        with pytest.raises(RuntimeError, match="already active"):
            ctx.__enter__()
    with ctx:  # and can be used again after leaving
        assert stb.thread_count() == 2


def test_threads_context_must_be_left_on_the_thread_that_entered_it():
    ctx = stb.threads(2)
    ctx.__enter__()
    errors = []

    def leave():
        try:
            ctx.__exit__(None, None, None)
        except RuntimeError as e:
            errors.append(str(e))

    t = threading.Thread(target=leave)
    t.start()
    t.join()
    assert errors and "thread that entered it" in errors[0]
    ctx.__exit__(None, None, None)  # the right thread: fine


# ------------------------------------------------------------------------- resize


@pytest.mark.parametrize("channels", [1, 2, 3, 4])
@pytest.mark.parametrize("filter", ["nearest", "bilinear", "auto", "bicubic", "mitchell"])
def test_resize_gives_the_same_pixels_on_any_number_of_threads(channels, filter):
    # Big enough that the work deserves threads on any machine (the C++ tests lower the grain instead).
    img = photo(1500, 1100, channels, seed=channels)
    with stb.threads(1):
        one = img.resize(700, 520, filter).numpy()
    with stb.threads(4):
        four = img.resize(700, 520, filter).numpy()
    with stb.threads(7):
        seven = img.resize(700, 520, filter).numpy()
    np.testing.assert_array_equal(four, one)
    np.testing.assert_array_equal(seven, one)


def test_resizer_object_is_unaffected_by_threads():
    img = photo(1200, 900, 4, seed=9)
    r = stb.Resizer("bicubic")
    with stb.threads(1):
        one = r.resize(img, 333, 250).numpy()
    with stb.threads(5):
        five = r.resize(img, 333, 250).numpy()
    np.testing.assert_array_equal(five, one)


def test_resizing_from_several_python_threads_at_once():
    img = photo(1200, 900, 3, seed=2)
    with stb.threads(1):
        want = img.resize(400, 300).numpy()
    results = [None] * 4

    def work(i):
        with stb.threads(3):
            results[i] = img.resize(400, 300).numpy()

    ts = [threading.Thread(target=work, args=(i,)) for i in range(4)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    for r in results:
        np.testing.assert_array_equal(r, want)
