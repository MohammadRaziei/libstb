#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_common.hpp"

// font::shape: positioned glyphs in font units, with the byte offset of the text each one came from.
// Fonts of tests/data:
//   libstb-test.ttf          rectangles: A advances 600, B 500, a space 300, legacy `kern` (A, B) = -100
//   libstb-fa-gsub-test.ttf  Persian letters whose shapes all come from GSUB (checked against HarfBuzz in test_font.cpp)
//   libstb-fa-test.ttf       Persian letters as presentation forms, with harakat

namespace {

using U = std::vector<std::uint32_t>;

std::string data(const char* name) { return std::string(LIBSTB_TEST_DATA_DIR) + "/" + name; }
stb::font rects() { return stb::font::open(data("libstb-test.ttf")); }
stb::font gsub_font() { return stb::font::open(data("libstb-fa-gsub-test.ttf")); }
stb::font marks_font() { return stb::font::open(data("libstb-fa-test.ttf")); }

bool close_to(float a, float b) { return a - b < 1e-3f && b - a < 1e-3f; }

U clusters(const std::vector<stb::shaped_glyph>& g) {
    U out;
    for (const stb::shaped_glyph& s : g) out.push_back(s.cluster);
    return out;
}

U glyphs(const std::vector<stb::shaped_glyph>& g) {
    U out;
    for (const stb::shaped_glyph& s : g) out.push_back(s.glyph);
    return out;
}

stb::shape_options with_direction(stb::text_direction d) {
    stb::shape_options o;
    o.direction = d;
    return o;
}

}  // namespace

UTEST(libstb_font_shape_tests, test_latin_text_is_one_glyph_per_character_in_font_units) {
    const stb::font f = rects();
    const std::vector<stb::shaped_glyph> g = f.shape("AB");
    ASSERT_EQ(std::size_t(2), g.size());
    ASSERT_EQ(f.glyph_index(U'A'), g[0].glyph);
    ASSERT_EQ(f.glyph_index(U'B'), g[1].glyph);
    ASSERT_TRUE(clusters(g) == (U{0, 1}));
    ASSERT_TRUE(g[0].x_advance == 500.0f);  // 600, kerned by -100 towards B
    ASSERT_TRUE(g[1].x_advance == 500.0f);  // nothing follows it
    for (const stb::shaped_glyph& s : g) {
        ASSERT_TRUE(s.x_offset == 0.0f && s.y_offset == 0.0f);
    }
    ASSERT_TRUE(f.shape("").empty());
}

UTEST(libstb_font_shape_tests, test_the_kerning_option_chooses_the_kerning) {
    const stb::font f = rects();
    stb::shape_options no_kerning;
    no_kerning.kerning = stb::kerning_mode::none;
    ASSERT_TRUE(f.shape("AB", no_kerning)[0].x_advance == 600.0f);
    stb::shape_options table;
    table.kerning = stb::kerning_mode::kern_table;
    ASSERT_TRUE(f.shape("AB", table)[0].x_advance == 500.0f);
    ASSERT_TRUE(f.shape("BA")[0].x_advance == 500.0f);  // no pair (B, A): the advance of B alone

    // whatever the font says about a pair is what goes into the advance of the first glyph
    for (const stb::kerning_mode mode : {stb::kerning_mode::none, stb::kerning_mode::kern_table, stb::kerning_mode::automatic}) {
        stb::shape_options o;
        o.kerning = mode;
        const std::vector<stb::shaped_glyph> g = f.shape("AB", o);
        ASSERT_TRUE(g[0].x_advance == f.glyph_info(g[0].glyph).advance + f.glyph_kerning(g[0].glyph, g[1].glyph, mode));
    }
}

UTEST(libstb_font_shape_tests, test_a_cluster_is_a_byte_offset_into_the_utf8_text) {
    const stb::font f = rects();
    ASSERT_TRUE(clusters(f.shape("A\u00E9B")) == (U{0, 1, 3}));          // e acute: 2 bytes
    ASSERT_TRUE(clusters(f.shape("A\U0001F600B")) == (U{0, 1, 5}));      // an emoji: 4 bytes
    ASSERT_TRUE(clusters(f.shape("\u00E9\u00E9")) == (U{0, 2}));
}

