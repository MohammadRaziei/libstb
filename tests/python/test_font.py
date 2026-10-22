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


def _canvas(img, font=None, size=0, color=None):
    c = stb.Canvas(img)
    if font is not None:
        c.set_font(font, size)
    if color is not None:
        c.set_color(color)
    return c


def test_canvas_draws_many_texts_on_one_copy(font):
    white = stb.Image(np.full((100, 300, 3), 255, np.uint8))
    c = _canvas(white, font, 100, "red")
    c.add_text("A", 0, 0)
    c.add_text("A", 100, 0).color((0, 0, 255))
    c.add_text("A", 200, 0)
    out = c.finish().numpy()
    assert out[10, 10].tolist() == [255, 0, 0]
    assert out[10, 110].tolist() == [0, 0, 255]      # the override applied to that text only
    assert out[10, 210].tolist() == [255, 0, 0]
    assert (white.numpy() == 255).all()              # the input is untouched
    one = white.draw_text("A", font, 100, 5, 7, color="red").numpy()
    c = _canvas(white, font, 100, "red")
    c.add_text("A", 5, 7)
    assert (c.finish().numpy() == one).all()
    c = stb.Canvas(white)
    c.add_text("A", 5, 7).font(font, 100).color("red")
    assert (c.finish().numpy() == one).all()         # a text can bring its own font


def test_canvas_text_takes_the_defaults_when_it_is_added(font):
    white = stb.Image(np.full((100, 200, 3), 255, np.uint8))
    c = _canvas(white, font, 100, "red")
    c.add_text("A", 0, 0)
    c.set_color("blue")                              # later: must not touch the first text
    c.add_text("A", 100, 0)
    out = c.finish().numpy()
    assert out[10, 10].tolist() == [255, 0, 0]
    assert out[10, 110].tolist() == [0, 0, 255]


def test_canvas_size_and_order(font):
    white = stb.Image(np.full((200, 200, 3), 255, np.uint8))
    at_40 = white.draw_text("A", font, 40, 0, 0).numpy()
    c = _canvas(white, font, 100)
    c.add_text("A", 0, 0).size(40)
    assert (c.finish().numpy() == at_40).all()
    c = _canvas(white, font, 100)
    c.add_text("A", 0, 0).color((0, 0, 0))
    c.add_text("A", 0, 0).color((255, 255, 255))
    assert c.finish().numpy()[10, 10].tolist() == [255, 255, 255]   # the later text is on top


def test_canvas_errors(font):
    img = stb.Image(np.zeros((10, 10, 3), np.uint8))
    c = stb.Canvas(img)
    c.add_text("A")                                  # nothing is checked yet
    with pytest.raises(ValueError):
        c.finish()                                   # no font anywhere
    with pytest.raises(ValueError):
        _canvas(img, font, 100).add_text("A").align("middle")
    with pytest.raises(ValueError):
        _canvas(img, font, 100).add_text("A").color("not-a-colour")
    with pytest.raises(ValueError):
        _canvas(img, font, 100).add_text("A").padding(-1)
    c = _canvas(img, font, 100)
    t = c.add_text("A")
    c.finish()
    with pytest.raises(ValueError):
        c.add_text("A")                              # finished
    with pytest.raises(ValueError):
        c.set_color("red")
    with pytest.raises(ValueError):
        t.color("red")                               # its canvas was finished
    with pytest.raises(ValueError):
        c.finish()


def test_canvas_background_and_padding(font):
    white = stb.Image(np.full((300, 300, 3), 255, np.uint8))
    s = font.measure("A", 100)
    w, h = round(s.width), round(s.height)
    c = _canvas(white, font, 100)
    c.add_text("A", 50, 60).bg_color("red").padding(10).color((0, 0, 255))
    out = c.finish().numpy()
    assert out[60 - 10, 50 - 10].tolist() == [255, 0, 0]            # box = layout box + 10 on every side
    assert out[60 + h + 9, 50 + w + 9].tolist() == [255, 0, 0]
    assert out[60 + 5, 50 - 11].tolist() == [255, 255, 255]
    assert out[60 + 5, 50 + w + 10].tolist() == [255, 255, 255]
    assert out[60 - 11, 50 + 5].tolist() == [255, 255, 255]
    assert out[60 + h + 10, 50 + 5].tolist() == [255, 255, 255]
    assert out[70, 60].tolist() == [0, 0, 255]                      # the text is drawn over it
    c = _canvas(white, font, 100)
    c.add_text("A", 0, 0).bg_color("black").padding(100000)
    assert (c.finish().numpy() == 0).all()                          # clipped to the image: no giant layer


