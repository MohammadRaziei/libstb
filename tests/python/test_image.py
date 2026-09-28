import struct
import threading
import zlib

import numpy as np
import pytest

import libstb


def png(w, h, rows, color_type=2):
    """Minimal 8-bit PNG from `rows` (list of bytes, one per scanline)."""
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)  # filter type 0 per row
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


# 2x2 RGB: [[red, green], [blue, white]]
RGB = png(2, 2, [bytes([255, 0, 0, 0, 255, 0]), bytes([0, 0, 255, 255, 255, 255])])
EXPECTED = np.array([[[255, 0, 0], [0, 255, 0]], [[0, 0, 255], [255, 255, 255]]], dtype=np.uint8)


def test_load_png_bytes():
    a = libstb.load(RGB)
    assert a.dtype == np.uint8
    assert a.shape == (2, 2, 3)
    assert a.flags["C_CONTIGUOUS"]
    np.testing.assert_array_equal(a, EXPECTED)


def test_info():
    i = libstb.info(RGB)
    assert i == (2, 2, 3)
    assert (i.width, i.height, i.channels) == (2, 2, 3)


def test_load_from_str_and_pathlib_paths(tmp_path):
    p = tmp_path / "x.png"
    p.write_bytes(RGB)
    np.testing.assert_array_equal(libstb.load(p), EXPECTED)
    np.testing.assert_array_equal(libstb.load(str(p)), EXPECTED)
    assert libstb.info(p) == (2, 2, 3)


def test_load_from_bytearray_and_memoryview():
    np.testing.assert_array_equal(libstb.load(bytearray(RGB)), EXPECTED)
    np.testing.assert_array_equal(libstb.load(memoryview(RGB)), EXPECTED)


def test_missing_file_raises_oserror(tmp_path):
    with pytest.raises(FileNotFoundError):
        libstb.load(tmp_path / "nope.png")


def test_channels_conversion():
    rgba = libstb.load(RGB, channels=4)
    assert rgba.shape == (2, 2, 4)
    assert (rgba[..., 3] == 255).all()
    np.testing.assert_array_equal(rgba[..., :3], EXPECTED)
    assert libstb.load(RGB, channels=1).shape == (2, 2, 1)


def test_flip():
    np.testing.assert_array_equal(libstb.load(RGB, flip=True), EXPECTED[::-1])


def test_result_owns_its_memory():
    a = libstb.load(RGB)
    import gc

    gc.collect()
    np.testing.assert_array_equal(a, EXPECTED)  # still valid, source long gone
    a[0, 0, 0] = 7  # writable, independent buffer
    np.testing.assert_array_equal(libstb.load(RGB), EXPECTED)


def test_garbage_raises_decode_error():
    with pytest.raises(libstb.DecodeError):
        libstb.load(b"definitely not an image")
    with pytest.raises(libstb.DecodeError):
        libstb.info(b"definitely not an image")


def test_empty_input_raises_decode_error():
    with pytest.raises(libstb.DecodeError, match="empty"):
        libstb.load(b"")


def test_truncated_png_raises_decode_error():
    with pytest.raises(libstb.DecodeError):
        libstb.load(RGB[: len(RGB) // 2])


@pytest.mark.parametrize("bad", [-1, 5])
def test_bad_channels_raises_value_error(bad):
    with pytest.raises(ValueError):
        libstb.load(RGB, channels=bad)


def test_max_bytes():
    with pytest.raises(libstb.LimitError, match="too large"):
        libstb.load(RGB, max_bytes=11)  # decodes to 12 bytes
    assert libstb.load(RGB, max_bytes=12).shape == (2, 2, 3)


def test_huge_declared_dimensions_rejected_fast():
    # Valid-looking PNG header that claims 60000x60000 (~10 GB), no pixel data.
    hdr = png(1, 1, [b"\x00\x00\x00"])
    hdr = hdr.replace(struct.pack(">II", 1, 1), struct.pack(">II", 60000, 60000), 1)
    # stb itself may refuse such a header (DecodeError) or our max_bytes guard
    # does (LimitError); either way nothing is allocated.
    with pytest.raises(libstb.Error):
        libstb.load(hdr)


def test_flip_is_thread_safe():
    errors = []

    def work(t):
        for i in range(500):
            flip = (i + t) % 2 == 1
            a = libstb.load(RGB, flip=flip)
            want = EXPECTED[::-1] if flip else EXPECTED
            if not np.array_equal(a, want):
                errors.append((t, i))
                return

    ts = [threading.Thread(target=work, args=(t,)) for t in range(8)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert not errors


def test_cmake_dir_layout():
    import os

    assert os.path.isfile(os.path.join(libstb.get_cmake_dir(), "libstbConfig.cmake"))
    assert os.path.isfile(os.path.join(libstb.get_include_dir(), "libstb.h"))