UTEST(libstb_font_shape_tests, test_a_new_line_is_a_glyph_zero_and_ends_the_kerning_pair) {
    const stb::font f = rects();
    const std::vector<stb::shaped_glyph> g = f.shape("A\nB");
    ASSERT_EQ(std::size_t(3), g.size());
    ASSERT_EQ(std::uint32_t(0), g[1].glyph);
    ASSERT_TRUE(g[1].x_advance == 0.0f);
    ASSERT_TRUE(clusters(g) == (U{0, 1, 2}));
    ASSERT_TRUE(g[0].x_advance == 600.0f);  // not kerned towards a B on the next line
}

UTEST(libstb_font_shape_tests, test_a_carriage_return_has_no_glyph_and_does_not_break_the_pair) {
    const stb::font f = rects();
    const std::vector<stb::shaped_glyph> g = f.shape("A\rB");
    ASSERT_EQ(std::size_t(2), g.size());
    ASSERT_TRUE(clusters(g) == (U{0, 2}));
    ASSERT_TRUE(g[0].x_advance == 500.0f);
}

UTEST(libstb_font_shape_tests, test_it_chooses_the_same_glyphs_as_shape_glyphs) {
    const stb::font g = gsub_font();
    for (const char* text : {"AB", "a\nb", "\u0633\u0644\u0627\u0645", "\u0627\u0644\u0644\u0647", "\u0628\u0633\u0645 \u0627\u0644\u0644\u0647",
                             "\u067E\u0698\u0648\u0647\u0634 \u06AF\u0686", "\u0633 ab", "(\u0633)\nab \u0633"}) {
        ASSERT_TRUE(glyphs(g.shape(text)) == g.shape_glyphs(text));
    }
}

UTEST(libstb_font_shape_tests, test_clusters_follow_the_letters_through_joining_ligatures_and_reordering) {
    const stb::font f = gsub_font();
    // right-to-left: the last letter is the leftmost glyph, so the offsets go down
    ASSERT_TRUE(clusters(f.shape("\u0633\u0644\u0627\u0645")) == (U{6, 2, 0}));  // seen, lam + alef (one glyph), meem
    ASSERT_EQ(std::size_t(3), f.shape("\u0633\u0644\u0627\u0645").size());
    ASSERT_TRUE(clusters(f.shape("\u0627\u0644\u0644\u0647")) == (U{0}));        // the Allah ligature: the cluster of its first letter
    ASSERT_TRUE(clusters(f.shape("\u0644\u0627")) == (U{0}));
    ASSERT_TRUE(clusters(f.shape("\u0628\u0633\u0645 \u0627\u0644\u0644\u0647")) == (U{7, 6, 4, 2, 0}));
    // a Latin word keeps its own order inside the right-to-left line; brackets swap places
    ASSERT_TRUE(clusters(f.shape("\u0633 ab")) == (U{3, 4, 2, 0}));
    ASSERT_TRUE(clusters(f.shape("(\u0633)")) == (U{3, 1, 0}));
    // each line on its own, a new line is a cluster too
    ASSERT_TRUE(clusters(f.shape("\u0633\u0644\n\u0645")) == (U{2, 0, 4, 5}));
}

UTEST(libstb_font_shape_tests, test_the_direction_option_decides_the_order_of_the_runs) {
    const stb::font f = gsub_font();
    // Persian first: the paragraph is right-to-left by itself
    ASSERT_TRUE(clusters(f.shape("\u0633 ab", with_direction(stb::text_direction::automatic))) == (U{3, 4, 2, 0}));
    ASSERT_TRUE(clusters(f.shape("\u0633 ab", with_direction(stb::text_direction::rtl))) == (U{3, 4, 2, 0}));
    // forced left to right: the Persian word is one right-to-left run in a left-to-right line
    ASSERT_TRUE(clusters(f.shape("\u0633 ab", with_direction(stb::text_direction::ltr))) == (U{0, 2, 3, 4}));

    // a neutral after left-to-right text goes to the far side of a right-to-left paragraph
    const stb::font r = rects();
    ASSERT_TRUE(clusters(r.shape("A!")) == (U{0, 1}));
    ASSERT_TRUE(clusters(r.shape("A!", with_direction(stb::text_direction::ltr))) == (U{0, 1}));
    ASSERT_TRUE(clusters(r.shape("A!", with_direction(stb::text_direction::rtl))) == (U{1, 0}));
}