def test_canvas_background_follows_align(font):
    white = stb.Image(np.full((200, 400, 3), 255, np.uint8))
    w = round(font.measure("A", 100).width)
    c = _canvas(white, font, 100)
    c.add_text("A", 300, 20).align("right").bg_color((0, 0, 0)).padding(5).color((255, 255, 255))
    out = c.finish().numpy()
    assert out[20 - 5, 300 - w - 5, 0] == 0 and out[23, 300 - w - 6, 0] == 255
    assert out[23, 300 + 4, 0] == 0 and out[23, 300 + 5, 0] == 255


def test_canvas_setters_return_the_same_object_and_keep_the_canvas_alive(font):
    t = stb.Canvas(stb.Image(np.zeros((50, 50, 3), np.uint8))).set_font(font, 20).add_text("A")   # canvas not named
    import gc
    gc.collect()
    assert t.color("red") is t and t.size(10) is t and t.padding(1) is t
    c = stb.Canvas(stb.Image(np.zeros((50, 50, 3), np.uint8)))
    assert c.set_font(font, 20) is c and c.set_color("red") is c and c.set_align("left") is c and c.set_size(9) is c


# The same scene is built by test_canvas_matches_the_cpp_golden_hash in tests/cpp/test_font.cpp:
# both must reach this value, so C++ and Python draw byte-identical pixels.
GOLDEN_CANVAS_FNV1A64 = 0xd7f088b011c4d0dd


def _fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def test_canvas_matches_the_cpp_golden_hash(font):
    c = stb.Canvas(stb.Image(np.full((120, 200, 3), 128, np.uint8)))
    c.set_font(font, 40).set_color("white")
    c.add_text("AA", 10, 5)
    c.add_text("A\nAAA", 100, 5).align("center").color("#ffcc00").bg_color("#222").padding(6)
    c.add_text("AA", 190, 70).align("right").color((255, 255, 255, 120)).bg_color((0, 0, 0, 90)).padding(4).size(24)
    c.add_text("A", 5, 80).font(font).size(30).color("red")
    assert _fnv1a64(c.finish().numpy().tobytes()) == GOLDEN_CANVAS_FNV1A64


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
    c = _canvas(white, font, 100)
    c.set_align("center")
    c.add_text("A\nAAA", 200, 0)
    assert (c.finish().numpy() == out).all()
    c = _canvas(white, font, 100)
    c.set_align("right")
    c.add_text("A", 300, 0).align("left")
    over = c.finish()
    assert _black_columns(over)[0] == 300 + 10          # the text's own align won


def test_draw_text_align_errors(font):
    img = stb.Image(np.zeros((10, 10, 3), np.uint8))
    with pytest.raises(ValueError):
        img.draw_text("A", font, 100, align="middle")



def test_canvas_and_draw_text_arguments(font):
    img = stb.Image(np.zeros((40, 40, 3), np.uint8))
    with pytest.raises(TypeError):
        img.draw_text("A", font, 20, 0, 0, "red")    # colour is keyword-only
    with pytest.raises(TypeError):
        _canvas(img).set_font("not a font", 20)
    with pytest.raises(ValueError):
        stb.Canvas(img).set_align("middle")
    # the Python Font and the native one it wraps are both accepted
    a = img.draw_text("A", font, 20, color="red").numpy()
    b = img.draw_text("A", font._f, 20, color="red").numpy()
    c = _canvas(img, font._f, 20, "red")
    c.add_text("A")
    assert (a == b).all() and (a == c.finish().numpy()).all()


