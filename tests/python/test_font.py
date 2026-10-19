"""Uses the synthetic font from tests/data (solid-rectangle glyphs).

unitsPerEm=1000, ascent=800, descent=-200: at pixel_height=100 one font unit
is exactly 0.1 px, so every expectation below is exact.
"""
import pathlib
import threading

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb
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
    assert (g.bitmap.numpy() == 255).all()
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
    a = t.bitmap.numpy()[:, :, 0]
    assert a.shape == (100, 100) and (t.origin_x, t.origin_y) == (0, 0)
    assert a[10, 10] == 255 and a[79, 49] == 255 and a[10, 9] == 0 and a[79, 50] == 0  # A
    assert a[45, 60] == 255 and a[79, 89] == 255 and a[45, 59] == 0 and a[44, 60] == 0  # B
    assert int(a.sum()) == 255 * (40 * 70 + 30 * 35)  # nothing else drawn


def test_render_multiline(font):
    a = font.render("A\nA", 100).bitmap.numpy()[:, :, 0]
    assert a.shape[0] == 200 and a[10, 10] == 255 and a[110, 10] == 255 and a[100, 10] == 0


def test_render_result_can_be_saved(font, tmp_path):
    p = tmp_path / "text.png"
    font.render("AB", 100).bitmap.write(p)
    back = stb.Image.open(p)
    assert back.shape == (100, 100, 1)


def test_render_errors(font):
    with pytest.raises(ValueError):
        font.render("", 100)
    with pytest.raises(stb.LimitError):
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
    px = a.image.numpy()[g.y0:g.y1, g.x0:g.x1]
    assert (px == 255).all()
    assert a.image.shape == (256, 256, 1)


def test_atlas_errors(font):
    with pytest.raises(stb.LimitError):
        font.make_atlas("AB", 100, 16, 16)
    with pytest.raises(ValueError):
        font.make_atlas("", 100, 64, 64)
    with pytest.raises(ValueError):
        font.make_atlas("A", 100, 0, 64)


def test_loading_errors():
    with pytest.raises(stb.DecodeError):
        Font.open(b"definitely not a font")
    with pytest.raises(stb.DecodeError):
        Font.open(b"")
    with pytest.raises(ValueError):
        Font.open(FONT_PATH, index=-1)
    with pytest.raises(stb.DecodeError):
        Font.open(FONT_PATH, index=1)  # single-face file
    with pytest.raises(FileNotFoundError):
        Font.open(FONT_PATH.with_name("missing.ttf"))


