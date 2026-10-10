"""Image.to_pil() / Image.from_pil(): Pillow is imported only when they run."""

import subprocess
import sys
import textwrap

import numpy as np
import pytest
from PIL import Image as PILImage

import libstb

MODES = {1: "L", 2: "LA", 3: "RGB", 4: "RGBA"}


def rand(h, w, c, seed=0):
    return np.random.default_rng(seed).integers(0, 256, (h, w, c), dtype=np.uint8)


def pixels(pil, c):
    return np.asarray(pil).reshape(pil.height, pil.width, c)


# --------------------------------------------------------------------- to_pil


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_to_pil_has_the_mode_the_channels_say_and_the_same_pixels(c):
    a = rand(5, 7, c)
    pil = libstb.Image(a.copy()).to_pil()
    assert isinstance(pil, PILImage.Image)
    assert pil.mode == MODES[c] and pil.size == (7, 5)
    np.testing.assert_array_equal(pixels(pil, c), a)


def test_to_pil_is_a_copy():
    img = libstb.Image(np.zeros((2, 2, 3), np.uint8))
    pil = img.to_pil()
    img.numpy()[:] = 255
    assert pil.getpixel((0, 0)) == (0, 0, 0)
    pil.putpixel((1, 1), (9, 9, 9))
    assert img.numpy()[1, 1].tolist() == [255, 255, 255]


# ------------------------------------------------------------------- from_pil


@pytest.mark.parametrize("c", [1, 2, 3, 4])
def test_from_pil_takes_the_four_native_modes_as_they_are(c):
    a = rand(5, 7, c, seed=c)
    pil = PILImage.fromarray(a[..., 0] if c == 1 else a)
    img = libstb.Image.from_pil(pil)
    assert (img.width, img.height, img.channels) == (7, 5, c)
    np.testing.assert_array_equal(img.numpy(), a)


@pytest.mark.parametrize(
    "mode, expected_mode",
    [("1", "L"), ("La", "LA"), ("RGBa", "RGBA"), ("RGBX", "RGB"), ("CMYK", "RGB"), ("YCbCr", "RGB"), ("PA", "RGBA")],
)
def test_from_pil_converts_the_modes_that_have_8_bit_colour(mode, expected_mode):
    pil = PILImage.new(mode, (5, 4))
    pil.putpixel((1, 2), {"1": 1, "La": (7, 200), "RGBa": (10, 20, 30, 100), "RGBX": (1, 2, 3, 0),
                          "CMYK": (10, 20, 30, 40), "YCbCr": (50, 60, 70), "PA": (3, 120)}[mode])
    img = libstb.Image.from_pil(pil)
    want = np.asarray(pil.convert(expected_mode))
    assert img.channels == len(expected_mode)
    np.testing.assert_array_equal(img.numpy().reshape(want.shape), want)


def test_from_pil_converts_a_palette_image_to_its_colours():
    pil = PILImage.new("P", (4, 3))
    pil.putpalette([255, 0, 0, 0, 255, 0, 0, 0, 255] + [0] * (256 * 3 - 9))
    pil.putpixel((1, 0), 1)
    pil.putpixel((2, 0), 2)
    img = libstb.Image.from_pil(pil)
    assert img.channels == 3
    assert img.numpy()[0, 0].tolist() == [255, 0, 0]
    assert img.numpy()[0, 1].tolist() == [0, 255, 0]
    assert img.numpy()[0, 2].tolist() == [0, 0, 255]
    np.testing.assert_array_equal(img.numpy(), np.asarray(pil.convert("RGB")))


def test_from_pil_keeps_the_transparency_of_a_palette_image():
    pil = PILImage.new("P", (3, 2))
    pil.putpalette([10, 20, 30, 40, 50, 60] + [0] * (256 * 3 - 6))
    pil.putpixel((1, 0), 1)
    pil.info["transparency"] = 0  # palette index 0 is transparent
    img = libstb.Image.from_pil(pil)
    assert img.channels == 4
    assert img.numpy()[0, 0].tolist() == [10, 20, 30, 0]
    assert img.numpy()[0, 1].tolist() == [40, 50, 60, 255]


@pytest.mark.parametrize("mode", ["I", "I;16", "F", "LAB", "HSV"])
def test_from_pil_refuses_modes_without_8_bit_colour(mode):
    with pytest.raises(TypeError, match="8-bit colour"):
        libstb.Image.from_pil(PILImage.new(mode, (3, 2)))


@pytest.mark.parametrize("bad", [None, 3, "photo.png", np.zeros((2, 2, 3), np.uint8), object()])
def test_from_pil_needs_a_pillow_image(bad):
    with pytest.raises(TypeError, match="PIL.Image.Image"):
        libstb.Image.from_pil(bad)


def test_image_of_a_palette_or_cmyk_image_is_not_silently_read_as_colour():
    # Image(pil) stays strict; from_pil is the forgiving one.
    with pytest.raises(TypeError):
        libstb.Image(PILImage.new("F", (2, 2)))


def test_a_round_trip_through_both_methods():
    a = rand(6, 9, 4, 3)
    back = libstb.Image.from_pil(libstb.Image(a.copy()).to_pil())
    np.testing.assert_array_equal(back.numpy(), a)


def test_from_pil_works_on_an_image_opened_from_a_file(tmp_path):
    a = rand(8, 8, 3, 4)
    p = tmp_path / "x.png"
    PILImage.fromarray(a).save(p)
    np.testing.assert_array_equal(libstb.Image.from_pil(PILImage.open(p)).numpy(), a)  # lazy-loaded pixels


# ------------------------------------------------------------------ lazy import


def run(code):
    return subprocess.run([sys.executable, "-c", textwrap.dedent(code)], capture_output=True, text=True)


def test_importing_libstb_does_not_import_pillow():
    r = run("""
        import sys, libstb
        assert 'PIL' not in sys.modules, 'importing libstb imported Pillow'
    """)
    assert r.returncode == 0, r.stderr


def test_the_methods_import_pillow_when_called():
    r = run("""
        import sys, libstb
        img = libstb.Image(memoryview(bytearray(24)).cast('B', shape=[2, 4, 3]))
        assert 'PIL' not in sys.modules
        img.to_pil()
        assert 'PIL' in sys.modules
    """)
    assert r.returncode == 0, r.stderr


def test_without_pillow_the_methods_say_how_to_install_it_and_the_rest_works():
    r = run("""
        import sys
        sys.modules['PIL'] = None  # makes `import PIL` fail
        import libstb
        img = libstb.Image(memoryview(bytearray(24)).cast('B', shape=[2, 4, 3]))
        for call in (lambda: img.to_pil(), lambda: libstb.Image.from_pil(object())):
            try:
                call()
            except ImportError as e:
                assert 'libstb[pillow]' in str(e), e
            else:
                raise SystemExit('no ImportError')
        assert img.convert(1).channels == 1 and len(img.to_png()) > 0  # everything else is unaffected
    """)
    assert r.returncode == 0, r.stderr