# Shapes and texts together; test_canvas_shapes_match_the_python_golden_hash in tests/cpp/test_font.cpp
# builds the same scene and must reach the same value.
GOLDEN_SHAPES_FNV1A64 = 0x49444ac71d1c9d17


def test_canvas_shapes_match_the_cpp_golden_hash(font):
    c = stb.Canvas(stb.Image(np.full((120, 200, 3), 128, np.uint8)))
    c.set_font(font, 30).set_color("white")
    c.add_rect(10, 10, 80, 50).fill("#224488").radius(12).stroke(3, "white")
    c.add_ellipse(100, 10, 90, 50).fill((255, 200, 0, 160)).stroke(2, (0, 0, 0, 200))
    c.add_line(5, 100, 195, 70).stroke(3, "red")
    c.add_line(5, 110, 195, 110)
    c.add_rect(60, 75, 70, 36).no_fill().stroke(2, "black")
    c.add_text("AA", 70, 80).color("black")
    assert _fnv1a64(c.finish().numpy().tobytes()) == GOLDEN_SHAPES_FNV1A64


def _px(img, x, y):
    return img.numpy()[y, x].tolist()


def test_canvas_rect_sharp_edges_and_default_colour():
    c = stb.Canvas(stb.Image(np.full((30, 30, 3), 255, np.uint8)))
    c.set_color("blue")
    c.add_rect(5, 5, 10, 8)                           # filled with the canvas colour
    c.add_rect(20, 20, 5, 5).fill("red")
    out = c.finish()
    assert _px(out, 5, 5) == [0, 0, 255] and _px(out, 14, 12) == [0, 0, 255]
    assert _px(out, 4, 5) == [255, 255, 255] and _px(out, 15, 5) == [255, 255, 255]
    assert _px(out, 5, 4) == [255, 255, 255] and _px(out, 5, 13) == [255, 255, 255]
    assert _px(out, 20, 20) == [255, 0, 0] and _px(out, 24, 24) == [255, 0, 0]


def test_canvas_stroke_is_inside_and_no_fill():
    c = stb.Canvas(stb.Image(np.full((30, 30, 3), 255, np.uint8)))
    c.add_rect(0, 0, 20, 20).no_fill().stroke(3, "black")
    out = c.finish()
    assert _px(out, 0, 10) == [0, 0, 0] and _px(out, 2, 10) == [0, 0, 0]
    assert _px(out, 3, 10) == [255, 255, 255] and _px(out, 10, 10) == [255, 255, 255]
    assert _px(out, 19, 10) == [0, 0, 0] and _px(out, 20, 10) == [255, 255, 255]


def test_canvas_circle_area_line_and_clipping():
    c = stb.Canvas(stb.Image(np.full((40, 40, 3), 255, np.uint8)))
    c.set_color("black")
    c.add_rect(0, 0, 20, 20).radius(10)
    c.add_ellipse(20, 0, 20, 20)
    out = c.finish().numpy()[:20, :, 0].astype(float)
    area = np.pi * 100
    assert abs((255 - out[:, :20]).sum() / 255 - area) < 2 and abs((255 - out[:, 20:]).sum() / 255 - area) < 2
    assert ((out > 10) & (out < 245)).any()           # soft edge pixels exist
    assert out[0, 0] == 255 and out[10, 10] == 0      # the corner is cut away

    c = stb.Canvas(stb.Image(np.full((12, 20, 3), 255, np.uint8)))
    c.set_color("black")
    c.add_line(2, 5, 17, 5)                           # width 1: exactly row 5
    c.add_line(2, 8, 17, 8).stroke(3, (0, 0, 0, 128))
    out = c.finish()
    assert _px(out, 2, 5) == [0, 0, 0] and _px(out, 17, 5) == [0, 0, 0] and _px(out, 1, 5) == [255, 255, 255]
    assert _px(out, 10, 4) == [255, 255, 255] and _px(out, 10, 6) == [255, 255, 255]
    assert all(126 <= _px(out, 10, r)[0] <= 128 for r in (7, 8, 9))

    c = stb.Canvas(stb.Image(np.full((20, 20, 3), 255, np.uint8)))
    c.add_rect(-1000000, -1000000, 2000000, 2000000).fill("black")   # clipped: no giant layer
    c.add_rect(5, 5, 5, 5).fill("red")                               # later = on top
    c.add_rect(100, 100, 5, 5)                                       # outside: nothing happens
    out = c.finish()
    assert _px(out, 0, 0) == [0, 0, 0] and _px(out, 5, 5) == [255, 0, 0]


