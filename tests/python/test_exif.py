"""EXIF orientation: reading it, exif_orientation(), and decoding with orient=True."""

import io

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped
PIL = pytest.importorskip("PIL")  # Pillow is only needed to compare against: skip the module without it
pytest.importorskip("PIL.Image")
pytest.importorskip("PIL.ImageOps")

import libstb as stb

ARR = np.random.default_rng(7).integers(0, 256, (6, 9, 3), dtype=np.uint8)


def encode(fmt, orientation=None, arr=ARR):
    exif = PIL.Image.Exif()
    if orientation is not None:
        exif[0x0112] = orientation
    buf = io.BytesIO()
    PIL.Image.fromarray(arr).save(buf, fmt, exif=exif.tobytes() if orientation is not None else b"")
    return buf.getvalue()


@pytest.mark.parametrize("fmt", ["JPEG", "PNG"])
@pytest.mark.parametrize("orientation", range(1, 9))
def test_exif_orientation_reads_what_pillow_wrote(fmt, orientation):
    assert stb.exif_orientation(encode(fmt, orientation)) == orientation


@pytest.mark.parametrize("fmt", ["JPEG", "PNG", "BMP"])
def test_no_exif_means_upright(fmt):
    buf = io.BytesIO()
    PIL.Image.fromarray(ARR).save(buf, fmt)
    assert stb.exif_orientation(buf.getvalue()) == 1


@pytest.mark.parametrize("junk", [b"", b"not an image", b"\xff\xd8", b"\x89PNG\r\n\x1a\n", bytes(100)])
def test_exif_orientation_of_junk_is_one(junk):
    assert stb.exif_orientation(junk) == 1


def test_exif_orientation_accepts_paths_and_every_bytes_like(tmp_path):
    data = encode("PNG", 6)
    p = tmp_path / "a.png"
    p.write_bytes(data)
    for src in (data, bytearray(data), memoryview(data), str(p), p):
        assert stb.exif_orientation(src) == 6
    with pytest.raises(FileNotFoundError):
        stb.exif_orientation(tmp_path / "missing.png")


def test_exif_orientation_survives_truncation_and_corruption():
    for fmt in ("JPEG", "PNG"):
        data = encode(fmt, 6)
        for n in range(len(data)):
            assert 1 <= stb.exif_orientation(data[:n]) <= 8
        rng = np.random.default_rng(1)
        for _ in range(1500):
            m = bytearray(data[:300])
            for _ in range(4):
                m[rng.integers(0, len(m))] = rng.integers(0, 256)
            assert 1 <= stb.exif_orientation(bytes(m)) <= 8


@pytest.mark.parametrize("orientation", range(1, 9))
def test_png_orient_matches_pillows_exif_transpose_exactly(orientation):
    data = encode("PNG", orientation)
    want = np.asarray(PIL.ImageOps.exif_transpose(PIL.Image.open(io.BytesIO(data))))
    np.testing.assert_array_equal(stb.imread(data, orient=True), want)
    np.testing.assert_array_equal(stb.Image.open(data, orient=True).numpy(), want)


@pytest.mark.parametrize("orientation", range(1, 9))
def test_jpeg_orient_is_the_decoded_image_oriented(orientation):
    data = encode("JPEG", orientation)
    plain = stb.Image.open(data)
    got = stb.Image.open(data, orient=True)
    np.testing.assert_array_equal(got.numpy(), plain.orient(orientation).numpy())
    assert (got.width, got.height) == ((plain.height, plain.width) if orientation >= 5 else (plain.width, plain.height))


def test_orient_is_off_by_default_and_a_noop_without_exif(tmp_path):
    data = encode("PNG", 6)
    np.testing.assert_array_equal(stb.imread(data), ARR)
    np.testing.assert_array_equal(stb.Image.open(data, orient=False).numpy(), ARR)
    plain = encode("PNG")
    np.testing.assert_array_equal(stb.imread(plain, orient=True), ARR)
    assert stb.iminfo(data).width == 9  # the header reports the stored size


def test_orient_from_a_path(tmp_path):
    p = tmp_path / "photo.png"
    p.write_bytes(encode("PNG", 8))
    np.testing.assert_array_equal(stb.imread(p, orient=True), stb.Image.open(p).orient(8).numpy())
    np.testing.assert_array_equal(stb.imread(str(p), orient=True), stb.imread(p, orient=True))


def test_orient_with_flip_flips_after_orienting():
    data = encode("PNG", 6)
    oriented = stb.imread(data, orient=True)
    np.testing.assert_array_equal(stb.imread(data, orient=True, flip=True), oriented[::-1])
    upright = encode("PNG", 1)
    np.testing.assert_array_equal(stb.imread(upright, orient=True, flip=True), ARR[::-1])


def test_orient_with_channels_and_limits():
    data = encode("PNG", 6)
    out = stb.Image.open(data, orient=True, format=stb.PixelFormat.RGBA)
    assert out.channels == 4 and out.width == 6
    with pytest.raises(stb.LimitError):
        stb.Image.open(data, orient=True, max_bytes=10)
