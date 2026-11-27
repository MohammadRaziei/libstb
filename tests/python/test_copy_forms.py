"""The sixteen functions that make a new value (equalize, gamma, erode, ...): one name for an Image, a uint8 array and a float32 array,
a method of Image, the argument left alone, and nothing converted silently (a uint8 array stays uint8: it is never read as floats)."""

import pytest

np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb

RNG = np.random.default_rng(5)
RGB8 = RNG.integers(0, 256, (12, 16, 3), dtype=np.uint8)
GRAY8 = RNG.integers(0, 256, (9, 11), dtype=np.uint8)
SE = stb.structuring_element("rect", 3)

# name, arguments after the data
CASES = [
    ("equalize", ()),
    ("auto_contrast", (1.0,)),
    ("clahe", (2.0, 4, 3)),
    ("gamma", (1.7,)),
    ("levels", (0.1, 0.9, 1.2)),
    ("convert_color", ("rgb", "hsv")),
    ("pixelize", (3,)),
    ("unsharp_mask", (1.0, 1.2, 0.01)),
    ("erode", (SE,)),
    ("dilate", (SE,)),
    ("morph_open", (SE,)),
    ("morph_close", (SE,)),
    ("morph_gradient", (SE,)),
    ("top_hat", (SE,)),
    ("black_hat", (SE,)),
]
IDS = [c[0] for c in CASES]


def reference(name, args, data):
    """What the function must give: the in-place form on a copy of the data (the in-place forms are tested apart)."""
    copy = stb.Image(data.copy()) if data.dtype == np.uint8 else data.copy()
    out = getattr(stb, name + "_inplace")(copy, *args)
    return out.numpy() if isinstance(out, stb.Image) else out


def test_there_are_sixteen_functions_and_they_all_exist_twice():
    names = {c[0] for c in CASES} | {"match_histogram"}
    assert len(names) == 16
    for name in names:
        assert callable(getattr(stb, name)) and callable(getattr(stb.Image, name))


@pytest.mark.parametrize("name,args", CASES, ids=IDS)
def test_an_image_gives_a_new_image_and_is_not_changed(name, args):
    img = stb.Image(RGB8.copy())
    out = getattr(stb, name)(img, *args)
    assert isinstance(out, stb.Image) and out is not img
    assert np.array_equal(img.numpy(), RGB8)
    assert np.array_equal(out.numpy(), reference(name, args, RGB8))
    assert np.array_equal(getattr(img, name)(*args).numpy(), out.numpy())  # the method


@pytest.mark.parametrize("name,args", CASES, ids=IDS)
def test_a_uint8_array_gives_a_new_uint8_array_with_the_values_of_an_image(name, args):
    arr = RGB8.copy()
    out = getattr(stb, name)(arr, *args)
    assert isinstance(out, np.ndarray) and out.dtype == np.uint8 and out.shape == arr.shape
    assert not np.shares_memory(out, arr) and np.array_equal(arr, RGB8)
    assert np.array_equal(out, reference(name, args, RGB8))


@pytest.mark.parametrize("name,args", CASES, ids=IDS)
def test_a_float32_array_gives_a_new_float32_array(name, args):
    arr = RGB8.astype(np.float32) / 255
    keep = arr.copy()
    out = getattr(stb, name)(arr, *args)
    assert out.dtype == np.float32 and out.shape == arr.shape
    assert not np.shares_memory(out, arr) and np.array_equal(arr, keep)
    assert np.array_equal(out, reference(name, args, keep))


@pytest.mark.parametrize("name,args", CASES, ids=IDS)
def test_a_read_only_array_is_read_and_a_gray_array_keeps_its_shape(name, args):
    if name == "convert_color":
        pytest.skip("a colour conversion needs three channels")
    ro = GRAY8.copy()
    ro.flags.writeable = False
    out = getattr(stb, name)(ro, *args)
    assert out.shape == GRAY8.shape and out.dtype == np.uint8
    rof = GRAY8.astype(np.float32) / 255
    rof.flags.writeable = False
    assert getattr(stb, name)(rof, *args).shape == GRAY8.shape


