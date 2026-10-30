"""Memory behaviour, checked with pytest-memray (pip install pytest-memray; Linux and macOS):

    python -m pytest tests/python/test_memory.py --memray

Without --memray the limit markers are ignored and these run as plain functional tests, so the
file is harmless in an environment that has the plugin but is not asked to use it. Without the
plugin the whole module is skipped.

limit_leaks counts memory allocated during the test and still alive at its end, native
allocations included (stb and our own code allocate with malloc/new). Each leak test repeats an
operation on an image of about 0.8 MB, so one leaked image per call would show up as tens of MB
against a limit of 200 KB; the small allowance is for interpreter-level caches that a warm-up
call cannot fully settle.
"""

import gc
import os

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

pytest.importorskip("pytest_memray")

import libstb as stb  # noqa: E402
import pngbomb  # noqa: E402

ROUNDS = 100
LEAK_LIMIT = "200 KB"


def _rgb(h=512, w=512, c=3):
    return np.random.default_rng(h * w + c).integers(0, 256, (h, w, c), dtype=np.uint8)


@pytest.fixture(scope="module")
def small():
    return _rgb()


@pytest.fixture(scope="module")
def big():
    return _rgb(3000, 4000)  # 36 MB, built outside the measured part of a test


@pytest.fixture(scope="module", autouse=True)
def warm_up(small):
    """First calls allocate lazily (imports, tables, nanobind caches): do them before measuring."""
    img = stb.Image(small)
    png, jpg = img.to_png(), img.to_jpg()
    stb.Image.open(png), stb.Image.open(jpg)
    img.resize(100, 100).numpy()
    stb.Image(_rgb(c=4)).blend(stb.Image(_rgb(c=4)))
    for _ in range(2):
        gc.collect()


class Wrapper:
    def __init__(self, a):
        self.a = a

    def __dlpack__(self, *args, **kw):
        return self.a.__dlpack__(*args, **kw)

    def __dlpack_device__(self):
        return self.a.__dlpack_device__()


# ------------------------------------------------------------------ nothing is left behind


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_encode_and_decode_leave_nothing_behind(small):
    img = stb.Image(small)
    for _ in range(ROUNDS):
        stb.Image.open(img.to_png())
        stb.Image.open(img.to_jpg(80))
        stb.Image.open(img.to_bmp())
        stb.Image.open(img.to_tga())


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_resize_leaves_nothing_behind(small):
    img = stb.Image(small)
    for i in range(ROUNDS):
        img.resize(200 + i % 7, 300)
        img.resize(900, 700 + i % 5, "mitchell")
        img.thumbnail(64, 64)


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_geometry_leaves_nothing_behind(small):
    img = stb.Image(small)
    for _ in range(ROUNDS):
        img.crop(10, 20, 300, 200)
        img.flip_horizontal(), img.flip_vertical()
        img.rotate90(1), img.rotate90(3), img.transpose()
        img.pad(3, 4, 5, 6, fill=7)
        img.orient(6)


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_convert_composite_and_flatten_leave_nothing_behind(small):
    rgb, rgba = stb.Image(small), stb.Image(_rgb(c=4))
    for _ in range(ROUNDS):
        rgb.convert(stb.PixelFormat.GRAY), rgb.convert(stb.PixelFormat.RGBA), rgba.convert(stb.PixelFormat.RGB)
        rgb.blend(rgba)
        rgba.blend(rgba)
        rgba.flatten((10, 20, 30))
        a = rgba.split()
        stb.Image.merge(a)


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_interop_leaves_nothing_behind(small):
    img = stb.Image(small)
    for _ in range(ROUNDS):
        np.asarray(img)
        img.numpy()
        memoryview(img)
        np.from_dlpack(img)
        stb.Image(np.array(img))
        stb.Image.from_dlpack(small, copy=True)


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_images_viewing_a_dlpack_object_are_freed_even_in_a_cycle(small):
    # An object that holds the image that views it: only the garbage collector can free this,
    # and it needs the image to report the reference it keeps (the old "leaked 1 instances").
    for _ in range(ROUNDS):
        obj = Wrapper(small.copy())
        obj.image = stb.Image.from_dlpack(obj)
        del obj
        gc.collect()


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_the_simd_context_manager_and_the_resizer_leave_nothing_behind(small):
    img = stb.Image(small)
    for _ in range(ROUNDS):
        with stb.simd("scalar"):
            img.convert(stb.PixelFormat.GRAY)
        stb.Resizer("cubic")
        stb.imresize(small, 100, 100)


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_exceptions_leave_nothing_behind(small):
    img = stb.Image(small)
    for _ in range(ROUNDS):
        with pytest.raises(ValueError):
            img.crop(0, 0, 0, 5)
        with pytest.raises(stb.DecodeError):
            stb.Image.open(b"not an image")
        with pytest.raises(ValueError):
            stb.simd("nope")


