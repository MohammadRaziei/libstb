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
    data = src.encode(libstb.PngEncoder())
    assert data[:4] == b"\x89PNG"
    np.testing.assert_array_equal(Image.open(data).array, src.array)


def test_bmp_and_tga_roundtrip():
    src = Image(gradient(9, 5, 3))
    np.testing.assert_array_equal(Image.open(src.encode(libstb.BmpEncoder())).array, src.array)
    for rle in (True, False):
        np.testing.assert_array_equal(Image.open(src.encode(libstb.TgaEncoder(rle))).array, src.array)


def test_jpeg_roundtrip_is_close_and_quality_matters():
    flat = Image(np.full((16, 16, 3), 128, np.uint8))
    out = Image.open(flat.encode(libstb.JpegEncoder(95))).array
    assert out.shape == (16, 16, 3)
    assert np.abs(out.astype(int) - 128).max() <= 4
    busy = Image(gradient(64, 64, 3))
    assert len(busy.encode(libstb.JpegEncoder(10))) < len(busy.encode(libstb.JpegEncoder(95)))


def test_encoder_classes_and_properties():
    assert isinstance(libstb.PngEncoder(), libstb.Encoder)
    assert libstb.PngEncoder().compression == 8 and libstb.PngEncoder(3).compression == 3
    assert libstb.JpegEncoder().quality == 90
    assert libstb.TgaEncoder().rle is True and libstb.TgaEncoder(False).rle is False
    assert [e.extension for e in (libstb.PngEncoder(), libstb.JpegEncoder(), libstb.BmpEncoder(), libstb.TgaEncoder())] == [
        "png", "jpg", "bmp", "tga"]


def test_encoder_base_is_abstract():
    with pytest.raises(TypeError):
        libstb.Encoder()


@pytest.mark.parametrize("make", [lambda: libstb.PngEncoder(0), lambda: libstb.PngEncoder(10),
                                  lambda: libstb.JpegEncoder(0), lambda: libstb.JpegEncoder(101)])
def test_invalid_encoder_parameters_raise_value_error(make):
    with pytest.raises(ValueError):
        make()


def test_for_path_returns_the_right_subclass():
    assert type(libstb.Encoder.for_path("a.png")) is libstb.PngEncoder
    assert type(libstb.Encoder.for_path("A.JPEG")) is libstb.JpegEncoder
    assert type(libstb.Encoder.for_path("a.bmp")) is libstb.BmpEncoder
    assert type(libstb.Encoder.for_path("a.tga")) is libstb.TgaEncoder
    with pytest.raises(ValueError, match="unsupported"):
        libstb.Encoder.for_path("a.gif")


def test_save_by_extension_and_with_explicit_encoder(tmp_path):
    src = Image(gradient(10, 10, 4))
    p = tmp_path / "x.png"
    src.save(p)
    np.testing.assert_array_equal(Image.open(p).array, src.array)
    q = tmp_path / "x.dat"  # explicit encoder ignores the extension
    src.save(q, libstb.BmpEncoder())
    assert libstb.info(q).channels == 3 or libstb.info(q).channels == 4
    assert q.read_bytes()[:2] == b"BM"
    src.save(str(tmp_path / "y.tga"))


def test_save_unknown_extension_writes_nothing(tmp_path):
    p = tmp_path / "x.gif"
    with pytest.raises(ValueError):
        Image(gradient(4, 4, 3)).save(p)
    assert not p.exists()


def test_save_to_missing_directory_raises_oserror(tmp_path):
    with pytest.raises(FileNotFoundError):
        Image(gradient(4, 4, 3)).save(tmp_path / "nope" / "x.png")


def test_encode_accepts_non_contiguous_arrays():
    arr = gradient(8, 6, 3)
    flipped = Image(arr[::-1])  # negative strides
    data = flipped.encode(libstb.PngEncoder())
    np.testing.assert_array_equal(Image.open(data).array, arr[::-1])


def test_encode_rejects_non_encoder():
    with pytest.raises(TypeError):
        Image(gradient(2, 2, 3)).encode("png")


def test_concurrent_encoding_with_different_settings():
    src = Image(gradient(24, 24, 3))
    errors = []

    def work(t):
        png, tga = libstb.PngEncoder(1 if t % 2 else 9), libstb.TgaEncoder(t % 2 == 0)
        for _ in range(100):
            for enc in (png, tga):
                if not np.array_equal(Image.open(src.encode(enc)).array, src.array):
                    errors.append(t)
                    return

    ts = [threading.Thread(target=work, args=(t,)) for t in range(8)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert not errors