def test_concurrent_rendering(font):
    ref = font.render("AB\nBA", 100).bitmap.numpy()
    bad = []

    def work():
        for _ in range(50):
            if not np.array_equal(font.render("AB\nBA", 100).bitmap.numpy(), ref):
                bad.append(1)

    ts = [threading.Thread(target=work) for _ in range(8)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert not bad


# ---------------------------------------------------------------- estimates (no font file)

LIBERATION = pathlib.Path("/usr/share/fonts/truetype/liberation")
SAMPLES = ["Hello, world", "The quick brown fox jumps over the lazy dog", "Pack my box with five dozen liquor jugs.",
           "Version 0.6.0 (stable): 1,234.56 / 99%", "Caf\u00e9 cr\u00e8me br\u00fbl\u00e9e"]


def test_estimate_text_matches_the_documented_shape():
    size = stb.estimate_text("Hello\nWorld!", 16)
    assert isinstance(size, stb.TextSize)
    assert size.lines == 2 and size.width > 0
    assert size.height == pytest.approx(2 * stb.estimate_metrics(16).line_height)
    empty = stb.estimate_text("", 16)
    assert empty.width == 0.0 and empty.lines == 1
    assert empty.height == pytest.approx(stb.estimate_metrics(16).line_height)


def test_estimate_families_differ_and_mono_is_fixed_pitch():
    sans, serif, mono = (stb.estimate_text("Wi Wi", 20, family=f).width for f in ("sans", "serif", "mono"))
    assert len({round(sans, 3), round(serif, 3), round(mono, 3)}) == 3
    assert stb.estimate_text("iiiii", 20, family="mono").width == pytest.approx(
        stb.estimate_text("WWWWW", 20, family="mono").width)


def test_estimate_scales_linearly():
    assert stb.estimate_text("Hello", 40).width == pytest.approx(2 * stb.estimate_text("Hello", 20).width)


def test_estimate_errors():
    with pytest.raises(ValueError):
        stb.estimate_text("A", 16, family="comic")
    for bad in (0, -1, 3000, float("nan"), float("inf")):
        with pytest.raises(ValueError):
            stb.estimate_text("A", bad)
        with pytest.raises(ValueError):
            stb.estimate_metrics(bad)


@pytest.mark.parametrize("family, stem", [("sans", "LiberationSans"), ("serif", "LiberationSerif"),
                                          ("mono", "LiberationMono")])
def test_estimate_agrees_with_the_real_font(family, stem):
    """The tables were cut from Liberation: against the real font only kerning is missing, so
    the estimate stays within 3 % (width) and is exact for the vertical metrics."""
    path = LIBERATION / f"{stem}-Regular.ttf"
    if not path.exists():
        pytest.skip("Liberation fonts are not installed here")
    real = Font.open(path)
    assert stb.estimate_metrics(48, family) == pytest.approx(real.metrics(48))
    for text in SAMPLES:
        assert stb.estimate_text(text, 48, family).width == pytest.approx(real.measure(text, 48).width, rel=0.03)


# --- Image.draw_text: at 100 px the font's "A" is a solid rectangle, x 10..49, y 10..79 of the box.

def test_draw_text_paints_the_glyph_where_asked(font):
    white = stb.Image(np.full((200, 200, 3), 255, np.uint8))
    out = white.draw_text("A", font, 100, 20, 30, color=(255, 0, 0))
    a = out.numpy()
    assert out.shape == (200, 200, 3)
    assert (a[40:110, 30:70] == (255, 0, 0)).all()
    assert int((a != 255).any(axis=2).sum()) == 70 * 40   # nothing outside the glyph changed
    assert (white.numpy() == 255).all()                   # the source is untouched


def test_draw_text_color_forms_and_alpha(font):
    black = stb.Image(np.zeros((100, 100, 3), np.uint8))
    assert black.draw_text("A", font, 100, color=200).numpy()[10, 10].tolist() == [200, 200, 200]
    assert black.draw_text("A", font, 100).numpy()[10, 10].tolist() == [0, 0, 0]  # default: black
    half = black.draw_text("A", font, 100, color=(200, 200, 200, 128))
    assert half.numpy()[10, 10].tolist() == [100, 100, 100]


def test_draw_text_on_gray_and_rgba(font):
    gray = stb.Image(np.zeros((100, 100, 1), np.uint8)).draw_text("A", font, 100, color=(255, 255, 255))
    assert gray.channels == 1 and gray.numpy()[10, 10, 0] == 255 and gray.numpy()[10, 9, 0] == 0
    rgba = stb.Image(np.zeros((100, 100, 4), np.uint8)).draw_text("A", font, 100, color=(10, 20, 30))
    assert rgba.numpy()[10, 10].tolist() == [10, 20, 30, 255] and rgba.numpy()[10, 9, 3] == 0


def test_draw_text_clips_and_multiline(font):
    white = stb.Image(np.full((50, 50, 1), 255, np.uint8))
    part = white.draw_text("A", font, 100, -30, -10).numpy()[:, :, 0]
    assert part[0, 0] == 0 and part[49, 19] == 0 and part[0, 20] == 255
    assert (white.draw_text("A", font, 100, 500, 500).numpy() == 255).all()  # fully outside
    two = stb.Image(np.full((200, 100, 1), 255, np.uint8)).draw_text("A\nA", font, 100).numpy()[:, :, 0]
    assert two[10, 10] == 0 and two[110, 10] == 0 and two[100, 10] == 255


def test_draw_text_errors(font):
    img = stb.Image(np.zeros((10, 10, 3), np.uint8))
    with pytest.raises(ValueError):
        img.draw_text("", font, 100)
    with pytest.raises(ValueError):
        img.draw_text("A", font, 0)
    with pytest.raises(ValueError):
        img.draw_text("A", font, 100, color=(1, 2))
    with pytest.raises(ValueError):
        img.draw_text("A", font, 100, color=(1, 2, 300))


def test_draw_text_color_strings(font):
    black = stb.Image(np.zeros((100, 100, 3), np.uint8))
    assert black.draw_text("A", font, 100, color="red").numpy()[10, 10].tolist() == [255, 0, 0]
    assert black.draw_text("A", font, 100, color="#0a141e").numpy()[10, 10].tolist() == [10, 20, 30]
    with pytest.raises(ValueError):
        black.draw_text("A", font, 100, color="not-a-colour")


def test_canvas_draws_many_texts_on_one_copy(font):
    white = stb.Image(np.full((100, 300, 3), 255, np.uint8))
    c = white.canvas(font, 100, color="red")
    c.text("A", 0, 0).text("A", 100, 0).color((0, 0, 255)).text("A", 200, 0)
    out = c.finish().numpy()
    assert out[10, 10].tolist() == [255, 0, 0]
    assert out[10, 110].tolist() == [0, 0, 255]      # the override applied to that text only
    assert out[10, 210].tolist() == [255, 0, 0]
    assert (white.numpy() == 255).all()              # the input is untouched
    one = white.draw_text("A", font, 100, 5, 7, color="red").numpy()
    assert (white.canvas().font(font, 100).color("red").text("A", 5, 7).finish().numpy() == one).all()
    own = white.canvas().text("A", 5, 7).font(font, 100).color("red").finish().numpy()
    assert (own == one).all()                        # a text can bring its own font


def test_canvas_text_takes_the_defaults_when_it_is_added(font):
    white = stb.Image(np.full((100, 200, 3), 255, np.uint8))
    c = white.canvas(font, 100, color="red")
    c.text("A", 0, 0)
    c.color("blue")                                  # later: must not touch the first text
    c.text("A", 100, 0)
    out = c.finish().numpy()
    assert out[10, 10].tolist() == [255, 0, 0]
    assert out[10, 110].tolist() == [0, 0, 255]


def test_canvas_size_and_order(font):
    white = stb.Image(np.full((200, 200, 3), 255, np.uint8))
    small = white.draw_text("A", font, 40, 0, 0).numpy()
    assert (white.canvas(font, 100).text("A", 0, 0).size(40).finish().numpy() == small).all()
    top = (white.canvas(font, 100).text("A", 0, 0).color((0, 0, 0))
                .text("A", 0, 0).color((255, 255, 255)).finish().numpy())
    assert top[10, 10].tolist() == [255, 255, 255]   # the later text is on top


def test_canvas_errors(font):
    img = stb.Image(np.zeros((10, 10, 3), np.uint8))
    c = img.canvas()
    c.text("A")                                      # nothing is checked yet
    with pytest.raises(ValueError):
        c.finish()                                   # no font anywhere
    with pytest.raises(ValueError):
        img.canvas(font, 100).text("A").align("middle").finish()
    c = img.canvas(font, 100)
    t = c.text("A")
    c.finish()
    with pytest.raises(ValueError):
        c.text("A")                                  # finished
    with pytest.raises(ValueError):
        t.color("red")                               # its canvas was finished
    with pytest.raises(ValueError):
        c.finish()


def _black_columns(img):
    return np.flatnonzero((img.numpy()[..., 0] == 0).any(axis=0)).tolist()


def test_draw_text_align(font):
    white = stb.Image(np.full((100, 400, 3), 255, np.uint8))
    w = font.measure("A", 100).width
    left = _black_columns(white.draw_text("A", font, 100, 150, 0))
    centre = _black_columns(white.draw_text("A", font, 100, 150, 0, align="center"))
    right = _black_columns(white.draw_text("A", font, 100, 150, 0, align="right"))
    assert centre[0] - left[0] == -round(w * 0.5)      # x is the centre of the line
    assert right[0] - left[0] == -round(w)             # x is its right edge


def test_draw_text_align_multiline_and_canvas(font):
    white = stb.Image(np.full((400, 400, 3), 255, np.uint8))
    out = white.draw_text("A\nAAA", font, 100, 200, 0, align="center").numpy()
    pitch = font.measure("A\nAAA", 100).height / 2
    row1, row2 = 10, round(pitch) + 10
    first = np.flatnonzero((out[row1, :, 0] == 0))[0]
    second = np.flatnonzero((out[row2, :, 0] == 0))[0]
    assert second < first                              # the longer line starts further left
    via_canvas = white.canvas(font, 100, align="center").text("A\nAAA", 200, 0).finish().numpy()
    assert (via_canvas == out).all()
    over = white.canvas(font, 100, align="right").text("A", 300, 0).align("left").finish()
    assert _black_columns(over)[0] == 300 + 10          # the text's own align won


def test_draw_text_align_errors(font):
    img = stb.Image(np.zeros((10, 10, 3), np.uint8))
    with pytest.raises(ValueError):
        img.draw_text("A", font, 100, align="middle")

