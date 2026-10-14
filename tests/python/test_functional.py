"""imread / imwrite / iminfo: the functional API over Image."""

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb

SRC = np.arange(4 * 6 * 3, dtype=np.uint8).reshape(4, 6, 3)


@pytest.mark.parametrize("ext", [".png", ".bmp", ".tga", ".PNG", ".Tga"])
def test_imwrite_imread_round_trip_is_exact(tmp_path, ext):
    p = tmp_path / f"a{ext}"
    stb.imwrite(p, SRC)
    np.testing.assert_array_equal(stb.imread(p), SRC)
    np.testing.assert_array_equal(stb.imread(str(p)), SRC)


def test_imwrite_takes_an_image_or_an_array(tmp_path):
    stb.imwrite(tmp_path / "i.png", stb.Image(SRC))
    stb.imwrite(tmp_path / "a.png", SRC)
    np.testing.assert_array_equal(stb.imread(tmp_path / "i.png"), stb.imread(tmp_path / "a.png"))


def test_imwrite_accepts_grayscale_2d(tmp_path):
    g = np.arange(12, dtype=np.uint8).reshape(3, 4)
    stb.imwrite(tmp_path / "g.png", g)
    assert stb.imread(tmp_path / "g.png").shape == (3, 4, 1)


def test_imwrite_options_reach_the_encoder(tmp_path):
    big = np.random.default_rng(0).integers(0, 255, (64, 64, 3), dtype=np.uint8)
    stb.imwrite(tmp_path / "lo.jpg", big, quality=10)
    stb.imwrite(tmp_path / "hi.jpg", big, quality=95)
    assert (tmp_path / "lo.jpg").stat().st_size < (tmp_path / "hi.jpg").stat().st_size
    assert (tmp_path / "lo.jpg").read_bytes() == stb.Image(big).to_jpg(quality=10)
    stb.imwrite(tmp_path / "c.png", big, compression=9)
    assert (tmp_path / "c.png").read_bytes() == stb.Image(big).to_png(compression=9)
    stb.imwrite(tmp_path / "r.tga", big, rle=False)
    assert (tmp_path / "r.tga").read_bytes() == stb.Image(big).to_tga(rle=False)


def test_imwrite_jpeg_extensions_are_the_same_format(tmp_path):
    stb.imwrite(tmp_path / "a.jpg", SRC)
    stb.imwrite(tmp_path / "b.JPEG", SRC)
    assert (tmp_path / "a.jpg").read_bytes() == (tmp_path / "b.JPEG").read_bytes()


@pytest.mark.parametrize(
    "name, kwargs",
    [("a.png", {"quality": 50}), ("a.jpg", {"compression": 5}), ("a.bmp", {"rle": True}),
     ("a.tga", {"quality": 50})],
)
def test_imwrite_rejects_options_the_format_does_not_have(tmp_path, name, kwargs):
    with pytest.raises(ValueError, match="not an option"):
        stb.imwrite(tmp_path / name, SRC, **kwargs)
    assert not (tmp_path / name).exists()


def test_imwrite_unknown_extension_is_a_value_error(tmp_path):
    with pytest.raises(ValueError, match="unsupported extension"):
        stb.imwrite(tmp_path / "a.webp", SRC)
    with pytest.raises(ValueError, match="unsupported extension"):
        stb.imwrite(tmp_path / "noext", SRC)


def test_imwrite_bad_array_is_a_type_or_value_error(tmp_path):
    with pytest.raises((TypeError, ValueError)):
        stb.imwrite(tmp_path / "a.png", SRC.astype(np.float32))
    with pytest.raises((TypeError, ValueError)):
        stb.imwrite(tmp_path / "a.png", np.zeros((2, 2, 5), np.uint8))


def test_imread_options_and_bytes(tmp_path):
    data = stb.Image(SRC).to_png()
    assert stb.imread(data).shape == (4, 6, 3)
    assert stb.imread(data, channels=4).shape == (4, 6, 4)
    np.testing.assert_array_equal(stb.imread(data, flip=True), SRC[::-1])
    with pytest.raises(stb.LimitError):
        stb.imread(data, max_bytes=10)
    with pytest.raises(TypeError):
        stb.imread(data, 4)  # options are keyword-only


def test_imread_missing_file_and_garbage():
    with pytest.raises(FileNotFoundError):
        stb.imread("/definitely/not/here.png")
    with pytest.raises(stb.DecodeError):
        stb.imread(b"not an image")


def test_iminfo_is_header_only(tmp_path):
    data = stb.Image(SRC).to_png()
    assert stb.iminfo(data) == (6, 4, 3)
    assert stb.iminfo(data).channels == 3


def test_old_names_are_gone():
    assert not hasattr(stb, "load")
    assert not hasattr(stb, "info")
    assert not hasattr(stb.Image, "array")


# ------------------------------------------------------------------ imresize


@pytest.mark.parametrize("resizer", [None, "box", "mitchell", stb.Resizer("cubic")])
def test_imresize_equals_image_resize(resizer):
    src = np.random.default_rng(3).integers(0, 256, (20, 30, 3), dtype=np.uint8)
    want = stb.Image(src).resize(12, 9, resizer).numpy()
    got = stb.imresize(src, 12, 9, resizer)
    assert got.shape == (9, 12, 3) and got.dtype == np.uint8
    np.testing.assert_array_equal(got, want)


@pytest.mark.parametrize("c", [1, 2, 4])
def test_imresize_keeps_the_channel_count(c):
    src = np.random.default_rng(c).integers(0, 256, (8, 10, c), dtype=np.uint8)
    assert stb.imresize(src, 5, 4).shape == (4, 5, c)


def test_imresize_of_a_2d_array_is_2d():
    g = np.arange(12, dtype=np.uint8).reshape(3, 4)
    assert stb.imresize(g, 8, 6).shape == (6, 8)


def test_imresize_takes_an_image_and_returns_an_array():
    out = stb.imresize(stb.Image(SRC), 3, 2)
    assert isinstance(out, np.ndarray) and out.shape == (2, 3, 3)


def test_imresize_reports_bad_input_like_image():
    with pytest.raises(ValueError):
        stb.imresize(SRC, 0, 4)
    with pytest.raises(TypeError):
        stb.imresize(SRC.astype(np.float32), 3, 3)
    with pytest.raises(ValueError):
        stb.imresize(SRC, 3, 3, "no-such-filter")
