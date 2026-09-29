"""Uses the synthetic font from tests/data (solid-rectangle glyphs).

unitsPerEm=1000, ascent=800, descent=-200: at pixel_height=100 one font unit
is exactly 0.1 px, so every expectation below is exact.
"""
import pathlib
import threading

import numpy as np
import pytest

import libstb
from libstb import Font

FONT_PATH = pathlib.Path(__file__).parent.parent / "data" / "libstb-test.ttf"


@pytest.fixture(scope="module")
def font():
    return Font.open(FONT_PATH)


def test_open_from_path_str_and_bytes():
    for src in (FONT_PATH, str(FONT_PATH), FONT_PATH.read_bytes(), bytearray(FONT_PATH.read_bytes())):
        assert Font.open(src).advance("A", 100) == 60.0


def test_metrics(font):
    m = font.metrics(100)
    assert (m.ascent, m.descent, m.line_gap, m.line_height) == (80.0, -20.0, 0.0, 100.0)


def test_lookup_advance_kerning(font):
    assert font.has_glyph("A") and font.has_glyph("é") and font.has_glyph("😀")
    assert not font.has_glyph("Z")
    assert font.advance("B", 100) == 50.0
    assert font.kerning("A", "B", 100) == -10.0
    assert font.kerning("B", "A", 100) == 0.0


def test_render_glyph_is_an_exact_rectangle(font):
    g = font.render_glyph("A", 100)
    assert (g.bitmap.width, g.bitmap.height, g.bitmap.channels) == (40, 70, 1)
    assert (g.bitmap.array == 255).all()
    assert (g.x_offset, g.y_offset, g.advance) == (10, -70, 60.0)


def test_blank_glyph_has_no_bitmap(font):
    g = font.render_glyph(" ", 100)
    assert g.bitmap is None and g.advance == 30.0


def test_measure(font):
    assert font.measure("AB", 100) == (100.0, 100.0, 1)
    assert font.measure("A\nAB", 100) == (100.0, 200.0, 2)
    assert font.measure("é", 100).width == 40.0
    assert font.measure("😀", 100).width == 70.0


def test_render_places_ink_exactly(font):
    t = font.render("AB", 100)
    a = t.bitmap.array[:, :, 0]
    assert a.shape == (100, 100) and (t.origin_x, t.origin_y) == (0, 0)
    assert a[10, 10] == 255 and a[79, 49] == 255 and a[10, 9] == 0 and a[79, 50] == 0  # A
    assert a[45, 60] == 255 and a[79, 89] == 255 and a[45, 59] == 0 and a[44, 60] == 0  # B
    assert int(a.sum()) == 255 * (40 * 70 + 30 * 35)  # nothing else drawn


def test_render_multiline(font):
    a = font.render("A\nA", 100).bitmap.array[:, :, 0]
    assert a.shape[0] == 200 and a[10, 10] == 255 and a[110, 10] == 255 and a[100, 10] == 0


def test_render_result_can_be_saved(font, tmp_path):
    p = tmp_path / "text.png"
    font.render("AB", 100).bitmap.write(p)
    back = libstb.Image.open(p)
    assert back.shape == (100, 100, 1)


def test_render_errors(font):
    with pytest.raises(ValueError):
        font.render("", 100)
    with pytest.raises(libstb.LimitError):
        font.render("A", 100, max_bytes=10)
    with pytest.raises(ValueError):
        font.render_glyph("AB", 100)  # two characters
    with pytest.raises(ValueError):
        font.has_glyph("")


@pytest.mark.parametrize("bad", [0, -1, 3000, float("nan"), float("inf")])
def test_pixel_height_validated(font, bad):
    with pytest.raises(ValueError):
        font.metrics(bad)


def test_atlas(font):
    a = font.make_atlas("AAB é😀", 100, 256, 256)
    assert len(a) == 5 and "A" in a and "Z" not in a  # duplicate A ignored, space included
    g = a["A"]
    assert (g.x1 - g.x0, g.y1 - g.y0) == (40, 70)
    assert (g.xoff, g.yoff, g.advance) == (10.0, -70.0, 60.0)
    px = a.image.array[g.y0:g.y1, g.x0:g.x1]
    assert (px == 255).all()
    assert a.image.shape == (256, 256, 1)


def test_atlas_errors(font):
    with pytest.raises(libstb.LimitError):
        font.make_atlas("AB", 100, 16, 16)
    with pytest.raises(ValueError):
        font.make_atlas("", 100, 64, 64)
    with pytest.raises(ValueError):
        font.make_atlas("A", 100, 0, 64)


def test_loading_errors():
    with pytest.raises(libstb.DecodeError):
        Font.open(b"definitely not a font")
    with pytest.raises(libstb.DecodeError):
        Font.open(b"")
    with pytest.raises(ValueError):
        Font.open(FONT_PATH, index=-1)
    with pytest.raises(libstb.DecodeError):
        Font.open(FONT_PATH, index=1)  # single-face file
    with pytest.raises(FileNotFoundError):
        Font.open(FONT_PATH.with_name("missing.ttf"))


def test_concurrent_rendering(font):
    ref = font.render("AB\nBA", 100).bitmap.array
    bad = []

    def work():
        for _ in range(50):
            if not np.array_equal(font.render("AB\nBA", 100).bitmap.array, ref):
                bad.append(1)

    ts = [threading.Thread(target=work) for _ in range(8)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert not bad
