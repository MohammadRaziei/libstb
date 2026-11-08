"""The pictures of data/: headers, decoding against Pillow, every writer, resizing, alpha, SIMD and threads.

Real photographs find what synthetic gradients do not: noise, sharp edges, every channel count and a real JPEG.
"""
import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb
from sample_data import IMAGES, PICTURES, PNGS
from tolerances import JPEG_MAX_DIFF, JPEG_NMAE

NAMES = list(PICTURES)


def load(name, **options):
    return stb.Image.open(IMAGES / name, **options)


def nmae(a, b):
    """Mean absolute difference per pixel and channel, as a fraction of 255: the pixel-wise measure of these tests."""
    return float(np.abs(a.astype(np.int64) - b.astype(np.int64)).mean() / 255)


@pytest.mark.parametrize("name", NAMES)
def test_header_says_what_the_decoded_picture_is(name):
    width, height, channels = PICTURES[name]
    info = stb.iminfo(IMAGES / name)
    assert (info.width, info.height, info.channels) == (width, height, channels)
    img = load(name)
    assert (img.width, img.height, img.channels) == (width, height, channels)
    assert img.numpy().shape == (height, width, channels)


@pytest.mark.parametrize("name", NAMES)
def test_decoding_from_bytes_is_decoding_from_the_file(name):
    assert np.array_equal(stb.Image.open((IMAGES / name).read_bytes()).numpy(), load(name).numpy())


@pytest.mark.parametrize("name", NAMES)
def test_the_pictures_are_not_flat(name):
    # a decoder that returned zeros or one colour would pass every other test of this module
    a = load(name).numpy()
    assert a.std() > 10 and len(np.unique(a)) > 100


@pytest.mark.parametrize("name", PNGS)
def test_png_decodes_to_the_same_pixels_as_pillow(name):
    PIL = pytest.importorskip("PIL")
    pytest.importorskip("PIL.Image")
    expected = np.asarray(PIL.Image.open(IMAGES / name))
    assert np.array_equal(load(name).numpy().reshape(expected.shape), expected)


def test_the_real_jpeg_decodes_like_libjpeg():
    PIL = pytest.importorskip("PIL")
    pytest.importorskip("PIL.Image")
    expected = np.asarray(PIL.Image.open(IMAGES / "rocket.jpg").convert("RGB")).astype(int)
    got = load("rocket.jpg").numpy().astype(int)
    assert got.shape == expected.shape
    assert np.abs(got - expected).mean() / 255 < JPEG_NMAE
    assert np.abs(got - expected).max() <= JPEG_MAX_DIFF


@pytest.mark.parametrize("name", NAMES)
def test_pixel_formats_of_a_real_picture(name):
    src = load(name)
    for fmt, channels in (("gray", 1), ("gray_alpha", 2), ("rgb", 3), ("rgba", 4)):
        assert load(name, format=fmt).channels == channels
    rgba = load(name, format="rgba").numpy()
    if src.channels < 4:
        assert (rgba[..., 3] == 255).all()  # a missing alpha is opaque
    assert np.array_equal(load(name, flip=True).numpy(), src.numpy()[::-1])


# ---- every writer ---------------------------------------------------------------------------------------

LOSSLESS = [
    ("png1", lambda i: i.to_png(compression=1)),
    ("png9", lambda i: i.to_png(compression=9)),
    ("tga_rle", lambda i: i.to_tga(rle=True)),
    ("tga_raw", lambda i: i.to_tga(rle=False)),
    ("bmp", lambda i: i.to_bmp()),
]


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize("writer_name, writer", LOSSLESS, ids=[w[0] for w in LOSSLESS])
def test_lossless_writers_give_back_every_pixel(name, writer_name, writer):
    img = load(name)
    if writer_name == "bmp" and img.channels in (1, 2):
        pytest.skip("BMP holds colour: a gray picture comes back as RGB (see test_roundtrip.py)")
    back = stb.Image.open(writer(img))
    assert back.width == img.width and back.height == img.height
    if back.channels == img.channels:
        assert np.array_equal(back.numpy(), img.numpy())
    else:  # a writer that widens gray to RGB: the three channels must all be the gray one
        assert np.array_equal(back.numpy()[..., 0], img.numpy()[..., 0])


