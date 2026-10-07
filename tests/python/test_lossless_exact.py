"""Lossless formats (PNG, BMP, TGA) must survive bit for bit, so every comparison in this
file is EXACT (assert_array_equal). The lossy ones (JPEG, resize) are in
test_lossy_tolerance.py and use measured tolerances instead.

The PNG cases are picked to push stbi_zlib_compress (the patched deflate in
stb_image_write.h) through every path: stored blocks (noise), fixed and dynamic Huffman
blocks (small and structured images), long runs (flat), and many 64K-token blocks (the big
images). A decoder other than libstb's own (Pillow / zlib) must accept every stream too.
"""
import io

import numpy as np
import pytest
from PIL import Image as PILImage

from libstb import Image

KINDS = ("noise", "flat", "gradient", "blocks", "sparse", "photo")


def make(kind, h, w, c, seed=0):
    rng = np.random.default_rng(seed)
    if kind == "noise":
        return rng.integers(0, 256, (h, w, c), dtype=np.uint8)
    if kind == "flat":
        return np.full((h, w, c), 77, np.uint8)
    if kind == "gradient":
        y, x, k = np.mgrid[0:h, 0:w, 0:c]
        return ((x * 3 + y * 2 + k * 40) & 0xFF).astype(np.uint8)
    if kind == "blocks":  # a few flat colours in 16x16 tiles: screenshot-like
        pal = rng.integers(0, 256, (4, c), dtype=np.uint8)
        idx = rng.integers(0, 4, ((h + 15) // 16, (w + 15) // 16))
        return np.ascontiguousarray(np.repeat(np.repeat(pal[idx], 16, 0), 16, 1)[:h, :w])
    if kind == "sparse":  # mostly zeros with a few dots: long runs and long distances
        a = np.zeros((h, w, c), np.uint8)
        m = rng.random((h, w)) < 0.02
        a[m] = rng.integers(1, 256, (int(m.sum()), c))
        return a
    if kind == "photo":  # smooth colour fields plus sensor-like noise
        ys, xs, ks = np.linspace(0, 1, h)[:, None, None], np.linspace(0, 1, w)[None, :, None], np.arange(c)[None, None, :]
        a = 128 + 70 * np.sin(2 * np.pi * (1.5 * xs + 0.7 * ys) + ks) + rng.normal(0, 3, (h, w, c))
        return np.clip(a, 0, 255).astype(np.uint8)
    raise ValueError(kind)


def pil_array(data):
    a = np.asarray(PILImage.open(io.BytesIO(data)))
    return a[..., None] if a.ndim == 2 else a


def pil_bytes(arr, fmt, **kw):
    buf = io.BytesIO()
    PILImage.fromarray(arr[..., 0] if arr.shape[2] == 1 else arr).save(buf, fmt, **kw)
    return buf.getvalue()


# ------------------------------------------------------------------ PNG ----

@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("c", [1, 2, 3, 4])
@pytest.mark.parametrize("level", [1, 5, 9])
def test_png_roundtrip_is_exact(kind, c, level):
    src = make(kind, 90, 120, c)
    data = Image(src).to_png(level)
    np.testing.assert_array_equal(Image.open(data).numpy(), src)  # libstb's own decoder
    np.testing.assert_array_equal(pil_array(data), src)  # an independent one (zlib)


@pytest.mark.parametrize("kind", ["photo", "blocks", "sparse", "noise"])
def test_png_large_image_with_many_deflate_blocks_is_exact(kind):
    src = make(kind, 1100, 1300, 3)  # millions of tokens: far past one 64K-token block
    data = Image(src).to_png()
    np.testing.assert_array_equal(Image.open(data).numpy(), src)
    np.testing.assert_array_equal(pil_array(data), src)


@pytest.mark.parametrize("shape", [(1, 1, 1), (1, 2, 3), (2, 1, 4), (1, 5000, 3), (5000, 1, 3), (3, 3, 2)])
def test_png_degenerate_sizes_are_exact(shape):
    h, w, c = shape
    src = make("photo", h, w, c)
    np.testing.assert_array_equal(pil_array(Image(src).to_png()), src)
    np.testing.assert_array_equal(Image.open(Image(src).to_png()).numpy(), src)


@pytest.mark.parametrize("kind", ["flat", "gradient", "blocks", "sparse", "photo"])
def test_png_size_is_close_to_zlib(kind):
    """A size guard rather than an exactness check: stb's stock fixed-Huffman deflate was
    ~55% bigger than zlib, and that is the regression this pins down."""
    src = make(kind, 300, 400, 3)
    ref = len(pil_bytes(src, "PNG", compress_level=6))
    assert len(Image(src).to_png()) <= ref * 1.15 + 64


def test_png_of_incompressible_data_barely_grows():
    src = make("noise", 300, 400, 3)
    raw = src.size + src.shape[0]  # pixels plus one filter byte per row
    assert len(Image(src).to_png()) <= raw * 1.003 + 200  # stored blocks, not an expansion


# ------------------------------------------------------- Pillow -> libstb ----

@pytest.mark.parametrize("fmt,c", [("PNG", 1), ("PNG", 2), ("PNG", 3), ("PNG", 4),
                                    ("TGA", 1), ("TGA", 3), ("TGA", 4), ("BMP", 3)])
def test_files_written_by_pillow_decode_exactly(fmt, c):
    src = make("photo", 47, 59, c)
    data = pil_bytes(src, fmt)
    np.testing.assert_array_equal(Image.open(data).numpy(), pil_array(data))
    np.testing.assert_array_equal(Image.open(data).numpy(), src)


# ------------------------------------------------------- libstb -> Pillow ----

@pytest.mark.parametrize("c", [1, 3, 4])
@pytest.mark.parametrize("rle", [False, True])
def test_tga_written_by_libstb_is_exact(c, rle):
    src = make("blocks", 40, 50, c)
    data = Image(src).to_tga(rle)
    np.testing.assert_array_equal(Image.open(data).numpy(), src)
    np.testing.assert_array_equal(pil_array(data), src)


def test_bmp_rgb_is_exact():
    src = make("photo", 33, 45, 3)
    data = Image(src).to_bmp()
    np.testing.assert_array_equal(Image.open(data).numpy(), src)
    np.testing.assert_array_equal(pil_array(data), src)


def test_bmp_rgba_keeps_alpha_exactly():
    src = make("photo", 33, 45, 4)
    data = Image(src).to_bmp()
    np.testing.assert_array_equal(Image.open(data).numpy(), src)
    np.testing.assert_array_equal(pil_array(data), src)  # Pillow reads libstb's 32-bit BMP with alpha too


def test_bmp_gray_is_stored_as_rgb_with_three_equal_planes():
    """BMP has no gray pixel format here: libstb writes 24-bit RGB, so the decoded array is
    (h, w, 3) and each plane must equal the source exactly."""
    src = make("photo", 33, 45, 1)
    data = Image(src).to_bmp()
    for decoded in (Image.open(data).numpy(), pil_array(data)):
        assert decoded.shape == (33, 45, 3)
        for plane in range(3):
            np.testing.assert_array_equal(decoded[..., plane], src[..., 0])