@pytest.mark.parametrize("name,args", CASES, ids=IDS)
def test_nothing_is_converted_silently(name, args):
    fn = getattr(stb, name)
    for bad in (RGB8.astype(np.float64), RGB8.astype(np.int32), RGB8.astype(np.uint16), np.asfortranarray(RGB8), RGB8[:, ::2]):
        with pytest.raises(TypeError):
            fn(bad, *args)
    with pytest.raises(TypeError):
        fn([[1, 2], [3, 4]], *args)


def test_a_uint8_array_is_not_read_as_floats():
    """The bug this replaces: equalize and gamma took a uint8 array as float32 with values 0 to 255 and gave a wrong answer."""
    out = stb.gamma(GRAY8, 2.0)
    assert out.dtype == np.uint8
    assert np.array_equal(out, stb.gamma(stb.Image(GRAY8.copy()), 2.0).numpy()[:, :, 0])  # an Image of one channel is (H, W, 1)
    assert np.array_equal(stb.equalize(GRAY8), stb.equalize(stb.Image(GRAY8.copy())).numpy()[:, :, 0])


@pytest.mark.parametrize("name,args", CASES, ids=IDS)
def test_the_result_has_the_shape_of_the_argument(name, args):
    if name == "convert_color":
        pytest.skip("a colour conversion needs three channels")
    for dtype in (np.uint8, np.float32):
        gray3 = GRAY8.astype(dtype)[:, :, None]  # (H, W, 1)
        gray2 = GRAY8.astype(dtype)  # (H, W)
        if dtype == np.float32:
            gray3, gray2 = gray3 / 255, gray2 / 255
        assert getattr(stb, name)(gray3, *args).shape == gray3.shape
        assert getattr(stb, name)(gray2, *args).shape == gray2.shape


def test_calls_chain_on_an_image_and_on_arrays():
    img = stb.Image(RGB8.copy())
    want = stb.dilate(stb.gamma(stb.equalize(RGB8), 1.3), SE)
    assert np.array_equal(img.equalize().gamma(1.3).dilate(SE).numpy(), want)
    assert np.array_equal(stb.dilate(stb.gamma(stb.equalize(img), 1.3), SE).numpy(), want)


def test_match_histogram_takes_a_second_value_of_the_same_kind():
    ref8 = RNG.integers(0, 256, (7, 9, 3), dtype=np.uint8)
    want = stb.match_histogram(stb.Image(RGB8.copy()), stb.Image(ref8)).numpy()
    assert np.array_equal(stb.match_histogram(RGB8, ref8), want)
    out = stb.match_histogram(RGB8.astype(np.float32) / 255, ref8.astype(np.float32) / 255)
    assert out.dtype == np.float32
    with pytest.raises(TypeError):
        stb.match_histogram(RGB8, ref8.astype(np.float32) / 255)
    # an Image and a uint8 array is a call on arrays: the Image is read as an array (it has the array interface), the result is an array
    mixed = stb.match_histogram(stb.Image(RGB8.copy()), ref8)
    assert isinstance(mixed, np.ndarray) and np.array_equal(mixed, want)
    with pytest.raises(TypeError):
        stb.match_histogram(stb.Image(RGB8.copy()), ref8.astype(np.float32) / 255)


def test_enums_are_given_by_name_in_any_case_or_as_themselves():
    assert np.array_equal(stb.equalize(RGB8, "PER_CHANNEL"), stb.equalize(RGB8, stb.ToneMode.PER_CHANNEL))
    with pytest.raises(ValueError):
        stb.equalize(RGB8, "nonsense")
    with pytest.raises(ValueError):
        stb.unsharp_mask(RGB8, 1.0, edge="nonsense")


def test_the_result_owns_its_memory():
    outs = [stb.gamma(RGB8, 1.0 + i / 10) for i in range(30)]
    import gc

    gc.collect()
    assert all(np.array_equal(o, stb.gamma(RGB8, 1.0 + i / 10)) for i, o in enumerate(outs))
