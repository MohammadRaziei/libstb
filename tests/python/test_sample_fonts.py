"""The real font of data/ (Vazirmatn: TrueType with GSUB and GPOS): shaping against HarfBuzz, kerning, drawing.

tests/data/ has synthetic fonts for exact numbers; this font is what a user really loads, so it finds what a
tiny hand-made one cannot: real joining tables, real kerning pairs, thousands of rules.
"""
import random

import pytest
np = pytest.importorskip("numpy")  # the tests below need numpy: without it this module is skipped

import libstb as stb
from libstb import Font
from sample_data import BOLD, IMAGES, REGULAR

SALAM = "\u0633\u0644\u0627\u0645"                      # س ل ا م
DONYA = "\u062f\u0646\u06cc\u0627"                      # د ن ی ا
PERSIAN_ONLY = "\u067e\u0686\u0698\u06af"               # پ چ ژ گ: the letters Persian adds to Arabic
LETTERS = ("\u0621\u0622\u0623\u0624\u0625\u0626\u0627\u0628\u0629\u062a\u062b\u062c\u062d\u062e\u062f\u0630\u0631\u0632"
           "\u0633\u0634\u0635\u0636\u0637\u0638\u0639\u063a\u0641\u0642\u0643\u0644\u0645\u0646\u0647\u0648\u0649\u064a"
           "\u067e\u0686\u0698\u06a9\u06af\u06cc")
PERSIAN_DIGITS = "\u06f0\u06f1\u06f2\u06f3\u06f4\u06f5\u06f6\u06f7\u06f8\u06f9"


@pytest.fixture(scope="module")
def font():
    return Font.open(REGULAR)


@pytest.fixture(scope="module")
def bold():
    return Font.open(BOLD)


def test_the_font_is_what_data_readme_says(font, bold):
    for f in (font, bold):
        d = f.design_metrics
        assert (d.units_per_em, d.ascent, d.descent) == (2048, 2100, -1100)
        assert f.glyph_count == 1333
    assert Font.face_count(REGULAR) == Font.face_count(BOLD) == 1


def test_it_covers_latin_arabic_and_persian(font):
    for ch in "AZaz09,.!" + PERSIAN_ONLY + PERSIAN_DIGITS + "\u0627\u0628\u064b\u064e\u0651":
        assert font.has_glyph(ch), hex(ord(ch))
    assert not font.has_glyph("\U0001F600")  # no emoji


def test_regular_and_bold_are_two_faces_of_one_design(font, bold):
    a, b = font.glyph_index("A"), bold.glyph_index("A")
    assert a == b  # the same glyph order
    assert bold.glyph_info(b).advance > font.glyph_info(a).advance  # wider
    assert bold.outline(b) != font.outline(a)
    assert bold.measure("Hello", 40).width > font.measure("Hello", 40).width


# ---- shaping --------------------------------------------------------------------------------------------

def test_joining_letters_take_their_initial_medial_and_final_forms(font):
    isolated = font.shape("\u0628")[0].glyph                       # ب alone
    visual = font.shape("\u0628\u0628\u0628")                      # right to left: the last letter is on the left
    assert [g.cluster for g in visual] == [4, 2, 0]
    forms = [g.glyph for g in visual]
    assert len(set(forms)) == 3 and isolated not in forms            # final, medial, initial: all different


def test_lam_alef_is_one_glyph_and_the_clusters_run_backwards(font):
    shaped = font.shape(SALAM)
    assert len(shaped) == 3 and [g.cluster for g in shaped] == [6, 2, 0]   # م, the ligature لا, س
    assert len(font.shape("\u0644\u0627")) == 1
    # an Arabic word reads right to left whatever the paragraph direction is
    assert [g.cluster for g in font.shape(SALAM, direction="ltr")] == [6, 2, 0]


