"""libstb against Pillow, the reference everyone already trusts.

Pillow is a test-only dependency (`pip install "libstb[test]"`). Every test
here feeds the same data to both libraries and compares the results:

  * lossless formats (PNG, BMP, TGA) must decode to the IDENTICAL pixels, and
    whatever libstb encodes must decode back to the original, in Pillow;
  * lossy steps (JPEG decode, JPEG encode, resampling) are compared by RMSE.
    Two correct implementations still round differently (IDCT, chroma
    upsampling, 8-bit fixed point), so zero is not the bar: the bounds below
    are about 2 to 3 times what was measured when they were written
    (JPEG decode ~0.4, resize ~0.5), tight enough that a wrong filter, an
    off-by-half-pixel or a swapped channel fails them by a wide margin.

Images are non-square on purpose, so a swapped width and height cannot hide.
"""
import io

import numpy as np
import pytest
from PIL import Image as PILImage

import libstb
from libstb import Image, Resizer

MODES = {1: "L", 2: "LA", 3: "RGB", 4: "RGBA"}


def rmse(a, b):
    assert a.shape == b.shape, f"shape {a.shape} != {b.shape}"
    d = a.astype(np.float64) - b.astype(np.float64)
    return float(np.sqrt(np.mean(d * d)))


def _smooth(rng, h, w, c):
    """A photo-like image: smooth colour fields, soft noise, a few hard edges."""
    out = np.empty((h, w, c), np.float64)
    ys, xs = np.linspace(0, 1, h)[:, None], np.linspace(0, 1, w)[None, :]
    for k in range(c):
        p = rng.uniform(0, 6.28, 3)
        out[..., k] = (128 + 70 * np.sin(2 * np.pi * (1.5 * xs + 0.7 * ys) + p[0])
                       + 40 * np.sin(2 * np.pi * (4 * xs - 3 * ys) + p[1])
                       + 15 * np.sin(2 * np.pi * (11 * xs + 9 * ys) + p[2]))
    out += rng.normal(0, 2.0, out.shape)
    out[h // 4: h // 2, w // 5: w // 2] = rng.uniform(0, 255, c)  # one hard-edged block
    return np.clip(out, 0, 255).astype(np.uint8)


@pytest.fixture(scope="module")
def rng_images():
    rng = np.random.default_rng(1234)
    return {(c, w, h): _smooth(rng, h, w, c) for c in (1, 2, 3, 4) for (w, h) in ((131, 97), (64, 48))}


def photo(c=3, w=131, h=97):
    return _smooth(np.random.default_rng(c * 1000 + w), h, w, c)


def pil_bytes(arr, fmt, **kw):
    im = PILImage.fromarray(arr[..., 0] if arr.shape[2] == 1 else arr, MODES[arr.shape[2]])
    buf = io.BytesIO()
    im.save(buf, fmt, **kw)
    return buf.getvalue()


def pil_array(data):
    a = np.asarray(PILImage.open(io.BytesIO(data)))
    return a[..., None] if a.ndim == 2 else a


# ------------------------------------------------------------------ decode --

@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_png_decode_is_pixel_exact(c):
    src = photo(c)
    data = pil_bytes(src, "PNG")
    np.testing.assert_array_equal(Image.open(data).array, pil_array(data))
    np.testing.assert_array_equal(Image.open(data).array, src)


@pytest.mark.parametrize("c,fmt", [(3, "BMP"), (3, "TGA"), (4, "TGA"), (1, "TGA")])
def test_bmp_and_tga_decode_is_pixel_exact(c, fmt):
    src = photo(c)
    data = pil_bytes(src, fmt)
    np.testing.assert_array_equal(Image.open(data).array, pil_array(data))


@pytest.mark.parametrize("c", [3, 4])
def test_rle_tga_decode_is_pixel_exact(c):
    src = photo(c)
    data = pil_bytes(src, "TGA", compression="tga_rle")
    np.testing.assert_array_equal(Image.open(data).array, src)


def test_palette_and_one_bit_pngs_expand_like_pillow():
    rgb = photo(3)
    pal = PILImage.fromarray(rgb).quantize(64)
    buf = io.BytesIO()
    pal.save(buf, "PNG")
    np.testing.assert_array_equal(Image.open(buf.getvalue()).array, np.asarray(pal.convert("RGB")))

    with_trns = pal.copy()
    with_trns.info["transparency"] = 3                  # palette entry 3 is fully transparent
    buf = io.BytesIO()
    with_trns.save(buf, "PNG", transparency=3)
    mine = Image.open(buf.getvalue()).array
    np.testing.assert_array_equal(mine, np.asarray(with_trns.convert("RGBA")))
    assert (mine[..., 3] == 0).any()

    bw = PILImage.fromarray(photo(1)[..., 0]).convert("1")
    buf = io.BytesIO()
    bw.save(buf, "PNG")
    np.testing.assert_array_equal(Image.open(buf.getvalue()).array[..., 0],
                                  np.asarray(bw).astype(np.uint8) * 255)


@pytest.mark.parametrize("subsampling", [0, 2], ids=["444", "420"])
@pytest.mark.parametrize("quality", [50, 75, 90, 100])
def test_jpeg_decode_matches_pillow(quality, subsampling):
    data = pil_bytes(photo(3), "JPEG", quality=quality, subsampling=subsampling)
    mine, ref = Image.open(data).array, pil_array(data)
    assert rmse(mine, ref) < 1.0
    assert np.abs(mine.astype(int) - ref.astype(int)).max() <= 8


def test_grayscale_jpeg_decode_matches_pillow():
    data = pil_bytes(photo(1), "JPEG", quality=90)
    mine = Image.open(data)
    assert mine.channels == 1
    assert rmse(mine.array, pil_array(data)) < 0.5


@pytest.mark.parametrize("fmt,c", [("PNG", 4), ("PNG", 1), ("BMP", 3), ("TGA", 4), ("JPEG", 3)])
def test_info_and_load_agree_with_pillow(fmt, c, tmp_path):
    src = photo(c, 83, 61)
    data = pil_bytes(src, fmt)
    ref = PILImage.open(io.BytesIO(data))
    width, height, channels = libstb.info(data)
    assert (width, height) == ref.size
    assert channels == len(ref.getbands())

    path = tmp_path / f"x.{fmt.lower()}"
    path.write_bytes(data)
    np.testing.assert_array_equal(libstb.load(path), Image.open(path).array)
    np.testing.assert_array_equal(libstb.load(path), Image.open(data).array)


# ------------------------------------------------------------------ encode --

@pytest.mark.parametrize("c", [1, 2, 3, 4])
@pytest.mark.parametrize("level", [1, 6, 9])
def test_png_written_by_libstb_decodes_exactly_in_pillow(c, level):
    src = photo(c)
    out = pil_array(Image(src).to_png(compression=level))
    np.testing.assert_array_equal(out, src)


@pytest.mark.parametrize("c,fmt", [(3, "bmp"), (3, "tga"), (4, "tga"), (1, "tga")])
def test_bmp_and_tga_written_by_libstb_decode_exactly_in_pillow(c, fmt):
    src = photo(c)
    img = Image(src)
    data = img.to_bmp() if fmt == "bmp" else img.to_tga()
    np.testing.assert_array_equal(pil_array(data), src)
    if fmt == "tga":
        np.testing.assert_array_equal(pil_array(img.to_tga(rle=False)), src)


@pytest.mark.parametrize("quality", [50, 75, 90])
def test_jpeg_quality_matches_pillows_at_the_same_setting(quality):
    # Both are baseline encoders at the same quality number, so each should lose about as much
    # as the other against the original, and decode to nearly the same picture.
    src = photo(3, 160, 120)
    mine = pil_array(Image(src).to_jpg(quality=quality))
    theirs = pil_array(pil_bytes(src, "JPEG", quality=quality))
    assert abs(rmse(mine, src) - rmse(theirs, src)) < 0.3
    assert rmse(mine, theirs) < 2.5


def test_jpeg_above_90_is_never_worse_than_pillow():
    # stb switches chroma subsampling off above quality 90, Pillow does not, so libstb keeps more.
    src = photo(3, 160, 120)
    mine = pil_array(Image(src).to_jpg(quality=95))
    theirs = pil_array(pil_bytes(src, "JPEG", quality=95))
    assert rmse(mine, src) <= rmse(theirs, src) + 0.1


def test_jpeg_grayscale_encode_matches_pillows_loss():
    # stb's JPEG writer always writes 3 components, even for 1-channel input (Pillow writes a true
    # grayscale file), so the result decodes to RGB; it must still be grey and lose as little.
    src = photo(1)
    data = Image(src).to_jpg(quality=90)
    assert libstb.info(data).channels == 3
    mine = pil_array(data)
    assert np.abs(mine.astype(int) - mine[..., :1].astype(int)).max() <= 2
    theirs = pil_array(pil_bytes(src, "JPEG", quality=90))
    assert abs(rmse(mine[..., :1], src) - rmse(theirs, src)) < 0.3


def test_both_decoders_agree_on_libstbs_own_jpeg():
    data = Image(photo(3)).to_jpg(quality=90)
    assert rmse(Image.open(data).array, pil_array(data)) < 1.0


# ------------------------------------------------------------------ resize --

PIL_FILTERS = {"linear": PILImage.BILINEAR, "cubic": PILImage.BICUBIC, "box": PILImage.BOX}


def pil_resize(arr, w, h, filt):
    mode = MODES[arr.shape[2]]
    im = PILImage.fromarray(arr[..., 0] if arr.shape[2] == 1 else arr, mode)
    out = np.asarray(im.resize((w, h), resample=PIL_FILTERS[filt]))
    return out[..., None] if out.ndim == 2 else out


def libstb_resize(arr, w, h, filt):
    # srgb=False: Pillow blends the stored values directly, libstb's default resizes in linear light.
    return Image(arr).resize(w, h, Resizer(filt, srgb=False)).array


@pytest.mark.parametrize("filt", ["linear", "cubic"])
@pytest.mark.parametrize("size", [(65, 48), (43, 32), (262, 194), (196, 145), (131, 97)],
                         ids=["half", "third", "up2", "up1.5", "same"])
def test_resize_matches_pillow(filt, size):
    src = photo(3, 131, 97)
    assert rmse(libstb_resize(src, *size, filt), pil_resize(src, *size, filt)) < 1.2


def test_box_resize_matches_pillow_on_whole_number_ratios():
    # Box windows only line up between the two libraries when the ratio is a whole number.
    src = photo(3, 128, 96)
    for size in ((64, 48), (32, 24), (256, 192)):
        assert rmse(libstb_resize(src, *size, "box"), pil_resize(src, *size, "box")) < 1.2


@pytest.mark.parametrize("c", [1, 2, 3, 4])
@pytest.mark.parametrize("filt", ["linear", "cubic"])
def test_resize_matches_pillow_for_every_channel_count(c, filt):
    # 2 and 4 channels: opaque except for one partly transparent region. Both libraries weight
    # colour by alpha (premultiplied), so the colour at the region's edge is compared too.
    src = photo(c, 120, 90)
    if c in (2, 4):
        src[..., -1] = 255
        src[10:50, 20:70, -1] = 90
    assert rmse(libstb_resize(src, 60, 45, filt), pil_resize(src, 60, 45, filt)) < 1.2


def test_resize_does_not_swap_width_and_height():
    src = photo(3, 131, 97)
    out = Image(src).resize(40, 90, Resizer("linear", srgb=False))
    assert (out.width, out.height) == (40, 90)
    assert rmse(out.array, pil_resize(src, 40, 90, "linear")) < 1.2


def test_the_srgb_default_differs_from_pillow_and_the_flag_fixes_it():
    # Not a bug: stb resizes in linear light by default, which is the correct way and not what Pillow does.
    src = np.zeros((64, 64, 3), np.uint8)
    src[:, ::2] = 255                                    # fine black and white stripes
    ref = pil_resize(src, 16, 16, "box")
    assert rmse(libstb_resize(src, 16, 16, "box"), ref) < 1.2
    assert rmse(Image(src).resize(16, 16, Resizer("box")).array, ref) > 20
