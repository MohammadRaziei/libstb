"""Loader (stb::loader), Image.open (a path) and Image.decode (encoded bytes)."""

import gc
import io
import pathlib

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb

ARR = np.random.default_rng(11).integers(0, 256, (6, 9, 3), dtype=np.uint8)


def png(arr=ARR):
    return stb.Image(arr.copy()).to_png()


def png_with_orientation(orientation, arr=ARR):
    """A PNG whose eXIf chunk says `orientation`, made with Pillow."""
    PIL = pytest.importorskip("PIL")
    pytest.importorskip("PIL.Image")
    exif = PIL.Image.Exif()
    exif[0x0112] = orientation
    buf = io.BytesIO()
    PIL.Image.fromarray(arr).save(buf, "PNG", exif=exif.tobytes())
    return buf.getvalue()


def test_loader_is_exported():
    assert "Loader" in stb.__all__
    assert stb.Loader is not None


@pytest.mark.parametrize("make", [bytes, bytearray, memoryview, lambda b: np.frombuffer(b, np.uint8)])
def test_loader_from_any_bytes_like(make):
    data = png()
    img = stb.Loader(make(data)).run()
    np.testing.assert_array_equal(img.numpy(), ARR)


def test_loader_from_a_path(tmp_path):
    p = tmp_path / "a.png"
    p.write_bytes(png())
    for source in (p, str(p)):
        np.testing.assert_array_equal(stb.Loader(source).run().numpy(), ARR)


def test_open_is_a_path_and_decode_is_bytes(tmp_path):
    p = tmp_path / "a.png"
    p.write_bytes(png())
    np.testing.assert_array_equal(stb.Image.open(p).numpy(), ARR)
    np.testing.assert_array_equal(stb.Image.open(str(p)).numpy(), ARR)
    for make in (bytes, bytearray, memoryview):
        np.testing.assert_array_equal(stb.Image.decode(make(p.read_bytes())).numpy(), ARR)


@pytest.mark.parametrize("make", [bytes, bytearray, memoryview])
def test_open_with_bytes_says_to_use_decode(make):
    with pytest.raises(TypeError, match="Image.decode"):
        stb.Image.open(make(png()))


@pytest.mark.parametrize("bad", [123, None, 1.5, [1, 2]])
def test_open_and_loader_reject_what_is_neither_a_path_nor_bytes(bad):
    with pytest.raises(TypeError):
        stb.Image.open(bad)
    with pytest.raises(TypeError):
        stb.Loader(bad)


def test_decode_rejects_a_path():
    with pytest.raises(TypeError):
        stb.Image.decode("photo.png")
    with pytest.raises(TypeError):
        stb.Image.decode(pathlib.Path("photo.png"))


def test_settings_chain_and_return_the_loader_itself():
    ld = stb.Loader(png())
    assert ld.format("rgba") is ld
    assert ld.flip() is ld
    assert ld.exif_orient() is ld
    assert ld.max_bytes(1 << 20) is ld


def test_settings_match_the_keywords_of_open_and_decode(tmp_path):
    data = png_with_orientation(6)
    p = tmp_path / "a.png"
    p.write_bytes(data)
    want = stb.Image.decode(data, format="rgba", flip=True, exif_orient=True)
    got = stb.Loader(data).format(stb.PixelFormat.RGBA).flip().exif_orient().run()
    np.testing.assert_array_equal(got.numpy(), want.numpy())
    np.testing.assert_array_equal(stb.Loader(p).format("rgba").flip().exif_orient().run().numpy(), want.numpy())
    np.testing.assert_array_equal(stb.Image.open(p, format="rgba", flip=True, exif_orient=True).numpy(), want.numpy())


def test_exif_orient_is_off_by_default_and_the_switch_can_be_turned_off():
    data = png_with_orientation(6)
    raw = stb.Loader(data).run()
    assert (raw.width, raw.height) == (9, 6)  # the stored pixels
    up = stb.Loader(data).exif_orient().run()
    assert (up.width, up.height) == (6, 9)
    np.testing.assert_array_equal(up.numpy(), raw.orient(6).numpy())
    again = stb.Loader(data).exif_orient().exif_orient(False).run()
    np.testing.assert_array_equal(again.numpy(), raw.numpy())


def test_flip_alone_and_run_can_be_repeated():
    ld = stb.Loader(png()).flip()
    a, b = ld.run(), ld.run()
    np.testing.assert_array_equal(a.numpy(), ARR[::-1])
    np.testing.assert_array_equal(a.numpy(), b.numpy())
    assert a is not b


def test_max_bytes_is_checked_before_decoding():
    with pytest.raises(stb.LimitError):
        stb.Loader(png()).max_bytes(10).run()
    stb.Loader(png()).max_bytes(ARR.nbytes).run()  # exactly enough


def test_bad_format_and_bad_data_raise_the_usual_errors():
    with pytest.raises(ValueError):
        stb.Loader(png()).format("cmyk")
    with pytest.raises(TypeError):
        stb.Loader(png()).format(3.5)
    with pytest.raises(stb.DecodeError):
        stb.Loader(b"not an image").run()
    with pytest.raises(OSError):
        stb.Loader("no/such/file.png").run()
    with pytest.raises(OSError):
        stb.Image.open("no/such/file.png")


def test_a_loader_from_a_bytearray_keeps_it_from_being_resized():
    data = bytearray(png())
    ld = stb.Loader(data)
    with pytest.raises(BufferError):
        data.append(0)  # the loader points at these bytes: they cannot move
    np.testing.assert_array_equal(ld.run().numpy(), ARR)
    del ld
    gc.collect()
    data.append(0)  # released with the loader


def test_a_loader_keeps_its_bytes_alive():
    ld = stb.Loader(bytes(png()))  # the only reference to the bytes is the loader's
    gc.collect()
    np.testing.assert_array_equal(ld.run().numpy(), ARR)


def test_imread_takes_a_path_or_bytes_and_the_new_keyword(tmp_path):
    data = png_with_orientation(8)
    p = tmp_path / "a.png"
    p.write_bytes(data)
    want = stb.Image.decode(data, exif_orient=True).numpy()
    np.testing.assert_array_equal(stb.imread(data, exif_orient=True), want)
    np.testing.assert_array_equal(stb.imread(p, exif_orient=True), want)
    np.testing.assert_array_equal(stb.imread(bytearray(data), exif_orient=True), want)
    with pytest.raises(TypeError):
        stb.imread(data, orient=True)  # the old name is gone


def test_the_old_keyword_is_gone():
    with pytest.raises(TypeError):
        stb.Image.decode(png(), orient=True)
    with pytest.raises(TypeError):
        stb.Image.open("x.png", orient=True)
    assert not hasattr(stb.Loader, "orient")