def test_mixed_text_lays_out_the_runs_in_the_right_order(font):
    text = SALAM + " hello " + DONYA        # bytes: SALAM 0-7, space 8, hello 9-13, space 14, DONYA 15-22
    # first strong letter Arabic: the paragraph runs right to left, so DONYA is leftmost, "hello" stays left to right
    assert [g.cluster for g in font.shape(text)] == [21, 19, 17, 15, 14, 9, 10, 11, 12, 13, 8, 6, 2, 0]
    # forced left to right: SALAM first, but each Arabic word is still reversed inside itself
    assert [g.cluster for g in font.shape(text, direction="ltr")] == [6, 2, 0, 8, 9, 10, 11, 12, 13, 14, 21, 19, 17, 15]


def test_zero_width_non_joiner_stops_the_joining(font):
    joined = font.shape("\u0645\u06cc\u062e\u0648\u0627\u0647\u0645")
    split = font.shape("\u0645\u06cc\u200c\u062e\u0648\u0627\u0647\u0645")  # می‌خواهم: ZWNJ between ی and خ
    assert len(joined) == len(split) == 7                                    # the ZWNJ itself is no glyph
    by_cluster = lambda glyphs: {g.cluster: g.glyph for g in glyphs}
    assert by_cluster(joined)[2] != by_cluster(split)[2]                       # ی: medial in one word, final in the other


def test_marks_take_no_advance(font):
    base = font.shape("\u0628")
    marked = font.shape("\u0628\u064e")  # ب + fatha
    assert len(marked) == 2 and sum(g.x_advance for g in marked) == base[0].x_advance


def test_the_digits_of_persian_are_one_glyph_each(font):
    assert len(font.shape(PERSIAN_DIGITS)) == 10


def _hb():
    hb = pytest.importorskip("uharfbuzz")
    return hb, hb.Font(hb.Face(hb.Blob.from_file_path(str(REGULAR))))


def test_random_persian_text_shapes_like_harfbuzz_glyph_for_glyph_and_advance_for_advance():
    hb, hbfont = _hb()
    font = Font.open(REGULAR)
    rng = random.Random(3)
    checked = 0
    for _ in range(300):
        text = ""
        for _ in range(rng.randint(1, 10)):
            text += " " if rng.random() < 0.12 else rng.choice(LETTERS)
            if rng.random() < 0.08:
                text += rng.choice(PERSIAN_DIGITS)
        text = text.strip()
        if not text:
            continue
        buf = hb.Buffer()
        buf.add_str(text)
        buf.guess_segment_properties()
        buf.direction, buf.script = "rtl", "Arab"
        # libstb does GSUB and the advances; marks and cursive attachment from GPOS are not done
        hb.shape(hbfont, buf, {"kern": False, "mark": False, "mkmk": False, "curs": False})
        ours = font.shape(text, direction="rtl", kerning="none")
        assert [g.glyph for g in ours] == [i.codepoint for i in buf.glyph_infos], text
        assert [round(g.x_advance) for g in ours] == [p.x_advance for p in buf.glyph_positions], text
        checked += 1
    assert checked > 250


@pytest.mark.parametrize("text", ["AV", "To", "Wa", "Hello, AV kerning", "Typography", "AVATAR"])
def test_latin_kerning_advances_equal_harfbuzz(text):
    hb, hbfont = _hb()
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    hb.shape(hbfont, buf, {})  # every default feature, GPOS kerning included
    ours = Font.open(REGULAR).shape(text)
    assert [round(g.x_advance) for g in ours] == [p.x_advance for p in buf.glyph_positions]


# ---- kerning: this font kerns with GPOS and has no `kern` table -------------------------------------------------

def test_the_kerning_of_a_gpos_only_font(font):
    a, v = font.glyph_index("A"), font.glyph_index("V")
    assert font.glyph_kerning(a, v, "automatic") < 0
    assert font.glyph_kerning(a, v, "kern_table") == 0  # no `kern` table: automatic is what finds the pair
    assert font.glyph_kerning(a, v, "none") == 0
    assert font.glyph_kerning(a, a, "automatic") == 0
    pair = font.shape("AV")[0].x_advance
    assert pair == font.glyph_info(a).advance + font.glyph_kerning(a, v)
    assert font.shape("AV", kerning="none")[0].x_advance == font.glyph_info(a).advance
    # Font.kerning is the same number in pixels: size / (ascent - descent) pixels per unit
    assert font.kerning("A", "V", 40) == pytest.approx(font.glyph_kerning(a, v) * 40 / 3200, abs=1e-3)


