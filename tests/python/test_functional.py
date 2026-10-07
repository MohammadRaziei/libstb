"""imread / imwrite / iminfo: the functional API over Image."""

import numpy as np
import pytest

import libstb

SRC = np.arange(4 * 6 * 3, dtype=np.uint8).reshape(4, 6, 3)


@pytest.mark.parametrize("ext", [".png", ".bmp", ".tga", ".PNG", ".Tga"])
def test_imwrite_imread_round_trip_is_exact(tmp_path, ext):
    p = tmp_path / f"a{ext}"
    libstb.imwrite(p, SRC)
    np.testing.assert_array_equal(libstb.imread(p), SRC)
    np.testing.assert_array_equal(libstb.imread(str(p)), SRC)


def test_imwrite_takes_an_image_or_an_array(tmp_path):
    libstb.imwrite(tmp_path / "i.png", libstb.Image(SRC))
    libstb.imwrite(tmp_path / "a.png", SRC)
    np.testing.assert_array_equal(libstb.imread(tmp_path / "i.png"), libstb.imread(tmp_path / "a.png"))


def test_imwrite_accepts_grayscale_2d(tmp_path):
    g = np.arange(12, dtype=np.uint8).reshape(3, 4)
    libstb.imwrite(tmp_path / "g.png", g)
    assert libstb.imread(tmp_path / "g.png").shape == (3, 4, 1)


def test_imwrite_options_reach_the_encoder(tmp_path):
    big = np.random.default_rng(0).integers(0, 255, (64, 64, 3), dtype=np.uint8)
    libstb.imwrite(tmp_path / "lo.jpg", big, quality=10)
    libstb.imwrite(tmp_path / "hi.jpg", big, quality=95)
    assert (tmp_path / "lo.jpg").stat().st_size < (tmp_path / "hi.jpg").stat().st_size
    assert (tmp_path / "lo.jpg").read_bytes() == libstb.Image(big).to_jpg(quality=10)
    libstb.imwrite(tmp_path / "c.png", big, compression=9)
    assert (tmp_path / "c.png").read_bytes() == libstb.Image(big).to_png(compression=9)
    libstb.imwrite(tmp_path / "r.tga", big, rle=False)
    assert (tmp_path / "r.tga").read_bytes() == libstb.Image(big).to_tga(rle=False)


def test_imwrite_jpeg_extensions_are_the_same_format(tmp_path):
    libstb.imwrite(tmp_path / "a.jpg", SRC)
    libstb.imwrite(tmp_path / "b.JPEG", SRC)
    assert (tmp_path / "a.jpg").read_bytes() == (tmp_path / "b.JPEG").read_bytes()


@pytest.mark.parametrize(
    "name, kwargs",
    [("a.png", {"quality": 50}), ("a.jpg", {"compression": 5}), ("a.bmp", {"rle": True}),
     ("a.tga", {"quality": 50})],
)
def test_imwrite_rejects_options_the_format_does_not_have(tmp_path, name, kwargs):
    with pytest.raises(ValueError, match="not an option"):
        libstb.imwrite(tmp_path / name, SRC, **kwargs)
    assert not (tmp_path / name).exists()


def test_imwrite_unknown_extension_is_a_value_error(tmp_path):
    with pytest.raises(ValueError, match="unsupported extension"):
        libstb.imwrite(tmp_path / "a.webp", SRC)
    with pytest.raises(ValueError, match="unsupported extension"):
        libstb.imwrite(tmp_path / "noext", SRC)


def test_imwrite_bad_array_is_a_type_or_value_error(tmp_path):
    with pytest.raises((TypeError, ValueError)):
        libstb.imwrite(tmp_path / "a.png", SRC.astype(np.float32))
    with pytest.raises((TypeError, ValueError)):
        libstb.imwrite(tmp_path / "a.png", np.zeros((2, 2, 5), np.uint8))


def test_imread_options_and_bytes(tmp_path):
    data = libstb.Image(SRC).to_png()
    assert libstb.imread(data).shape == (4, 6, 3)
    assert libstb.imread(data, channels=4).shape == (4, 6, 4)
    np.testing.assert_array_equal(libstb.imread(data, flip=True), SRC[::-1])
    with pytest.raises(libstb.LimitError):
        libstb.imread(data, max_bytes=10)
    with pytest.raises(TypeError):
        libstb.imread(data, 4)  # options are keyword-only


def test_imread_missing_file_and_garbage():
    with pytest.raises(FileNotFoundError):
        libstb.imread("/definitely/not/here.png")
    with pytest.raises(libstb.DecodeError):
        libstb.imread(b"not an image")


def test_iminfo_is_header_only(tmp_path):
    data = libstb.Image(SRC).to_png()
    assert libstb.iminfo(data) == (6, 4, 3)
    assert libstb.iminfo(data).channels == 3


def test_old_names_are_gone():
    assert not hasattr(libstb, "load")
    assert not hasattr(libstb, "info")
    assert not hasattr(libstb.Image, "array")