UTEST(libstb_font_shape_tests, test_the_ligatures_option_stops_the_optional_ligatures_only) {
    const stb::font f = gsub_font();
    stb::shape_options plain;
    plain.ligatures = false;
    ASSERT_TRUE(glyphs(f.shape("\u0627\u0644\u0644\u0647")) == (U{240}));  // liga: the Allah glyph
    const std::vector<stb::shaped_glyph> letters = f.shape("\u0627\u0644\u0644\u0647", plain);
    ASSERT_EQ(std::size_t(4), letters.size());
    for (const stb::shaped_glyph& s : letters) {
        ASSERT_TRUE(s.glyph != 240);
    }
    ASSERT_TRUE(clusters(letters) == (U{6, 4, 2, 0}));
    // lam + alef is required (rlig), so it stays one glyph
    ASSERT_EQ(std::size_t(1), f.shape("\u0644\u0627", plain).size());
    ASSERT_EQ(std::size_t(3), f.shape("\u0633\u0644\u0627\u0645", plain).size());
}

UTEST(libstb_font_shape_tests, test_marks_take_no_advance_and_stack_on_the_letter) {
    const stb::font f = marks_font();
    const float gap = 0.05f * float(f.design_metrics().units_per_em);

    const std::vector<stb::shaped_glyph> above = f.shape("\u0628\u064E");  // beh + fatha
    ASSERT_EQ(std::size_t(2), above.size());
    const stb::glyph_metrics base = f.glyph_info(above[0].glyph), fatha = f.glyph_info(above[1].glyph);
    ASSERT_TRUE(base.has_outline && fatha.has_outline);
    ASSERT_TRUE(above[0].x_advance == base.advance);  // the mark does not widen the letter
    ASSERT_TRUE(above[1].x_advance == 0.0f);
    ASSERT_TRUE(clusters(above) == (U{0, 2}));
    ASSERT_TRUE(close_to(fatha.y_min + above[1].y_offset, base.y_max + gap));  // gap above the ink of the letter
    // centred on the ink of the letter: the mark's centre, from the letter's origin, is the letter's centre
    ASSERT_TRUE(close_to(above[0].x_advance + above[1].x_offset + 0.5f * (fatha.x_min + fatha.x_max),
                         0.5f * (base.x_min + base.x_max)));

    const std::vector<stb::shaped_glyph> below = f.shape("\u0628\u0650");  // beh + kasra
    ASSERT_EQ(std::size_t(2), below.size());
    const stb::glyph_metrics kasra = f.glyph_info(below[1].glyph);
    ASSERT_TRUE(close_to(kasra.y_max + below[1].y_offset, base.y_min - gap));

    // shadda goes first and a vowel stacks over it, whichever was typed first
    const std::vector<stb::shaped_glyph> stacked = f.shape("\u0628\u0651\u064E");
    ASSERT_EQ(std::size_t(3), stacked.size());
    const stb::glyph_metrics shadda = f.glyph_info(stacked[1].glyph);
    ASSERT_TRUE(close_to(shadda.y_min + stacked[1].y_offset, base.y_max + gap));
    ASSERT_TRUE(close_to(fatha.y_min + stacked[2].y_offset, shadda.y_max + stacked[1].y_offset + gap));
    const std::vector<stb::shaped_glyph> other = f.shape("\u0628\u064E\u0651");
    ASSERT_EQ(std::size_t(3), other.size());
    for (std::size_t i = 0; i < 3; ++i) {
        ASSERT_EQ(stacked[i].glyph, other[i].glyph);
        ASSERT_TRUE(close_to(stacked[i].y_offset, other[i].y_offset));
        ASSERT_TRUE(close_to(stacked[i].x_offset, other[i].x_offset));
    }
}

UTEST(libstb_font_shape_tests, test_malformed_utf8_is_refused) {
    const stb::font f = rects();
    ASSERT_THROWS(f.shape("A\xFF"), std::invalid_argument);
    ASSERT_THROWS(f.shape("\xC3"), std::invalid_argument);
}