@pytest.mark.parametrize("name", ["camera.png", "astronaut.png", "chelsea.png", "rocket.jpg"])
@pytest.mark.parametrize("quality, ceiling", [(50, 0.02), (90, 0.012)])  # measured at worst 0.0154 and 0.0093
def test_jpeg_writer_keeps_the_picture_close(name, quality, ceiling):
    img = load(name)
    # the writer stores a gray picture as three equal components: ask for gray back to compare like with like
    back = stb.Image.open(img.to_jpg(quality=quality), format="gray" if img.channels == 1 else "rgb")
    assert back.numpy().shape == img.numpy().shape
    assert nmae(back.numpy(), img.numpy()) < ceiling


def test_higher_jpeg_quality_is_bigger_and_closer():
    img = load("astronaut.png")
    sizes, errors = [], []
    for quality in (20, 60, 95):
        data = img.to_jpg(quality=quality)
        sizes.append(len(data))
        errors.append(np.abs(stb.Image.open(data).numpy().astype(int) - img.numpy().astype(int)).mean())
    assert sizes == sorted(sizes) and errors == sorted(errors, reverse=True)


def test_png_compression_9_is_not_bigger_than_1():
    img = load("chelsea.png")
    assert len(img.to_png(compression=9)) <= len(img.to_png(compression=1)) < img.width * img.height * 3


# ---- geometry and resizing --------------------------------------------------------------------------------

FILTERS = ["nearest", "linear", "cubic", "bspline", "mitchell", "box", "auto"]


@pytest.mark.parametrize("name", ["camera.png", "astronaut.png", "chelsea.png"])
@pytest.mark.parametrize("filter_name", FILTERS)
@pytest.mark.parametrize("size", [(97, 61), (700, 333)])
def test_resizing_on_the_values_keeps_the_mean(name, filter_name, size):
    img = load(name)
    out = img.resize(*size, stb.Resizer(filter_name, srgb=False))  # srgb=False: average the stored values
    assert (out.width, out.height, out.channels) == (*size, img.channels)
    assert abs(out.numpy().mean() - img.numpy().mean()) < 0.5  # measured 0.0 to 0.1


@pytest.mark.parametrize("name", ["camera.png", "astronaut.png", "chelsea.png"])
@pytest.mark.parametrize("filter_name", ["linear", "cubic", "bspline", "mitchell", "box", "auto"])
def test_the_default_averages_in_linear_light_so_a_shrunk_photo_is_brighter(name, filter_name):
    # Averaging sRGB values as if they were light is what most libraries do and it darkens detail; libstb's default
    # converts to linear light first. On a photograph that is measurably brighter (measured +0.6 to +6.2 levels).
    img = load(name)
    light = img.resize(97, 61, filter_name).numpy().mean()
    values = img.resize(97, 61, stb.Resizer(filter_name, srgb=False)).numpy().mean()
    assert light > values + 0.3


