"""EXIF orientation: reading it, exif_orientation(), and decoding with orient=True."""

import io

import numpy as np
import pytest
from PIL import Image as PILImage
from PIL import ImageOps

import libstb

ARR = np.random.default_rng(7).integers(0, 256, (6, 9, 3), dtype=np.uint8)


def encode(fmt, orientation=None, arr=ARR):
    exif = PILImage.Exif()
    if orientation is not None:
        exif[0x0112] = orientation
    buf = io.BytesIO()
    PILImage.fromarray(arr).save(buf, fmt, exif=exif.tobytes() if orientation is not None else b"")
    return buf.getvalue()


@pytest.mark.parametrize("fmt", ["JPEG", "PNG"])
@pytest.mark.parametrize("orientation", range(1, 9))
def test_exif_orientation_reads_what_pillow_wrote(fmt, orientation):
    assert libstb.exif_orientation(encode(fmt, orientation)) == orientation


@pytest.mark.parametrize("fmt", ["JPEG", "PNG", "BMP"])
def test_no_exif_means_upright(fmt):
    buf = io.BytesIO()
    PILImage.fromarray(ARR).save(buf, fmt)
    assert libstb.exif_orientation(buf.getvalue()) == 1


@pytest.mark.parametrize("junk", [b"", b"not an image", b"\xff\xd8", b"\x89PNG\r\n\x1a\n", bytes(100)])
def test_exif_orientation_of_junk_is_one(junk):
    assert libstb.exif_orientation(junk) == 1


def test_exif_orientation_accepts_paths_and_every_bytes_like(tmp_path):
    data = encode("PNG", 6)
    p = tmp_path / "a.png"
    p.write_bytes(data)
    for src in (data, bytearray(data), memoryview(data), str(p), p):
        assert libstb.exif_orientation(src) == 6
    with pytest.raises(FileNotFoundError):
        libstb.exif_orientation(tmp_path / "missing.png")


def test_exif_orientation_survives_truncation_and_corruption():
    for fmt in ("JPEG", "PNG"):
        data = encode(fmt, 6)
        for n in range(len(data)):
            assert 1 <= libstb.exif_orientation(data[:n]) <= 8
        rng = np.random.default_rng(1)
        for _ in range(1500):
            m = bytearray(data[:300])
            for _ in range(4):
                m[rng.integers(0, len(m))] = rng.integers(0, 256)
            assert 1 <= libstb.exif_orientation(bytes(m)) <= 8


@pytest.mark.parametrize("orientation", range(1, 9))
def test_png_orient_matches_pillows_exif_transpose_exactly(orientation):
    data = encode("PNG", orientation)
    want = np.asarray(ImageOps.exif_transpose(PILImage.open(io.BytesIO(data))))
    np.testing.assert_array_equal(libstb.imread(data, orient=True), want)
    np.testing.assert_array_equal(libstb.Image.open(data, orient=True).numpy(), want)


@pytest.mark.parametrize("orientation", range(1, 9))
def test_jpeg_orient_is_the_decoded_image_oriented(orientation):
    data = encode("JPEG", orientation)
    plain = libstb.Image.open(data)
    got = libstb.Image.open(data, orient=True)
    np.testing.assert_array_equal(got.numpy(), plain.orient(orientation).numpy())
    assert (got.width, got.height) == ((plain.height, plain.width) if orientation >= 5 else (plain.width, plain.height))


def test_orient_is_off_by_default_and_a_noop_without_exif(tmp_path):
    data = encode("PNG", 6)
    np.testing.assert_array_equal(libstb.imread(data), ARR)
    np.testing.assert_array_equal(libstb.Image.open(data, orient=False).numpy(), ARR)
    plain = encode("PNG")
    np.testing.assert_array_equal(libstb.imread(plain, orient=True), ARR)
    assert libstb.iminfo(data).width == 9  # the header reports the stored size


def test_orient_from_a_path(tmp_path):
    p = tmp_path / "photo.png"
    p.write_bytes(encode("PNG", 8))
    np.testing.assert_array_equal(libstb.imread(p, orient=True), libstb.Image.open(p).orient(8).numpy())
    np.testing.assert_array_equal(libstb.imread(str(p), orient=True), libstb.imread(p, orient=True))


def test_orient_with_flip_flips_after_orienting():
    data = encode("PNG", 6)
    oriented = libstb.imread(data, orient=True)
    np.testing.assert_array_equal(libstb.imread(data, orient=True, flip=True), oriented[::-1])
    upright = encode("PNG", 1)
    np.testing.assert_array_equal(libstb.imread(upright, orient=True, flip=True), ARR[::-1])


def test_orient_with_channels_and_limits():
    data = encode("PNG", 6)
    out = libstb.Image.open(data, orient=True, channels=4)
    assert out.channels == 4 and out.width == 6
    with pytest.raises(libstb.LimitError):
        libstb.Image.open(data, orient=True, max_bytes=10)
