"""Left-to-right text is shaped with the font's GSUB and GPOS for its script (Latin, Greek, Cyrillic).

Uses tests/data/libstb-latin-test.ttf (make_latin_test_font.py): solid rectangles, unitsPerEm=1000.
Glyph ids: 0 .notdef, 1 space, 2 f, 3 i, 4 fi, 5 A, 6 V, 7 e, 8 eacute, 9 acute, 10 alpha, 11 beta,
12 alphabeta, 13 acy, 14 kacy, 15 acykacy.
"""
import pathlib
import random

import pytest

import libstb as stb

FONT_PATH = pathlib.Path(__file__).parent.parent / "data" / "libstb-latin-test.ttf"


@pytest.fixture(scope="module")
def font():
    return stb.Font.open(FONT_PATH)


def glyphs(font, text, **options):
    return [(g.glyph, g.cluster) for g in font.shape(text, **options)]


def test_liga_forms_a_ligature_and_ligatures_off_drops_it(font):
    assert glyphs(font, "fi") == [(4, 0)]
    assert glyphs(font, "fi", ligatures=False) == [(2, 0), (3, 1)]
    assert font.shape("fi")[0].x_advance == 650.0


def test_each_script_uses_its_own_features(font):
    assert glyphs(font, "\u03b1\u03b2") == [(12, 0)]
    assert glyphs(font, "\u0430\u043a") == [(15, 0)]
    # script runs in one line; the space takes the script before it; clusters are UTF-8 byte offsets
    assert glyphs(font, "\u03b1\u03b2 \u0430\u043a fi") == [(12, 0), (1, 4), (15, 5), (1, 9), (4, 10)]
    assert glyphs(font, "fi\u0430\u043a") == [(4, 0), (15, 2)]


def test_gpos_kerning_and_the_kerning_modes(font):
    assert [g.x_advance for g in font.shape("AV")] == [600.0, 700.0]
    assert font.shape("AV", kerning="none")[0].x_advance == 700.0
    assert font.shape("AV", kerning="kern_table")[0].x_advance == 700.0  # the legacy table only: the font has none
    assert font.measure("AV", 100).width == pytest.approx(130.0)


def test_ccmp_splits_a_composed_letter_and_gpos_hangs_the_mark(font):
    for text in ("\u00e9", "e\u0301"):
        g = font.shape(text)
        assert [x.glyph for x in g] == [7, 9]
        assert g[0].x_advance == 600.0 and g[1].x_advance == 0.0
        assert (g[1].x_offset + g[0].x_advance, g[1].y_offset) == (200.0, 700.0)
    assert [x.cluster for x in font.shape("\u00e9")] == [0, 0]
    assert [x.cluster for x in font.shape("e\u0301")] == [0, 1]


def test_missing_characters_and_new_lines_keep_their_place(font):
    g = font.shape("xfi")
    assert [(x.glyph, x.cluster) for x in g] == [(0, 0), (4, 1)]
    two = font.shape("fi\nfi")
    assert [x.glyph for x in two] == [4, 0, 4] and two[2].cluster == 3


def test_a_font_without_features_gives_one_glyph_per_character():
    plain = stb.Font.open(FONT_PATH.with_name("libstb-test.ttf"))
    assert len(plain.shape_glyphs("fi")) == 2 and len(plain.shape_glyphs("ABA")) == 3


@pytest.mark.parametrize("alphabet", [list("fiAVe x") + ["\u00e9"], ["\u03b1", "\u03b2", " "], ["\u0430", "\u043a", " "]])
def test_same_glyphs_clusters_and_advances_as_harfbuzz(font, alphabet):
    hb = pytest.importorskip("uharfbuzz")
    hb_font = hb.Font(hb.Face(hb.Blob.from_file_path(str(FONT_PATH))))
    rnd = random.Random(7)
    for _ in range(600):
        text = "".join(rnd.choice(alphabet) for _ in range(rnd.randint(1, 9)))
        byte_at = [0]
        for ch in text:
            byte_at.append(byte_at[-1] + len(ch.encode()))
        for ligatures in (True, False):
            buf = hb.Buffer()
            buf.add_str(text)
            buf.guess_segment_properties()
            buf.cluster_level = hb.BufferClusterLevel.MONOTONE_CHARACTERS  # a mark keeps its own character's cluster
            hb.shape(hb_font, buf, {"kern": True, "mark": True, "mkmk": True, "liga": ligatures, "clig": ligatures})
            want = [(i.codepoint, byte_at[i.cluster], int(p.x_advance)) for i, p in zip(buf.glyph_infos, buf.glyph_positions)]
            got = [(g.glyph, g.cluster, int(g.x_advance)) for g in font.shape(text, ligatures=ligatures)]
            assert got == want, (text, ligatures)