def test_canvas_shapes_and_texts_keep_their_order_and_shapes_need_no_font(font):
    c = _canvas(stb.Image(np.full((100, 100, 3), 255, np.uint8)), font, 100, "black")
    c.add_text("A", 0, 0)
    c.add_rect(0, 0, 100, 100).fill("white")          # covers the text
    assert _px(c.finish(), 10, 10) == [255, 255, 255]
    c = stb.Canvas(stb.Image(np.full((10, 10, 3), 255, np.uint8)))
    c.add_rect(0, 0, 5, 5)                            # no font needed
    assert _px(c.finish(), 0, 0) == [0, 0, 0]


def test_canvas_shape_errors():
    c = stb.Canvas(stb.Image(np.full((20, 20, 3), 255, np.uint8)))
    with pytest.raises(ValueError):
        c.add_rect(0, 0, 0, 5)
    with pytest.raises(ValueError):
        c.add_ellipse(0, 0, 5, -1)
    e = c.add_ellipse(0, 0, 5, 5)
    with pytest.raises(ValueError):
        e.radius(2)
    with pytest.raises(ValueError):
        e.stroke(0)
    with pytest.raises(ValueError):
        e.stroke(-1, "red")
    r = c.add_rect(0, 0, 5, 5)
    with pytest.raises(ValueError):
        r.radius(-1)
    l = c.add_line(0, 0, 5, 5)
    for call in (lambda: l.fill("red"), lambda: l.no_fill(), lambda: l.radius(1)):
        with pytest.raises(ValueError):
            call()
    assert r.fill("red") is r and r.stroke(2) is r and r.stroke(2, "white") is r and r.no_fill() is r
    c.finish()
    with pytest.raises(ValueError):
        c.add_rect(0, 0, 5, 5)
    with pytest.raises(ValueError):
        r.fill("red")


# The same scene is built by test_persian_canvas_matches_the_cpp_golden_hash in tests/cpp/test_font.cpp.
# The font is a subset of Vazirmatn (SIL OFL 1.1, tests/data/make_persian_font.py). The hash pins the
# pixels; that the shaping is right was checked by eye and against Pillow with raqm.
GOLDEN_PERSIAN_FNV1A64 = 0x6defa776040b3afc


def test_persian_canvas_matches_the_cpp_golden_hash():
    fa = Font.open(FONT_PATH.with_name("libstb-fa-test.ttf"))
    c = stb.Canvas(stb.Image(np.full((200, 320, 3), 255, np.uint8)))
    c.set_font(fa, 32).set_color("black")
    c.add_text("\u0633\u0644\u0627\u0645 \u062f\u0646\u06cc\u0627", 10, 5)
    c.add_text("\u0633\u0644\u0627\u0645 hello \u06f1\u06f2\u06f3", 10, 45)
    c.add_text("\u0645\u06cc\u200c\u062e\u0648\u0627\u0647\u0645 (\u0644\u0627)", 310, 85).align("right")
    c.add_text("\u067e\u0698\u0648\u0647\u0634 \u06af\u0686\n\u062e\u0637 \u062f\u0648\u0645", 160, 120).align("center").color("#224488")
    assert _fnv1a64(c.finish().numpy().tobytes()) == GOLDEN_PERSIAN_FNV1A64



def _ink_rows(font, text, px=40):
    r = font.render(text, px)
    a = r.bitmap.numpy().reshape(r.bitmap.height, -1)
    ys = np.where((a > 0).any(axis=1))[0]
    return ys.min() - r.origin_y, ys.max() - r.origin_y