@pytest.mark.parametrize("name", ["astronaut.png", "chelsea.png"])
def test_shrinking_then_growing_stays_close_to_the_original(name):
    img = load(name)
    there = img.resize(img.width // 2, img.height // 2, "mitchell").resize(img.width, img.height, "catmull_rom")
    assert nmae(there.numpy(), img.numpy()) < 0.025  # measured 0.013 and 0.018


@pytest.mark.parametrize("name", NAMES)
def test_rotations_and_flips_undo_each_other(name):
    img = load(name)
    assert np.array_equal(img.rotate90(1).rotate90(-1).numpy(), img.numpy())
    assert np.array_equal(img.rotate90(4).numpy(), img.numpy())
    assert np.array_equal(img.flip_horizontal().flip_horizontal().numpy(), img.numpy())
    assert np.array_equal(img.transpose().transpose().numpy(), img.numpy())
    assert np.array_equal(img.rotate90(2).numpy(), img.flip_horizontal().flip_vertical().numpy())


def test_thumbnail_keeps_the_aspect_ratio_and_never_enlarges():
    img = load("chelsea.png")  # 451 x 300
    thumb = img.thumbnail(100, 100)
    assert thumb.width == 100 and abs(thumb.height - 100 * 300 / 451) <= 1
    assert (img.thumbnail(1000, 1000).width, img.thumbnail(1000, 1000).height) == (451, 300)


def test_crop_is_a_window_of_the_picture():
    img = load("astronaut.png")
    piece = img.crop(100, 50, 200, 120)
    assert np.array_equal(piece.numpy(), img.numpy()[50:170, 100:300])


# ---- alpha: the one picture that has it ---------------------------------------------------------------------

def test_the_badge_has_a_transparent_corner_and_an_opaque_centre():
    a = load("badge.png").numpy()
    assert a[0, 0, 3] == 0 and a[0, -1, 3] == 0 and a[-1, 0, 3] == 0 and a[-1, -1, 3] == 0
    assert a[80, 80, 3] == 255 or a[40, 80, 3] == 255
    assert 0 < (a[..., 3] == 255).mean() < 1 and ((a[..., 3] > 0) & (a[..., 3] < 255)).any()  # a soft rim


def test_blend_over_leaves_transparent_pixels_alone_and_shows_opaque_ones():
    badge = load("badge.png")
    background = load("chelsea.png")
    placed = background.blend(badge, 40, 30).numpy()
    b, bg = badge.numpy(), background.numpy()
    window = placed[30:30 + badge.height, 40:40 + badge.width]
    opaque, clear = b[..., 3] == 255, b[..., 3] == 0
    assert np.array_equal(window[opaque], b[..., :3][opaque])
    assert np.array_equal(window[clear], bg[30:30 + badge.height, 40:40 + badge.width][clear])
    outside = np.ones(placed.shape[:2], bool)
    outside[30:30 + badge.height, 40:40 + badge.width] = False
    assert np.array_equal(placed[outside], bg[outside])  # nothing outside the badge changed


def test_blend_can_hang_over_the_edge():
    badge, background = load("badge.png"), load("chelsea.png")
    clipped = background.blend(badge, background.width - 50, background.height - 50)
    assert (clipped.width, clipped.height) == (background.width, background.height)
    assert not np.array_equal(clipped.numpy(), background.numpy())


def test_flatten_puts_the_transparency_on_a_colour():
    flat = load("badge.png").flatten((10, 20, 30)).numpy()
    clear = load("badge.png").numpy()[..., 3] == 0
    assert flat.shape[2] == 3 and (flat[clear] == (10, 20, 30)).all()


def test_the_alpha_of_the_badge_is_a_mask_and_with_alpha_puts_it_back():
    badge = load("badge.png")
    mask = badge.alpha()
    assert mask.channels == 1 and np.array_equal(mask.numpy()[..., 0], badge.numpy()[..., 3])
    again = badge.convert("rgb").with_alpha(mask)
    assert np.array_equal(again.numpy()[..., 3], badge.numpy()[..., 3])


def test_split_and_merge_give_the_picture_back():
    img = load("astronaut.png")
    planes = img.split()
    assert [p.channels for p in planes] == [1, 1, 1]
    assert np.array_equal(stb.Image.merge(planes).numpy(), img.numpy())


# ---- SIMD backends and threads must not change a single byte -------------------------------------------------

@pytest.mark.parametrize("backend", stb.simd_backends())
def test_every_simd_backend_gives_the_scalar_result_on_real_pictures(backend):
    base = load("astronaut.png", format="rgba")
    overlay = load("badge.png")
    with stb.simd("scalar"):
        want = {
            "blend": base.blend(overlay, 20, 20, opacity=0.7).numpy(),
            "convert": base.convert("gray").numpy(),
            "flatten": overlay.flatten((9, 99, 199)).numpy(),
            "resize": base.resize(333, 211).numpy(),
        }
    with stb.simd(backend):
        assert np.array_equal(base.blend(overlay, 20, 20, opacity=0.7).numpy(), want["blend"])
        assert np.array_equal(base.convert("gray").numpy(), want["convert"])
        assert np.array_equal(overlay.flatten((9, 99, 199)).numpy(), want["flatten"])
        assert np.array_equal(base.resize(333, 211).numpy(), want["resize"])


@pytest.mark.parametrize("threads", [1, 2, 4])
def test_the_thread_count_does_not_change_the_pixels(threads):
    img = load("astronaut.png")
    want = img.resize(1500, 1000, "cubic").numpy()
    with stb.threads(threads):
        assert np.array_equal(img.resize(1500, 1000, "cubic").numpy(), want)
