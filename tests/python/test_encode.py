import threading

import numpy as np
import pytest

import libstb
from libstb import Image


def gradient(w, h, c):
    y, x, k = np.mgrid[0:h, 0:w, 0:c]
    return ((x * 31 + y * 17 + k * 53) & 0xFF).astype(np.uint8)


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_png_roundtrip_is_lossless(c):
    src = Image(gradient(13, 7, c))
    data = src.to_png()
    assert isinstance(data, bytes) and data[:4] == b"\x89PNG"
    np.testing.assert_array_equal(Image.open(data).array, src.array)


def test_png_compression_levels_all_roundtrip():
    src = Image(gradient(32, 32, 3))
    for level in range(1, 10):
        np.testing.assert_array_equal(Image.open(src.to_png(level)).array, src.array)
        np.testing.assert_array_equal(Image.open(src.to_png(compression=level)).array, src.array)


def test_bmp_and_tga_roundtrip():
    src = Image(gradient(9, 5, 3))
    data = src.to_bmp()
    assert data[:2] == b"BM"
    np.testing.assert_array_equal(Image.open(data).array, src.array)
    for rle in (True, False):
        np.testing.assert_array_equal(Image.open(src.to_tga(rle)).array, src.array)
        np.testing.assert_array_equal(Image.open(src.to_tga(rle=rle)).array, src.array)


def test_tga_rle_shrinks_flat_images():
    flat = Image(np.full((64, 64, 3), 200, np.uint8))
    assert len(flat.to_tga(True)) < len(flat.to_tga(False))


def test_jpg_roundtrip_is_close_and_quality_matters():
    flat = Image(np.full((16, 16, 3), 128, np.uint8))
    data = flat.to_jpg(95)
    assert data[:2] == b"\xff\xd8"
    out = Image.open(data).array
    assert out.shape == (16, 16, 3)
    assert np.abs(out.astype(int) - 128).max() <= 4
    busy = Image(gradient(64, 64, 3))
    assert len(busy.to_jpg(10)) < len(busy.to_jpg(quality=95))


def test_defaults_are_the_documented_ones():
    from libstb import libstb_py

    assert libstb_py.DEFAULT_PNG_COMPRESSION == 8 and libstb_py.DEFAULT_JPG_QUALITY == 90
    src = Image(gradient(20, 20, 3))
    assert src.to_png() == src.to_png(8)
    assert src.to_jpg() == src.to_jpg(90)
    assert src.to_tga() == src.to_tga(True)


@pytest.mark.parametrize("call", [lambda i: i.to_png(0), lambda i: i.to_png(10),
                                  lambda i: i.to_jpg(0), lambda i: i.to_jpg(101)])
def test_out_of_range_settings_raise_value_error(call):
    with pytest.raises(ValueError):
        call(Image(gradient(4, 4, 3)))


def test_write_matches_to_and_ignores_the_extension(tmp_path):
    src = Image(gradient(6, 6, 3))
    p = tmp_path / "x.dat"
    src.write_bmp(p)
    assert p.read_bytes() == src.to_bmp()
    src.write_png(p, 9)
    assert p.read_bytes() == src.to_png(9)
    src.write_tga(str(p), rle=False)
    assert p.read_bytes() == src.to_tga(False)
    src.write_jpg(p, quality=70)
    assert p.read_bytes() == src.to_jpg(70)


def test_write_with_a_bad_setting_writes_nothing(tmp_path):
    p = tmp_path / "x.jpg"
    with pytest.raises(ValueError):
        Image(gradient(4, 4, 3)).write_jpg(p, 0)
    assert not p.exists()


def test_save_picks_the_format_from_the_extension(tmp_path):
    src = Image(gradient(10, 10, 4))
    p = tmp_path / "x.png"
    src.save(p)
    np.testing.assert_array_equal(Image.open(p).array, src.array)
    for name, magic in [("a.PNG", b"\x89PNG"), ("a.bmp", b"BM"), ("a.Tga", None),
                        ("a.jpg", b"\xff\xd8"), ("a.JPEG", b"\xff\xd8")]:
        q = tmp_path / name
        src.save(str(q))
        assert q.read_bytes()[: len(magic or b"")] == (magic or b"")
    assert (tmp_path / "a.Tga").read_bytes() == src.to_tga()


def test_save_unknown_extension_writes_nothing(tmp_path):
    p = tmp_path / "x.gif"
    with pytest.raises(ValueError, match="unsupported"):
        Image(gradient(4, 4, 3)).save(p)
    assert not p.exists()
    with pytest.raises(ValueError):
        Image(gradient(4, 4, 3)).save(tmp_path / "noext")


def test_save_to_missing_directory_raises_oserror(tmp_path):
    with pytest.raises(FileNotFoundError):
        Image(gradient(4, 4, 3)).save(tmp_path / "nope" / "x.png")
    with pytest.raises(FileNotFoundError):
        Image(gradient(4, 4, 3)).write_png(tmp_path / "nope" / "x.png")


def test_the_encoder_classes_are_gone():
    for name in ("Encoder", "PngEncoder", "JpegEncoder", "BmpEncoder", "TgaEncoder"):
        assert not hasattr(libstb, name)


def test_encoding_accepts_non_contiguous_arrays():
    arr = gradient(8, 6, 3)
    flipped = Image(arr[::-1])  # negative strides
    np.testing.assert_array_equal(Image.open(flipped.to_png()).array, arr[::-1])


def test_concurrent_encoding_with_different_settings():
    src = Image(gradient(24, 24, 3))
    errors = []

    def work(t):
        for _ in range(100):
            for data in (src.to_png(1 if t % 2 else 9), src.to_tga(t % 2 == 0)):
                if not np.array_equal(Image.open(data).array, src.array):
                    errors.append(t)
                    return

    ts = [threading.Thread(target=work, args=(t,)) for t in range(8)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert not errors