# ---- drawing --------------------------------------------------------------------------------------------

def ink(img):
    return (img.numpy().reshape(img.height, img.width, -1)[..., 0] < 255).sum()


def white(w=420, h=110):
    return stb.Image(np.full((h, w, 3), 255, np.uint8))


@pytest.mark.parametrize("text", ["Hello", SALAM + " " + DONYA, PERSIAN_ONLY, "libstb \u06cc\u0639\u0646\u06cc 2026"])
def test_draw_text_puts_ink_where_the_measure_says(font, text):
    size = 36
    box = font.measure(text, size)
    out = white().draw_text(text, font, size, 10, 10, color=0)
    a = out.numpy()[..., 0]
    ys, xs = np.where(a < 255)
    assert ys.size > 100
    assert xs.min() >= 10 - 2 and xs.max() <= 10 + box.width + 2          # inside the measured width
    assert ys.min() >= 10 - 2 and ys.max() <= 10 + box.height + 2


def test_a_vector_text_drawn_from_shape_and_outline_is_the_text_draw_text_draws(font):
    # draw_text's size is the height from the descent to the ascent: scale by size / (ascent - descent)
    d = font.design_metrics
    size, text = 40.0, "Hello, AV kerning"
    scale = size / (d.ascent - d.descent)
    ref = white(400, 90).draw_text(text, font, size, 10, 10, color=0)
    img, pen = white(400, 90), 0.0
    for g in font.shape(text):
        m = stb.Matrix().translate(10 + pen * scale, 10 + d.ascent * scale).scale(scale, -scale)
        img = img.fill_outline(stb.Outline(font.outline(g.glyph)), 0, matrix=m)
        pen += g.x_advance
    diff = np.abs(img.numpy().astype(int) - ref.numpy().astype(int))
    assert diff.mean() < 3.0                                              # measured 1.5: whole-pixel glyph positions
    assert (diff.max(axis=2) > 32).mean() < 0.05                          # and the shapes are the same ones


def test_the_width_of_the_glyph_advances_is_the_measured_width(font):
    d = font.design_metrics
    for text in ("Hello", SALAM + " " + DONYA):
        units = sum(g.x_advance for g in font.shape(text))
        assert units * 40 / (d.ascent - d.descent) == pytest.approx(font.measure(text, 40).width, abs=1.5)


def test_canvas_text_in_persian_and_latin(font):
    c = stb.Canvas(white(420, 150))
    c.set_font(font, 34).set_color("black")
    c.add_text(SALAM + " " + DONYA, 410, 10).align("right")
    c.add_text("libstb", 10, 70)
    c.add_text("Bold", 200, 70).color("#cc0000")
    assert ink(c.finish()) > 500


def test_render_and_atlas_of_the_real_font(font):
    text = font.render(SALAM, 40)
    assert text.bitmap.width > 20 and text.bitmap.numpy().any()
    atlas = font.make_atlas("ABCabc" + PERSIAN_ONLY, 32, 256, 256)
    assert atlas.image.numpy().any()
    g = atlas["A"]
    assert g.x1 > g.x0 and g.y1 > g.y0


def test_persian_text_on_a_photograph():
    photo = stb.Image.open(IMAGES / "astronaut.png")
    out = photo.draw_text(SALAM + " " + DONYA, Font.open(BOLD), 44, 20, 20, color="white")
    assert out.width == photo.width and not np.array_equal(out.numpy(), photo.numpy())
    assert np.array_equal(out.numpy()[300:], photo.numpy()[300:])        # nothing drawn far from the text
