#include <atomic>
#include <cmath>
#include <thread>

#include "test_common.hpp"

// The test font (tests/data/make_test_font.py) has solid-rectangle glyphs and
// unitsPerEm=1000, ascent=800, descent=-200, so at pixel_height=100 one font
// unit is exactly 0.1 px and every expectation below is exact:
//   A: advance 60, ink x 10..50, y -70..0   (40 x 70)
//   B: advance 50, ink x 10..40, y -35..0   (30 x 35)
//   space: advance 30, no ink;  legacy kern (A,B) = -10 px

namespace {

stb::font load() { return stb::font::open(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-test.ttf"); }

bool all_255(const stb::image& img) {
    for (auto v : img.pixels())
        if (v != 255) return false;
    return true;
}

}  // namespace

UTEST(libstb_font_tests, test_metrics) {
    const stb::font_metrics m = load().metrics(100);
    ASSERT_EQ(80.0f, m.ascent);
    ASSERT_EQ(-20.0f, m.descent);
    ASSERT_EQ(0.0f, m.line_gap);
    ASSERT_EQ(100.0f, m.line_height());
}

UTEST(libstb_font_tests, test_glyph_lookup_advance_and_kerning) {
    const stb::font f = load();
    ASSERT_TRUE(f.has_glyph(U'A'));
    ASSERT_TRUE(f.has_glyph(0xE9));
    ASSERT_TRUE(f.has_glyph(0x1F600));
    ASSERT_FALSE(f.has_glyph(U'Z'));
    ASSERT_EQ(60.0f, f.advance(U'A', 100));
    ASSERT_EQ(50.0f, f.advance(U'B', 100));
    ASSERT_EQ(30.0f, f.advance(U' ', 100));
    ASSERT_EQ(-10.0f, f.kerning(U'A', U'B', 100));
    ASSERT_EQ(0.0f, f.kerning(U'B', U'A', 100));
}

UTEST(libstb_font_tests, test_render_glyph_is_an_exact_rectangle) {
    const stb::glyph a = load().render_glyph(U'A', 100);
    ASSERT_EQ(40, a.bitmap.width());
    ASSERT_EQ(70, a.bitmap.height());
    ASSERT_EQ(1, a.bitmap.channels());
    ASSERT_TRUE(all_255(a.bitmap));
    ASSERT_EQ(10, a.x_offset);
    ASSERT_EQ(-70, a.y_offset);
    ASSERT_EQ(60.0f, a.advance);

    const stb::glyph b = load().render_glyph(U'B', 100);
    ASSERT_EQ(30, b.bitmap.width());
    ASSERT_EQ(35, b.bitmap.height());
    ASSERT_EQ(-35, b.y_offset);
}

UTEST(libstb_font_tests, test_blank_glyph_has_no_bitmap) {
    const stb::glyph sp = load().render_glyph(U' ', 100);
    ASSERT_TRUE(sp.bitmap.empty());
    ASSERT_EQ(30.0f, sp.advance);
}

UTEST(libstb_font_tests, test_missing_glyph_falls_back_to_notdef) {
    const stb::glyph z = load().render_glyph(U'Z', 100);  // not in the font
    ASSERT_EQ(40, z.bitmap.width());                          // .notdef ink is 50..450 units
    ASSERT_EQ(50.0f, z.advance);
}

UTEST(libstb_font_tests, test_measure) {
    const stb::font f = load();
    const stb::text_size ab = f.measure("AB", 100);
    ASSERT_EQ(100.0f, ab.width);  // 60 - 10 (kern) + 50
    ASSERT_EQ(100.0f, ab.height);
    ASSERT_EQ(1, ab.lines);
    const stb::text_size two = f.measure("A\nAB", 100);
    ASSERT_EQ(100.0f, two.width);
    ASSERT_EQ(200.0f, two.height);
    ASSERT_EQ(2, two.lines);
    ASSERT_EQ(40.0f, f.measure("\xC3\xA9", 100).width);      // é: 2-byte UTF-8
    ASSERT_EQ(70.0f, f.measure("\xF0\x9F\x98\x80", 100).width);  // 😀: 4-byte UTF-8
}

UTEST(libstb_font_tests, test_render_places_ink_exactly) {
    const stb::text_bitmap t = load().render("AB", 100);
    const stb::image& im = t.bitmap;
    ASSERT_EQ(100, im.width());
    ASSERT_EQ(100, im.height());
    ASSERT_EQ(0, t.origin_x);
    ASSERT_EQ(0, t.origin_y);
    // A: x 10..49, y 10..79
    ASSERT_EQ(255, im(10, 10));
    ASSERT_EQ(255, im(49, 79));
    ASSERT_EQ(0, im(9, 10));
    ASSERT_EQ(0, im(50, 79));
    ASSERT_EQ(0, im(10, 9));
    // B: pen 50 (60 - 10 kern) + 10 = x 60..89, y 45..79
    ASSERT_EQ(255, im(60, 45));
    ASSERT_EQ(255, im(89, 79));
    ASSERT_EQ(0, im(59, 45));
    ASSERT_EQ(0, im(60, 44));
}

UTEST(libstb_font_tests, test_render_multiline) {
    const stb::text_bitmap t = load().render("A\nA", 100);
    ASSERT_EQ(200, t.bitmap.height());
    ASSERT_EQ(255, t.bitmap(10, 10));    // first line
    ASSERT_EQ(255, t.bitmap(10, 110));   // second line: one line_height (100) lower
    ASSERT_EQ(0, t.bitmap(10, 100));
}

UTEST(libstb_font_tests, test_render_errors) {
    const stb::font f = load();
    ASSERT_THROWS(f.render("", 100), std::invalid_argument);
    ASSERT_THROWS(f.render("\xFF", 100), std::invalid_argument);   // malformed UTF-8
    ASSERT_THROWS(f.render("A", 100, 10), stb::limit_error);    // 60x100 > 10 bytes
}

UTEST(libstb_font_tests, test_pixel_height_is_validated) {
    const stb::font f = load();
    for (float bad : {0.0f, -1.0f, 3000.0f, std::nanf(""), INFINITY}) {
        ASSERT_THROWS(f.metrics(bad), std::invalid_argument);
        ASSERT_THROWS(f.render_glyph(U'A', bad), std::invalid_argument);
        ASSERT_THROWS(f.measure("A", bad), std::invalid_argument);
    }
}

UTEST(libstb_font_tests, test_atlas) {
    const stb::atlas a = load().make_atlas(U"AAB", 100, 128, 128);  // duplicate 'A' ignored
    ASSERT_EQ(2u, a.glyphs().size());
    ASSERT_EQ(128, a.bitmap().width());
    ASSERT_EQ(1, a.bitmap().channels());

    const stb::atlas_glyph* g = a.find(U'A');
    ASSERT_TRUE(g != nullptr);
    ASSERT_EQ(40, g->x1 - g->x0);
    ASSERT_EQ(70, g->y1 - g->y0);
    ASSERT_EQ(10.0f, g->xoff);
    ASSERT_EQ(-70.0f, g->yoff);
    ASSERT_EQ(60.0f, g->advance);
    for (int y = g->y0; y < g->y1; ++y)
        for (int x = g->x0; x < g->x1; ++x) ASSERT_EQ(255, a.bitmap()(x, y));
    ASSERT_TRUE(a.find(U'B') != nullptr);
    ASSERT_TRUE(a.find(U'Z') == nullptr);
}

UTEST(libstb_font_tests, test_atlas_errors) {
    const stb::font f = load();
    ASSERT_THROWS(f.make_atlas(U"AB", 100, 16, 16), stb::limit_error);  // does not fit
    ASSERT_THROWS(f.make_atlas(U"", 100, 64, 64), std::invalid_argument);
    ASSERT_THROWS(f.make_atlas(U"A", 100, 0, 64), std::invalid_argument);
    ASSERT_THROWS(f.make_atlas(U"A", 100, 64, 64, -1), std::invalid_argument);
}

UTEST(libstb_font_tests, test_loading_errors) {
    const std::string junk = "definitely not a font";
    ASSERT_THROWS(stb::font::from_memory({junk.begin(), junk.end()}), stb::decode_error);
    ASSERT_THROWS(stb::font::from_memory({}), stb::decode_error);
    ASSERT_THROWS(stb::font::from_memory({junk.begin(), junk.end()}, -1), std::invalid_argument);
    ASSERT_THROWS(stb::font::open("/definitely/missing.ttf"), stb::io_error);
    // a single-face file has no face #1
    ASSERT_THROWS(stb::font::open(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-test.ttf", 1), stb::decode_error);
}

UTEST(libstb_font_tests, test_copies_share_the_font_and_outlive_the_original) {
    stb::font copy = [] {
        stb::font original = load();
        return original;  // original destroyed here
    }();
    ASSERT_EQ(60.0f, copy.advance(U'A', 100));
}

UTEST(libstb_font_tests, test_concurrent_rendering) {
    const stb::font f = load();
    const stb::text_bitmap ref = f.render("AB\nBA", 100);
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t)
        ts.emplace_back([&] {
            for (int i = 0; i < 200; ++i)
                if (f.render("AB\nBA", 100).bitmap.pixels() != ref.bitmap.pixels()) ++bad;
        });
    for (auto& th : ts) th.join();
    ASSERT_EQ(0, bad.load());
}
