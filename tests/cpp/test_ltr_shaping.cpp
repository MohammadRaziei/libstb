// Left-to-right text is shaped with the font's GSUB and GPOS for its script (Latin, Greek, Cyrillic).
// The font is tests/data/make_latin_test_font.py: solid rectangles, unitsPerEm=1000, ascent 800 and
// descent -200, so at pixel_height=100 one font unit is 0.1 px. Glyph ids: 0 .notdef, 1 space, 2 f, 3 i,
// 4 fi, 5 A, 6 V, 7 e, 8 eacute, 9 acute, 10 alpha, 11 beta, 12 alphabeta, 13 acy, 14 kacy, 15 acykacy.
#include <cstdint>
#include <string>
#include <vector>

#include "test_common.hpp"

namespace {

stb::font latin_font() { return stb::font::open(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-latin-test.ttf"); }

using G = std::vector<std::uint32_t>;

}  // namespace

UTEST(libstb_ltr_shaping_tests, test_the_liga_feature_forms_a_ligature_and_ligatures_off_drops_it) {
    const stb::font f = latin_font();
    ASSERT_TRUE(f.shape_glyphs("fi") == (G{4}));
    const std::vector<stb::shaped_glyph> on = f.shape("fi");
    ASSERT_EQ(std::size_t(1), on.size());
    EXPECT_EQ(std::uint32_t(0), on[0].cluster);
    EXPECT_EQ(650.0f, on[0].x_advance);

    stb::shape_options off;
    off.ligatures = false;
    const std::vector<stb::shaped_glyph> plain = f.shape("fi", off);
    ASSERT_EQ(std::size_t(2), plain.size());
    EXPECT_EQ(std::uint32_t(2), plain[0].glyph);
    EXPECT_EQ(std::uint32_t(3), plain[1].glyph);
    EXPECT_EQ(std::uint32_t(0), plain[0].cluster);
    EXPECT_EQ(std::uint32_t(1), plain[1].cluster);
}

UTEST(libstb_ltr_shaping_tests, test_each_script_uses_its_own_features) {
    const stb::font f = latin_font();
    ASSERT_TRUE(f.shape_glyphs("\xCE\xB1\xCE\xB2") == (G{12}));  // alpha beta: the grek ligature
    ASSERT_TRUE(f.shape_glyphs("\xD0\xB0\xD0\xBA") == (G{15}));  // a ka: the cyrl ligature
    // runs of different scripts in one line, with the common space between them taking the script before it
    const std::vector<stb::shaped_glyph> mixed = f.shape("\xCE\xB1\xCE\xB2 \xD0\xB0\xD0\xBA fi");
    ASSERT_EQ(std::size_t(5), mixed.size());
    const std::uint32_t want_glyph[5] = {12, 1, 15, 1, 4};
    const std::uint32_t want_cluster[5] = {0, 4, 5, 9, 10};  // byte offsets
    for (std::size_t k = 0; k < 5; ++k) {
        EXPECT_EQ(want_glyph[k], mixed[k].glyph);
        EXPECT_EQ(want_cluster[k], mixed[k].cluster);
    }
    ASSERT_TRUE(f.shape_glyphs("fi\xD0\xB0\xD0\xBA") == (G{4, 15}));  // a ligature does not cross into the next script
}

UTEST(libstb_ltr_shaping_tests, test_gpos_kerning_applies_and_kerning_modes_still_choose_their_source) {
    const stb::font f = latin_font();
    const std::vector<stb::shaped_glyph> kerned = f.shape("AV");
    ASSERT_EQ(std::size_t(2), kerned.size());
    EXPECT_EQ(600.0f, kerned[0].x_advance);  // 700 - 100: the pair adjustment of GPOS (kern)
    EXPECT_EQ(700.0f, kerned[1].x_advance);

    stb::shape_options none;
    none.kerning = stb::kerning_mode::none;
    EXPECT_EQ(700.0f, f.shape("AV", none)[0].x_advance);
    stb::shape_options table;
    table.kerning = stb::kerning_mode::kern_table;  // the legacy table only; this font has none
    EXPECT_EQ(700.0f, f.shape("AV", table)[0].x_advance);

    // drawing and measuring make the same decisions: the pair is 100 units (10 px) tighter
    EXPECT_NEAR(130.0f, f.measure("AV", 100.0f).width, 1e-3f);
    EXPECT_NEAR(65.0f, f.measure("fi", 100.0f).width, 1e-3f);
}

UTEST(libstb_ltr_shaping_tests, test_ccmp_splits_a_composed_letter_and_gpos_hangs_the_mark_on_it) {
    const stb::font f = latin_font();
    // U+00E9 is decomposed by ccmp into e and the acute (both keep the cluster of the letter), and a
    // typed e + U+0301 gives the same glyphs: the mark's anchor on e is (300, 700), its own (100, 0)
    for (const char* text : {"\xC3\xA9", "e\xCC\x81"}) {
        const std::vector<stb::shaped_glyph> g = f.shape(text);
        ASSERT_EQ(std::size_t(2), g.size());
        EXPECT_EQ(std::uint32_t(7), g[0].glyph);
        EXPECT_EQ(std::uint32_t(9), g[1].glyph);
        EXPECT_EQ(600.0f, g[0].x_advance);
        EXPECT_EQ(0.0f, g[1].x_advance);
        EXPECT_EQ(0.0f, g[1].x_offset + g[0].x_advance - 200.0f);  // drawn 200 right of the origin of e
        EXPECT_EQ(700.0f, g[1].y_offset);
    }
    EXPECT_EQ(std::uint32_t(0), f.shape("\xC3\xA9")[1].cluster);
    EXPECT_EQ(std::uint32_t(1), f.shape("e\xCC\x81")[1].cluster);
}

UTEST(libstb_ltr_shaping_tests, test_missing_characters_and_new_lines_keep_their_place) {
    const stb::font f = latin_font();
    const std::vector<stb::shaped_glyph> g = f.shape("xfi");
    ASSERT_EQ(std::size_t(2), g.size());
    EXPECT_EQ(std::uint32_t(0), g[0].glyph);
    EXPECT_TRUE(g[0].kind == stb::glyph_kind::notdef);
    EXPECT_EQ(std::uint32_t(4), g[1].glyph);
    EXPECT_EQ(std::uint32_t(1), g[1].cluster);

    const std::vector<stb::shaped_glyph> two = f.shape("fi\nfi");
    ASSERT_EQ(std::size_t(3), two.size());
    EXPECT_EQ(std::uint32_t(4), two[0].glyph);
    EXPECT_TRUE(two[1].kind == stb::glyph_kind::line_break);
    EXPECT_EQ(std::uint32_t(4), two[2].glyph);
    EXPECT_EQ(std::uint32_t(3), two[2].cluster);
}

UTEST(libstb_ltr_shaping_tests, test_a_real_font_with_a_latin_liga_feature_is_shaped_and_one_without_is_not) {
    // The Persian test font (a Vazirmatn subset) has a Latin liga lookup for f + i in its GSUB
    const stb::font v = stb::font::open(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-fa-gsub-test.ttf");
    EXPECT_EQ(std::size_t(1), v.shape_glyphs("fi").size());
    stb::shape_options off;
    off.ligatures = false;
    EXPECT_EQ(std::size_t(2), v.shape("fi", off).size());
    EXPECT_EQ(std::size_t(3), v.shape_glyphs("a\nb").size());  // nothing to form: one glyph per character

    // The solid-rectangle test font has no GSUB and no GPOS: one glyph per character, as always
    const stb::font plain = stb::font::open(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-test.ttf");
    EXPECT_EQ(std::size_t(3), plain.shape_glyphs("ABA").size());
    EXPECT_EQ(std::size_t(2), plain.shape_glyphs("fi").size());
}
