"""threshold, otsu_threshold, adaptive_threshold, the morphology functions, connected_components, canny and scharr.
The C++ tests (tests/cpp/test_threshold.cpp, test_morphology.cpp, test_components.cpp, test_canny.cpp, test_filter.cpp) work
the small cases out by hand; here the values are compared with independent implementations (numpy, scipy, scikit-image) and
the Python surface is checked: Image and float32 array forms, names and enums, inplace, errors. scipy and scikit-image are
optional: the tests that need them are skipped without them."""

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb


def rand8(h, w, c=1, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def randf(h, w, c=None, seed=0):
    shape = (h, w) if c is None else (h, w, c)
    return np.random.default_rng(seed).random(shape, dtype=np.float32)


def mask_of(image):
    """An Image mask as a (H, W) uint8 array."""
    return np.asarray(image)[..., 0]


SCIPY_MODES = {"clamp": "nearest", "reflect": "reflect", "mirror": "mirror", "wrap": "wrap", "zero": "constant"}


# ------------------------------------------------------------------------------------------------- thresholds

def test_threshold_is_the_step_function_of_the_difference():
    a = rand8(20, 30, seed=1)
    img = stb.Image(a)
    want = np.where(a[..., 0] > 100, 255, 0).astype(np.uint8)
    assert np.array_equal(mask_of(stb.threshold(img, 100)), want)
    assert np.array_equal(mask_of(stb.threshold(img, 100, invert=True)), 255 - want)
    assert np.array_equal(mask_of(img.threshold(100)), want)
    f = randf(20, 30, seed=2)
    assert np.array_equal(mask_of(stb.threshold(f, 0.5)), np.where(f > 0.5, 255, 0).astype(np.uint8))
    for bad in (-1, 256):
        with pytest.raises(ValueError):
            stb.threshold(img, bad)
    with pytest.raises(ValueError):
        stb.threshold(stb.Image(rand8(4, 4, 3)), 10)  # one channel only


def test_otsu_threshold_matches_scikit_image():
    filters = pytest.importorskip("skimage.filters")
    for seed in (3, 4, 5):
        a = rand8(40, 50, seed=seed)
        assert stb.otsu_threshold(stb.Image(a)) == int(filters.threshold_otsu(a[..., 0]))
    # two clusters of different sizes: the level lies between them
    b = np.concatenate([np.full(300, 40), np.full(100, 210)]).astype(np.uint8).reshape(20, 20, 1)
    assert stb.otsu_threshold(stb.Image(b)) == int(filters.threshold_otsu(b[..., 0]))
    f = randf(40, 50, seed=6)
    width = (f.max() - f.min()) / 256
    assert stb.otsu_threshold(f) == pytest.approx(float(filters.threshold_otsu(f)), abs=float(width))


@pytest.mark.parametrize("method", ["mean", "gaussian"])
@pytest.mark.parametrize("radius", [2, 5])
def test_adaptive_threshold_matches_the_formula_with_scipy(method, radius):
    ndimage = pytest.importorskip("scipy.ndimage")
    a = rand8(40, 45, seed=7)[..., 0]
    img = stb.Image(a[..., None])
    values = a.astype(np.float64)
    if method == "mean":
        local = ndimage.uniform_filter(values, size=2 * radius + 1, mode="nearest")
    else:
        sigma = 0.3 * radius + 0.5
        local = ndimage.gaussian_filter(values, sigma, truncate=radius / sigma, mode="nearest")
    want = np.where(values > local - 6.0, 255, 0).astype(np.uint8)
    got = mask_of(stb.adaptive_threshold(img, radius, 6.0, method))
    assert (got != want).mean() < 0.002  # only values that fall within rounding of their level can differ
    inverted = mask_of(stb.adaptive_threshold(img, radius, 6.0, method, True))
    assert np.array_equal(inverted, 255 - got)
    assert stb.adaptive_threshold(randf(20, 20, seed=8), radius, 0.05, method).__class__ is stb.Image


def test_adaptive_threshold_finds_a_dark_line_on_an_uneven_page_and_errors():
    page = np.tile((100 + (100 * np.arange(41)) // 40).astype(np.uint8), (40, 1))
    page[:, 20] -= 60
    mask = mask_of(stb.adaptive_threshold(stb.Image(page[..., None]), 5, 10.0, stb.AdaptiveMethod.MEAN, True))
    assert np.array_equal(mask == 255, np.arange(41)[None, :].repeat(40, 0) == 20)
    flat = stb.Image(np.full((10, 10, 1), 77, dtype=np.uint8))
    assert not mask_of(stb.adaptive_threshold(flat, 3)).any()  # a flat page is background for offset 0
    for bad in ({"radius": 0}, {"radius": 1001}, {"radius": 3, "offset": float("nan")}, {"radius": 3, "method": "median"}):
        with pytest.raises(ValueError):
            stb.adaptive_threshold(flat, **bad)


# ------------------------------------------------------------------------------------------------- morphology

def test_structuring_elements():
    rect = mask_of(stb.structuring_element("rect", 3))
    assert rect.shape == (3, 3) and (rect == 255).all()
    cross = mask_of(stb.structuring_element(stb.MorphShape.CROSS, 5))
    assert (cross == 255).sum() == 9 and cross[2].all() and cross[:, 2].all()
    assert (mask_of(stb.structuring_element("ellipse", 5)) == 255).sum() == 13
    assert np.array_equal(mask_of(stb.structuring_element("ellipse", 3)), mask_of(stb.structuring_element("cross", 3)))
    assert mask_of(stb.structuring_element("rect", 5, 3)).shape == (3, 5)
    for args in (("rect", 4), ("rect", 3, 2), ("hexagon", 3)):
        with pytest.raises(ValueError):
            stb.structuring_element(*args)


@pytest.mark.parametrize("edge", sorted(SCIPY_MODES))
@pytest.mark.parametrize("shape,size", [("rect", (3, 3)), ("rect", (5, 3)), ("cross", (5, 5)), ("ellipse", (7, 7))])
def test_erode_dilate_open_close_match_scipy(edge, shape, size):
    ndimage = pytest.importorskip("scipy.ndimage")
    se = stb.structuring_element(shape, *size)
    footprint = mask_of(se) > 0
    a = rand8(21, 17, 3, seed=9)
    img = stb.Image(a)
    mode = SCIPY_MODES[edge]
    for name, scipy_fn in (("erode", ndimage.grey_erosion), ("dilate", ndimage.grey_dilation),
                           ("morph_open", ndimage.grey_opening), ("morph_close", ndimage.grey_closing)):
        got = np.asarray(getattr(stb, name)(img, se, edge=edge))
        for k in range(3):
            want = scipy_fn(a[..., k], footprint=footprint, mode=mode, cval=0)
            assert np.array_equal(got[..., k], want), (name, edge, shape, k)
    f = randf(15, 19, 2, seed=10)
    got = stb.erode(f, se, edge=edge)
    assert got.dtype == np.float32 and got.shape == f.shape
    for k in range(2):
        assert np.array_equal(got[..., k], ndimage.grey_erosion(f[..., k], footprint=footprint, mode=mode, cval=0))


def test_gradient_and_hats_are_the_differences_and_never_negative():
    ndimage = pytest.importorskip("scipy.ndimage")
    a = rand8(25, 31, 1, seed=11)
    se = stb.structuring_element("ellipse", 5)
    footprint = mask_of(se) > 0
    for edge in SCIPY_MODES:
        mode = SCIPY_MODES[edge]
        plane = a[..., 0].astype(int)
        opened = ndimage.grey_opening(a[..., 0], footprint=footprint, mode=mode, cval=0).astype(int)
        closed = ndimage.grey_closing(a[..., 0], footprint=footprint, mode=mode, cval=0).astype(int)
        dil = ndimage.grey_dilation(a[..., 0], footprint=footprint, mode=mode, cval=0).astype(int)
        ero = ndimage.grey_erosion(a[..., 0], footprint=footprint, mode=mode, cval=0).astype(int)
        img = stb.Image(a)
        assert np.array_equal(np.asarray(stb.top_hat(img, se, edge=edge))[..., 0], np.maximum(plane - opened, 0))
        assert np.array_equal(np.asarray(stb.black_hat(img, se, edge=edge))[..., 0], np.maximum(closed - plane, 0))
        assert np.array_equal(np.asarray(stb.morph_gradient(img, se, edge=edge))[..., 0], np.maximum(dil - ero, 0))


def test_open_removes_a_speck_and_close_fills_a_hole_and_the_forms_agree():
    pic = np.zeros((9, 9, 1), dtype=np.uint8)
    pic[2:7, 2:7] = 255
    se = stb.structuring_element("rect", 3)
    speck = pic.copy()
    speck[8, 0] = 255
    opened = stb.morph_open(stb.Image(speck), se)
    assert mask_of(opened)[8, 0] == 0 and (mask_of(opened) == 255).sum() == 25
    holed = pic.copy()
    holed[4, 4] = 0
    assert (mask_of(stb.morph_close(stb.Image(holed), se)) == 255).sum() == 25
    img = stb.Image(rand8(12, 12, 3, seed=12))
    out = img.erode(se)
    assert np.array_equal(np.asarray(out), np.asarray(stb.erode(img, se)))
    same = img.copy()
    assert same.dilate_inplace(se) is same
    assert np.array_equal(np.asarray(same), np.asarray(stb.dilate(img, se)))
    for name in ("erode", "dilate", "morph_open", "morph_close", "morph_gradient", "top_hat", "black_hat"):
        assert np.array_equal(np.asarray(getattr(img, name)(se, edge="mirror")), np.asarray(getattr(stb, name)(img, se, edge="mirror")))


def test_morphology_errors():
    img = stb.Image(rand8(8, 8, 1))
    with pytest.raises(ValueError):
        stb.erode(img, stb.Image(np.zeros((3, 3, 1), dtype=np.uint8)))  # no member
    with pytest.raises(ValueError):
        stb.erode(img, stb.Image(np.full((4, 3, 1), 255, dtype=np.uint8)))  # even width
    with pytest.raises(ValueError):
        stb.erode(img, stb.Image(np.full((3, 3, 3), 255, dtype=np.uint8)))  # not one channel
    with pytest.raises(ValueError):
        stb.erode(img, stb.structuring_element("rect", 3), edge="nowhere")


# ------------------------------------------------------------------------------------------ connected components

@pytest.mark.parametrize("connectivity,structure", [("four", None), ("eight", np.ones((3, 3)))])
def test_connected_components_match_scipy_label(connectivity, structure):
    ndimage = pytest.importorskip("scipy.ndimage")
    rng = np.random.default_rng(13)
    for density in (0.3, 0.5, 0.65):
        mask = (rng.random((40, 55)) < density).astype(np.uint8) * 255
        c = stb.connected_components(stb.Image(mask[..., None]), connectivity)
        want, count = ndimage.label(mask > 0, structure=structure)
        assert c.count == count
        assert c.labels.dtype == np.int32 and c.labels.shape == mask.shape
        assert np.array_equal(c.labels, want)
        assert c.stats.shape == (count, 7)
        index = np.arange(1, count + 1)
        assert np.array_equal(c.stats[:, 0], np.bincount(want.ravel())[1:])
        for i, box in enumerate(ndimage.find_objects(want)):
            assert tuple(c.stats[i, 1:5]) == (box[1].start, box[0].start, box[1].stop - box[1].start, box[0].stop - box[0].start)
        centres = np.array(ndimage.center_of_mass(mask > 0, want, index))
        assert np.allclose(c.stats[:, 5], centres[:, 1], atol=1e-4) and np.allclose(c.stats[:, 6], centres[:, 0], atol=1e-4)


def test_connected_components_of_a_label_map_and_the_object():
    ndimage = pytest.importorskip("scipy.ndimage")
    rng = np.random.default_rng(14)
    labels = rng.integers(-1, 3, (30, 40), dtype=np.int32)
    c = stb.connected_components(labels)
    # the same partition as scipy gives when each label value is labelled on its own (the numbering may differ)
    ref = np.zeros(labels.shape, dtype=np.int64)
    next_id = 0
    for value in (0, 1, 2):
        piece, n = ndimage.label(labels == value)
        ref[piece > 0] = piece[piece > 0] + next_id
        next_id += n
    assert c.count == next_id
    pairs = {(int(a), int(b)) for a, b in zip(c.labels.ravel(), ref.ravel())}
    assert len(pairs) == next_id + 1  # a bijection between the pieces, plus (0, 0) for the background
    assert (c.labels[labels < 0] == 0).all()
    # the label array views the object: read-only, and it stays valid while it is used
    view = c.labels
    assert not view.flags.writeable
    assert "Components" in repr(c)
    assert stb.connected_components(stb.Image(np.zeros((5, 5, 1), dtype=np.uint8))).count == 0


def test_connected_components_errors():
    with pytest.raises(ValueError):
        stb.connected_components(stb.Image(rand8(5, 5, 3)))
    with pytest.raises(ValueError):
        stb.connected_components(stb.Image(rand8(5, 5, 1)), "six")
    with pytest.raises(ValueError):
        stb.connected_components(np.zeros((2, 3, 4), dtype=np.int32))


# ------------------------------------------------------------------------------------------------ canny, scharr

def disk(size=80, radius=25):
    y, x = np.mgrid[:size, :size]
    return (np.hypot(x - size / 2 + 0.5, y - size / 2 + 0.5) < radius).astype(np.float32)


def test_canny_finds_the_ring_of_a_disk_like_scikit_image():
    feature = pytest.importorskip("skimage.feature")
    ndimage = pytest.importorskip("scipy.ndimage")
    pic = disk()
    ours = mask_of(stb.canny(pic)) > 0
    theirs = feature.canny(pic, sigma=1.0, low_threshold=0.05, high_threshold=0.2)
    assert ours.sum() > 50 and theirs.sum() > 50
    near_theirs = ndimage.binary_dilation(theirs, iterations=1)
    near_ours = ndimage.binary_dilation(ours, iterations=1)
    assert (ours & near_theirs).sum() / ours.sum() > 0.9
    assert (theirs & near_ours).sum() / theirs.sum() > 0.9
    # the 8-bit Image and the colour Image give the same ring
    u8 = (pic * 255).astype(np.uint8)
    gray = stb.Image(u8[..., None])
    assert np.array_equal(mask_of(stb.canny(gray)), mask_of(stb.canny(pic)))
    rgb = stb.Image(np.repeat(u8[..., None], 3, axis=2))
    assert np.array_equal(mask_of(stb.canny(rgb)), mask_of(stb.canny(pic)))
    assert np.array_equal(mask_of(gray.canny()), mask_of(stb.canny(gray)))


def test_canny_limits_and_errors():
    pic = disk()
    assert mask_of(stb.canny(np.full((20, 20), 0.4, dtype=np.float32))).sum() == 0
    assert mask_of(stb.canny(pic, 0.1, 0.2, 0.0)).sum() > 0
    for bad in ({"low": 0.3, "high": 0.2}, {"low": -1.0}, {"sigma": -1.0}, {"sigma": float("inf")}):
        with pytest.raises(ValueError):
            stb.canny(pic, **bad)
    with pytest.raises(ValueError):
        stb.canny(np.zeros((8, 8, 2), dtype=np.float32))


def test_scharr_of_a_step_is_16_times_its_height():
    step = np.zeros((5, 6), dtype=np.float32)
    step[:, 3:] = 1.0
    dx, dy = stb.scharr(step)
    assert dx.shape == step.shape and dx.dtype == np.float32
    assert dx[2, 2] == pytest.approx(16.0, abs=1e-5) and dx[2, 3] == pytest.approx(16.0, abs=1e-5)
    assert abs(dx[2, 0]) < 1e-6 and np.abs(dy).max() < 1e-6
    img = stb.Image((step * 255).astype(np.uint8)[..., None])
    dx8, _ = stb.scharr(img)
    assert dx8.shape == (5, 6, 1) and dx8[2, 2, 0] == pytest.approx(16.0, abs=1e-4)


def test_the_number_of_threads_does_not_change_the_results():
    img = stb.Image(rand8(300, 260, 1, seed=15))
    se = stb.structuring_element("ellipse", 7)
    mask = stb.threshold(img, 128)

    def run():
        return (mask_of(stb.canny(img)), np.asarray(stb.morph_gradient(img, se)), mask_of(stb.adaptive_threshold(img, 6, 4.0)),
                stb.connected_components(mask, "eight").labels.copy())

    with stb.threads(1):
        one = run()
    with stb.threads(4):
        many = run()
    for x, y in zip(one, many):
        assert np.array_equal(x, y)
