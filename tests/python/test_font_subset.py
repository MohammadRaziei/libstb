"""Font.embedding / can_subset / subset.

The C++ tests check a subset against its original through libstb's own reader. Here the file is checked by fontTools,
which knows nothing of libstb: it must open it, read the same outlines (composite glyphs decomposed, because the parts
are renumbered) and the same advances.
"""

import io
import pathlib

import pytest

import libstb as stb
from libstb import Font

ROOT = pathlib.Path(__file__).parent.parent.parent
VAZIRMATN = ROOT / "data" / "fonts" / "Vazirmatn-Regular.ttf"
DATA = ROOT / "tests" / "data"


def glyphs_of(font, text):
    return [font.glyph_index(c) for c in text]


def test_embedding_reports_the_license_bits():
    e = Font.open(VAZIRMATN).embedding
    assert e == stb.FontEmbedding("installable", True, False, 0)
    assert Font.open(DATA / "libstb-latin-test.ttf").embedding.permission == "preview_print"


def test_can_subset_is_false_for_cff_and_for_an_applied_variation():
    assert Font.open(VAZIRMATN).can_subset
    assert not Font.open(DATA / "libstb-cff-test.otf").can_subset
    variable = Font.open(DATA / "libstb-variable-test.ttf")
    assert variable.can_subset
    assert not variable.with_weight(800).can_subset


def test_subset_keeps_what_was_asked_and_the_parts_of_composites():
    font = Font.open(VAZIRMATN)
    wanted = glyphs_of(font, "AÁ\u0628 ")  # Á and beh are composites
    sub = font.subset(wanted)
    assert isinstance(sub.data, bytes) and sub.glyphs[0] == 0
    assert list(sub.glyphs) == sorted(sub.glyphs)
    for g in wanted:
        assert sub.new_index(g) == list(sub.glyphs).index(g)
    assert len(sub.glyphs) > len(set(wanted)) + 1  # the parts of the composites came along
    assert sub.new_index(font.glyph_index("Z")) is None
    assert len(sub.data) < VAZIRMATN.stat().st_size // 20


def test_subset_errors():
    font = Font.open(VAZIRMATN)
    with pytest.raises(ValueError):
        font.subset([-1])
    with pytest.raises(ValueError):
        font.subset([font.glyph_count])
    with pytest.raises(stb.Error):
        Font.open(DATA / "libstb-cff-test.otf").subset([1])


def test_the_subset_is_a_valid_font_with_the_same_glyphs_for_fonttools():
    ttlib = pytest.importorskip("fontTools.ttLib")
    from fontTools.pens.recordingPen import DecomposingRecordingPen

    font = Font.open(VAZIRMATN)
    sub = font.subset(glyphs_of(font, "AaÁé\u0628\u0633\u0645"))

    original = ttlib.TTFont(str(VAZIRMATN))
    made = ttlib.TTFont(io.BytesIO(sub.data))
    assert made["maxp"].numGlyphs == len(sub.glyphs)
    old_order, new_order = original.getGlyphOrder(), made.getGlyphOrder()
    old_set, new_set = original.getGlyphSet(), made.getGlyphSet()
    for new, old in enumerate(sub.glyphs):
        a = DecomposingRecordingPen(old_set)
        b = DecomposingRecordingPen(new_set)
        old_set[old_order[old]].draw(a)
        new_set[new_order[new]].draw(b)
        assert a.value == b.value, f"glyph {old}"
        assert original["hmtx"][old_order[old]] == made["hmtx"][new_order[new]], f"glyph {old}"
    made.save(io.BytesIO())  # fontTools can write it again: every table is readable