@pytest.mark.limit_leaks(LEAK_LIMIT)
def test_text_and_canvas_leave_nothing_behind(small):
    img = stb.Image(small)
    font = stb.Font.open(os.path.join(os.path.dirname(__file__), "..", "data", "libstb-test.ttf"))
    for i in range(ROUNDS):
        img.draw_text("AA\nA", font, 40 + i % 3, 5, 5, color=(255, 0, 0, 200), align="center")
        # a finished canvas, texts that outlive their canvas variable, and a canvas that is never finished
        c = stb.Canvas(img)
        c.set_font(font, 40).set_color("white")
        kept = c.add_text("A", 0, 0).bg_color("black").padding(5)
        c.add_text("AA", 50, 50).align("right").size(24)
        c.finish()
        del c
        with pytest.raises(ValueError):
            kept.color("red")                        # finished: refused, but alive
        c = stb.Canvas(img)                          # shapes: kept after finish, and dropped unfinished
        kept_shape = c.add_rect(5, 5, 100, 80).radius(10).stroke(3, "white")
        c.add_ellipse(0, 0, 60, 40)
        c.add_line(0, 0, 200, 150).stroke(4)
        c.finish()
        with pytest.raises(ValueError):
            kept_shape.fill("red")
        d = stb.Canvas(img)
        d.add_rect(1, 1, 50, 50).no_fill().stroke(2)
        d.set_font(font, 30)
        d.add_text("A", 1, 1)
        with pytest.raises(ValueError):
            stb.Canvas(img).add_text("A", 0, 0).color("not-a-colour")


# ------------------------------------------------------------------ no hidden copies


@pytest.mark.limit_memory("2 MB")
def test_wrapping_a_writable_array_does_not_copy_it(big):
    img = stb.Image(big)  # 36 MB of pixels used in place
    view = img.numpy()
    mv = memoryview(img)
    assert view.shape == big.shape and mv.nbytes == big.nbytes and np.shares_memory(view, big)


@pytest.mark.limit_memory("2 MB")
def test_a_dlpack_import_does_not_copy_either(big):
    img = stb.Image.from_dlpack(Wrapper(big))
    assert np.shares_memory(img.numpy(), big)


@pytest.mark.limit_memory("14 MB")
def test_crop_allocates_only_the_cropped_pixels(big):
    out = stb.Image(big).crop(100, 100, 2000, 1500)  # 9 MB
    assert (out.width, out.height) == (2000, 1500)


@pytest.mark.limit_memory("26 MB")
def test_convert_to_gray_allocates_only_the_gray_plane(big):
    out = stb.Image(big).convert(stb.PixelFormat.GRAY)  # 12 MB
    assert out.channels == 1


@pytest.mark.limit_memory("40 MB")
def test_resize_peak_is_the_output_plus_a_small_working_buffer(big):
    out = stb.Image(big).resize(2000, 1500)  # 9 MB out
    assert (out.width, out.height) == (2000, 1500)


@pytest.mark.limit_memory("40 MB")
def test_decoding_a_png_peaks_near_the_size_of_the_pixels(big):
    png = stb.Image(big[:1500, :2000]).to_png()  # 9 MB of pixels, encoded before measuring? no: part of the test
    stb.Image.open(png)


@pytest.fixture(scope="module")
def bomb():
    return pngbomb.make_bomb()  # a 0.5 MB PNG that declares 576 MB, built outside the measured part


@pytest.mark.limit_memory("5 MB")
def test_a_png_bomb_is_refused_before_any_pixel_memory_is_allocated(bomb):
    for _ in range(5):
        with pytest.raises(stb.LimitError):
            stb.Image.open(bomb)


@pytest.fixture(scope="module")
def zip_bomb():
    import zlib

    return zlib.compress(bytes(600 * 1024 * 1024), 9)  # ~600 KB that inflates to 600 MiB


@pytest.mark.limit_memory("60 MB")
def test_zlib_never_inflates_past_max_bytes(zip_bomb):
    for _ in range(3):
        with pytest.raises(stb.LimitError):
            stb.zlib_decompress(zip_bomb, max_bytes=32 * 1024 * 1024)
