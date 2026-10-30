"""Round trips: an operation followed by its inverse gives the input back.

These tests do not know how anything is implemented, so they keep holding while the code is
reorganised. Lossless steps (encode, geometry, channel moves) are compared EXACTLY; the lossy
JPEG step uses the shared tolerance of tolerances.py.
"""
import pytest

np = pytest.importorskip("numpy")

import libstb as stb
from tolerances import JPEG_MAX_DIFF, JPEG_NMAE

CHANNELS = (1, 2, 3, 4)


def noise(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def img(h=37, w=53, c=3, seed=0):
    return stb.Image(noise(h, w, c, seed))


def same(a, b):
    assert (a.width, a.height, a.channels) == (b.width, b.height, b.channels)
    assert a.tobytes() == b.tobytes()


# ---- encoders and decoder


@pytest.mark.parametrize("c", CHANNELS)
@pytest.mark.parametrize("fmt", ("png", "bmp", "tga"))
def test_lossless_encode_then_decode_is_exact(fmt, c):
    if fmt == "bmp" and c == 2:
        pytest.skip("BMP has no gray + alpha: it is stored as RGB")
    im = img(c=c)
    data = getattr(im, f"to_{fmt}")()
    back = stb.Image.open(data, format=stb.PixelFormat(c))
    same(back, im)


@pytest.mark.parametrize("c", CHANNELS)
def test_every_format_in_a_chain_keeps_the_pixels(c):
    im = img(c=c, seed=3)
    data = im.to_png()
    for fmt in ("bmp", "tga", "png", "tga", "bmp", "png") if c != 2 else ("tga", "png", "tga", "png"):
        step = stb.Image.open(data, format=stb.PixelFormat(c))
        data = getattr(step, f"to_{fmt}")()
    same(stb.Image.open(data, format=stb.PixelFormat(c)), im)


@pytest.mark.parametrize("c", (1, 3))
@pytest.mark.parametrize("quality", (75, 95, 100))
def test_jpeg_encode_then_decode_stays_close(c, quality):
    ramp = np.linspace(0, 255, 64 * 64 * c).reshape(64, 64, c).astype(np.uint8)  # smooth: JPEG's best case
    back = stb.Image.open(stb.Image(ramp).to_jpg(quality), format=stb.PixelFormat(c)).numpy()
    diff = np.abs(back.reshape(ramp.shape).astype(int) - ramp.astype(int))
    assert diff.mean() / 255 < (JPEG_NMAE * 40 if quality < 100 else JPEG_NMAE * 10)
    assert diff.max() <= JPEG_MAX_DIFF * (6 if quality < 100 else 3)


@pytest.mark.parametrize("c", CHANNELS)
def test_iminfo_reports_what_was_encoded(c):
    im = img(11, 29, c)
    for fmt in ("png", "bmp", "tga"):
        info = stb.iminfo(getattr(im, f"to_{fmt}")())
        assert (info.width, info.height) == (29, 11)


# ---- geometry


@pytest.mark.parametrize("c", CHANNELS)
def test_flips_and_transpose_are_their_own_inverse(c):
    im = img(c=c)
    same(im.flip_horizontal().flip_horizontal(), im)
    same(im.flip_vertical().flip_vertical(), im)
    same(im.transpose().transpose(), im)


@pytest.mark.parametrize("c", CHANNELS)
def test_four_quarter_turns_are_the_identity_and_opposite_turns_cancel(c):
    im = img(c=c)
    same(im.rotate90(1).rotate90(1).rotate90(1).rotate90(1), im)
    same(im.rotate90(1).rotate90(-1), im)
    same(im.rotate90(2), im.flip_horizontal().flip_vertical())
    same(im.rotate90(4), im)


@pytest.mark.parametrize("c", CHANNELS)
def test_rotate_agrees_with_numpy(c):
    im = img(c=c)
    for turns in range(4):
        assert np.array_equal(im.rotate90(turns).numpy().reshape(-1), np.rot90(im.numpy().reshape(im.height, im.width, c), turns).reshape(-1))


@pytest.mark.parametrize("c", CHANNELS)
def test_pad_then_crop_gives_the_input_back(c):
    im = img(c=c)
    padded = im.pad(3, 5, 7, 2, fill=9)
    assert (padded.width, padded.height) == (im.width + 10, im.height + 7)
    same(padded.crop(3, 5, im.width, im.height), im)


@pytest.mark.parametrize("c", CHANNELS)
def test_split_then_merge_gives_the_input_back(c):
    im = img(c=c)
    parts = im.split()
    assert len(parts) == c
    same(stb.Image.merge(parts), im)


@pytest.mark.parametrize("orientation", range(1, 9))
def test_orient_is_undone_by_the_inverse_orientation(orientation):
    inverse = {1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 8, 7: 7, 8: 6}[orientation]
    im = img()
    same(im.orient(orientation).orient(inverse), im)


# ---- channels


@pytest.mark.parametrize("c", (1, 3))
def test_adding_an_alpha_channel_then_dropping_it_is_exact(c):
    im = img(c=c)
    same(im.convert(stb.PixelFormat(c + 1)).convert(stb.PixelFormat(c)), im)


def test_gray_to_rgb_to_gray_is_exact():
    im = img(c=1)
    same(im.convert(stb.PixelFormat.RGB).convert(stb.PixelFormat.GRAY), im)


@pytest.mark.parametrize("c", CHANNELS)
def test_copy_is_equal_and_independent(c):
    im = img(c=c)
    cp = im.copy()
    same(cp, im)
    assert cp.numpy().__array_interface__["data"][0] != im.numpy().__array_interface__["data"][0]


# ---- interop


@pytest.mark.parametrize("c", CHANNELS)
def test_numpy_and_dlpack_round_trips_are_exact(c):
    im = img(c=c)
    same(stb.Image(im.numpy()), im)
    same(stb.Image.from_dlpack(np.from_dlpack(im)), im)


@pytest.mark.parametrize("c", CHANNELS)
def test_zlib_round_trips_at_every_level(c):
    data = noise(40, 40, c, seed=5).tobytes() + bytes(5000)
    for level in (-1, 0, 1, 5, 9):
        assert stb.zlib_decompress(stb.zlib_compress(data, level)) == data


# ---- resize


@pytest.mark.parametrize("c", CHANNELS)
def test_resizing_to_the_same_size_with_nearest_is_the_identity(c):
    im = img(c=c)
    same(im.resize(im.width, im.height, stb.Resizer("nearest")), im)


@pytest.mark.parametrize("c", CHANNELS)
def test_nearest_upscale_by_two_then_downscale_by_two_is_exact(c):
    im = img(20, 30, c)
    up = im.resize(60, 40, stb.Resizer("nearest"))
    same(up.resize(30, 20, stb.Resizer("nearest")), im)
