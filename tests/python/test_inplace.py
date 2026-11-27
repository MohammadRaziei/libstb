"""The sixteen functions that change their argument (equalize_inplace, gamma_inplace, erode_inplace, ...): one name for an Image,
a uint8 array and a float32 array, a method of Image, the argument given back so calls chain, and nothing converted silently."""

import pytest

np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb

RNG = np.random.default_rng(3)
RGB8 = RNG.integers(0, 256, (12, 16, 3), dtype=np.uint8)
SE = stb.structuring_element("rect", 3)

# name, arguments after the data, the function without _inplace that gives the same result
CASES = [
    ("equalize_inplace", (), "equalize"),
    ("auto_contrast_inplace", (1.0,), "auto_contrast"),
    ("clahe_inplace", (2.0, 4, 3), "clahe"),
    ("gamma_inplace", (1.7,), "gamma"),
    ("levels_inplace", (0.1, 0.9, 1.2), "levels"),
    ("convert_color_inplace", ("rgb", "hsv"), "convert_color"),
    ("pixelize_inplace", (3,), "pixelize"),
    ("unsharp_mask_inplace", (1.0, 1.2, 0.01), "unsharp_mask"),
    ("erode_inplace", (SE,), "erode"),
    ("dilate_inplace", (SE,), "dilate"),
    ("morph_open_inplace", (SE,), "morph_open"),
    ("morph_close_inplace", (SE,), "morph_close"),
    ("morph_gradient_inplace", (SE,), "morph_gradient"),
    ("top_hat_inplace", (SE,), "top_hat"),
    ("black_hat_inplace", (SE,), "black_hat"),
]
IDS = [c[0] for c in CASES]


def same_values(a, b):
    return np.array_equal(np.asarray(a), np.asarray(b))


def test_there_are_sixteen_functions_and_they_all_exist_twice():
    names = {c[0] for c in CASES} | {"match_histogram_inplace"}
    assert len(names) == 16
    for name in names:
        assert callable(getattr(stb, name)) and callable(getattr(stb.Image, name))


@pytest.mark.parametrize("name,args,plain", CASES, ids=IDS)
def test_an_image_is_changed_in_place_and_given_back(name, args, plain):
    img = stb.Image(RGB8.copy())
    want = getattr(stb, plain)(stb.Image(RGB8.copy()), *args)
    assert getattr(stb, name)(img, *args) is img
    assert same_values(img.numpy(), want.numpy())
    img2 = stb.Image(RGB8.copy())
    assert getattr(img2, name)(*args) is img2  # the method
    assert same_values(img2.numpy(), want.numpy())


@pytest.mark.parametrize("name,args,plain", CASES, ids=IDS)
def test_a_uint8_array_is_changed_in_place_and_given_back(name, args, plain):
    arr = RGB8.copy()
    want = getattr(stb, plain)(stb.Image(RGB8.copy()), *args).numpy()
    assert getattr(stb, name)(arr, *args) is arr
    assert same_values(arr, want)


@pytest.mark.parametrize("name,args,plain", CASES, ids=IDS)
def test_a_float32_array_is_changed_in_place_and_given_back(name, args, plain):
    arr = RGB8.astype(np.float32) / 255
    want = getattr(stb, plain)(arr.copy(), *args)
    assert getattr(stb, name)(arr, *args) is arr
    assert same_values(arr, want)


def test_match_histogram_takes_a_second_value_of_the_same_kind():
    ref8 = RNG.integers(0, 256, (7, 9, 3), dtype=np.uint8)
    img, arr, farr = stb.Image(RGB8.copy()), RGB8.copy(), RGB8.astype(np.float32) / 255
    want = stb.match_histogram(stb.Image(RGB8.copy()), stb.Image(ref8)).numpy()
    assert stb.match_histogram_inplace(img, stb.Image(ref8)) is img and same_values(img.numpy(), want)
    assert stb.match_histogram_inplace(arr, ref8) is arr and same_values(arr, want)
    fref = ref8.astype(np.float32) / 255
    assert stb.match_histogram_inplace(farr, fref) is farr
    with pytest.raises(TypeError):
        stb.match_histogram_inplace(arr, fref)  # a uint8 array with a float32 reference
    with pytest.raises(TypeError):
        stb.match_histogram_inplace(farr.copy(), ref8)  # a float32 array with a uint8 reference
    # an Image takes a uint8 array as its reference, as everywhere an Image is a parameter (Image converts from a uint8 array)
    img2 = stb.Image(RGB8.copy())
    assert stb.match_histogram_inplace(img2, ref8) is img2 and same_values(img2.numpy(), want)


def test_calls_chain_for_every_kind():
    img = stb.Image(RGB8.copy())
    assert img.equalize_inplace().gamma_inplace(1.5).pixelize_inplace(2) is img
    arr = RGB8.copy()
    assert stb.pixelize_inplace(stb.gamma_inplace(stb.equalize_inplace(arr), 1.5), 2) is arr
    want = stb.pixelize(stb.gamma(stb.equalize(stb.Image(RGB8.copy())), 1.5), 2).numpy()
    assert same_values(img.numpy(), want) and same_values(arr, want)


@pytest.mark.parametrize("name,args,plain", CASES[:6], ids=IDS[:6])
def test_nothing_is_converted_silently(name, args, plain):
    fn = getattr(stb, name)
    ro = RGB8.copy()
    ro.flags.writeable = False
    for bad in (RGB8.astype(np.float64), RGB8.astype(np.int32), ro, RGB8[:, ::2], [[1, 2], [3, 4]], "x.png"):
        with pytest.raises(TypeError):
            fn(bad, *args)


def test_a_refused_array_is_left_untouched():
    arr = RGB8.astype(np.float64)
    before = arr.copy()
    with pytest.raises(TypeError):
        stb.gamma_inplace(arr, 2.0)
    assert same_values(arr, before)


def test_enums_are_given_by_name_in_any_case_or_as_themselves():
    a, b, c = RGB8.copy(), RGB8.copy(), RGB8.copy()
    stb.equalize_inplace(a, mode="per_channel")
    stb.equalize_inplace(b, mode="PER_CHANNEL")
    stb.equalize_inplace(c, mode=stb.ToneMode.PER_CHANNEL)
    assert same_values(a, b) and same_values(a, c)
    with pytest.raises(ValueError, match="unknown tone mode"):
        stb.equalize_inplace(RGB8.copy(), mode="bright")
    with pytest.raises(ValueError, match="unknown edge mode"):
        stb.erode_inplace(RGB8.copy(), SE, edge="nowhere")
    with pytest.raises(ValueError, match="unknown color space"):
        stb.convert_color_inplace(RGB8.copy(), "rgb", "cmyk")
    with pytest.raises(TypeError):
        stb.equalize_inplace(RGB8.copy(), mode=3)


def test_the_array_shapes_are_checked():
    for bad in (np.zeros(5, np.uint8), np.zeros((2, 3, 4, 5), np.uint8), np.zeros((0, 4), np.uint8)):
        with pytest.raises(ValueError):
            stb.equalize_inplace(bad)
    gray = RGB8[..., 0].copy()
    assert stb.gamma_inplace(gray, 2.0) is gray  # (H, W) is one channel


def test_the_old_keyword_is_gone():
    with pytest.raises(TypeError):
        stb.Image(RGB8.copy()).equalize(inplace=True)
    with pytest.raises(TypeError):
        stb.Image(RGB8.copy()).erode(SE, inplace=True)