def test_marks_take_no_room_and_sit_over_or_under_the_letter():
    fa = Font.open(FONT_PATH.with_name("libstb-fa-test.ttf"))
    ba = "\u0628"
    assert fa.measure(ba + "\u064e", 40).width == fa.measure(ba, 40).width
    plain = _ink_rows(fa, ba)
    above = _ink_rows(fa, ba + "\u064e")
    below = _ink_rows(fa, ba + "\u0650")
    assert above[0] < plain[0] and above[1] == plain[1]
    assert below[1] > plain[1] and below[0] == plain[0]
    stacked = _ink_rows(fa, ba + "\u0651\u064e")
    assert stacked[0] < above[0]
    assert _ink_rows(fa, ba + "\u064e\u0651")[0] == stacked[0]  # shadda goes first in either typing order


# ---- font-driven shaping (GSUB), checked against HarfBuzz ----

GSUB_FONT = FONT_PATH.with_name("libstb-fa-gsub-test.ttf")


def test_gsub_font_glyphs_match_the_harfbuzz_reference():
    # Reference ids from HarfBuzz on the same file; this font has no presentation forms in its
    # cmap, so every shape comes from its GSUB table (the same values as in tests/cpp/test_font.cpp).
    fa = Font.open(GSUB_FONT)
    assert fa.shape_glyphs("\u0633\u0644\u0627\u0645") == [138, 362, 295]
    assert fa.shape_glyphs("\u0627\u0644\u0644\u0647") == [240]  # one Allah glyph
    assert fa.shape_glyphs("\u0644\u0627") == [361]
    assert fa.shape_glyphs("\u0628\u0633\u0645 \u0627\u0644\u0644\u0647") == [240, 98, 330, 296, 264]
    assert fa.shape_glyphs("a\nb")[1] == 0


def test_shape_glyphs_of_text_without_rtl_is_one_glyph_per_character():
    f = Font.open(FONT_PATH.with_name("libstb-fa-test.ttf"))
    assert len(f.shape_glyphs("abc")) == 3


def _hb_font():
    hb = pytest.importorskip("uharfbuzz")
    return hb, hb.Font(hb.Face(hb.Blob.from_file_path(str(GSUB_FONT))))


def test_random_text_shapes_like_harfbuzz():
    import random

    hb, hbfont = _hb_font()
    fa = Font.open(GSUB_FONT)
    letters = "\u0621\u0622\u0623\u0624\u0625\u0626\u0627\u0628\u0629\u062a\u062b\u062c\u062d\u062e\u062f\u0630\u0631\u0632\u0633\u0634\u0635\u0636\u0637\u0638\u0639\u063a\u0641\u0642\u0643\u0644\u0645\u0646\u0647\u0648\u0649\u064a\u067e\u0686\u0698\u06a9\u06af\u06cc"
    marks = "\u064b\u064c\u064d\u064e\u064f\u0650\u0651\u0652\u0670"
    rng = random.Random(7)

    def reference(text):
        buf = hb.Buffer()
        buf.add_str(text)
        buf.guess_segment_properties()
        buf.direction, buf.script = "rtl", "Arab"
        hb.shape(hbfont, buf, {"kern": False, "mark": False, "mkmk": False, "curs": False})
        return [i.codepoint for i in buf.glyph_infos]

    for with_marks in (False, True):
        for _ in range(400):
            text = ""
            for _ in range(rng.randint(1, 9)):
                text += " " if rng.random() < 0.12 else rng.choice(letters)
                if with_marks and rng.random() < 0.3:
                    text += rng.choice(marks)
            text = text.strip()
            if not text:
                continue
            ours, theirs = fa.shape_glyphs(text), reference(text)
            # HarfBuzz puts a mark before its letter in visual order, libstb after it: with marks
            # compare the glyphs as a multiset, without them the order too.
            assert (sorted(ours) == sorted(theirs)) if with_marks else (ours == theirs), text
