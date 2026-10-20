#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

#include "test_common.hpp"
#include "stb/canvas.hpp"

// The test font (tests/data/make_test_font.py) has solid-rectangle glyphs and
// unitsPerEm=1000, ascent=800, descent=-200, so at pixel_height=100 one font
// unit is exactly 0.1 px and every expectation below is exact:
//   A: advance 60, ink x 10..50, y -70..0   (40 x 70)
//   B: advance 50, ink x 10..40, y -35..0   (30 x 35)
//   space: advance 30, no ink;  legacy kern (A,B) = -10 px

namespace {

stb::font load() { return stb::font::open(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-test.ttf"); }

bool all_255(const stb::image& img) {
    for (auto v : img)
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
                if (!testutil::same(f.render("AB\nBA", 100).bitmap, ref.bitmap)) ++bad;
        });
    for (auto& th : ts) th.join();
    ASSERT_EQ(0, bad.load());
}

// ---------------------------------------------------------------- estimates
// Widths are 1/2048 em from the Liberation tables. For sans, ascent - descent is 2288 units, so at
// pixel_height = 100 one unit is 100 / 2288 px (the same scale font::measure uses).

UTEST(libstb_font_estimate_tests, test_sans_widths_are_exact_table_values) {
    // 'i' = 455 units, 'W' = 1933 units, ' ' = 569 units
    const float s = 100.0f / 2288.0f;
    ASSERT_NEAR(455 * s, stb::estimate_text("i", 100).width, 1e-3f);
    ASSERT_NEAR((1933 + 455 + 569) * s, stb::estimate_text("Wi ", 100).width, 1e-3f);
}

UTEST(libstb_font_estimate_tests, test_mono_is_fixed_pitch) {
    const float one = stb::estimate_text("i", 50, stb::font_family::mono).width;
    ASSERT_NEAR(one, stb::estimate_text("W", 50, stb::font_family::mono).width, 1e-4f);
    ASSERT_NEAR(one * 5, stb::estimate_text("hello", 50, stb::font_family::mono).width, 1e-3f);
}

UTEST(libstb_font_estimate_tests, test_lines_height_and_metrics) {
    const stb::font_metrics m = stb::estimate_metrics(100);
    const stb::text_size two = stb::estimate_text("ab\r\nabcd", 100);
    ASSERT_EQ(2, two.lines);
    ASSERT_NEAR(2 * m.line_height(), two.height, 1e-3f);
    ASSERT_NEAR(stb::estimate_text("abcd", 100).width, two.width, 1e-4f);  // widest line, '\r' ignored
    ASSERT_NEAR(100.0f, m.ascent - m.descent, 1e-3f);                       // em height = pixel_height
    ASSERT_EQ(1, stb::estimate_text("", 100).lines);
    ASSERT_EQ(0.0f, stb::estimate_text("", 100).width);
}

UTEST(libstb_font_estimate_tests, test_non_latin_rules) {
    const float em = 100.0f * 2048.0f / 2288.0f;  // 1 em = 2048 units
    ASSERT_NEAR(em, stb::estimate_text("\xE4\xB8\xAD", 100).width, 1e-3f);          // CJK: 1 em
    ASSERT_NEAR(em, stb::estimate_text("\xF0\x9F\x98\x80", 100).width, 1e-3f);      // emoji: 1 em
    ASSERT_EQ(0.0f, stb::estimate_text("\xCC\x81", 100).width);                     // combining acute: 0
    ASSERT_TRUE(stb::estimate_text("\xD0\x96", 100).width > 0.0f);                  // Cyrillic: average
    ASSERT_NEAR(stb::estimate_text("\xE2\x80\x94", 100).width, 100.0f * 2048 / 2288, 1e-2f);  // em dash
}

UTEST(libstb_font_estimate_tests, test_errors) {
    ASSERT_THROWS(stb::estimate_text("\xFF", 16), std::invalid_argument);       // malformed UTF-8
    for (float bad : {0.0f, -1.0f, 3000.0f, std::nanf(""), INFINITY}) {
        ASSERT_THROWS(stb::estimate_text("A", bad), std::invalid_argument);
        ASSERT_THROWS(stb::estimate_metrics(bad), std::invalid_argument);
    }
}

// --- draw_text: at 100 px the test font's "A" is a solid rectangle, x 10..49 and y 10..79 of the box.

namespace {
stb::image filled(int w, int h, int c, std::uint8_t v) {
    return stb::image(w, h, c, std::vector<std::uint8_t>(std::size_t(w) * std::size_t(h) * std::size_t(c), v));
}
const std::uint8_t* px(const stb::image& im, int x, int y) {
    return im.data() + std::size_t(y) * im.stride() + std::size_t(x) * std::size_t(im.channels());
}
}  // namespace

UTEST(libstb_font_tests, test_draw_text_paints_the_glyph_where_asked) {
    const stb::image white = filled(200, 200, 3, 255);
    const stb::image out = load().draw_text(white, "A", 100, 20, 30, stb::color(255, 0, 0));
    ASSERT_EQ(3, out.channels());
    // The glyph covers x 30..69, y 40..109.
    ASSERT_EQ(255, px(out, 30, 40)[0]);
    ASSERT_EQ(0, px(out, 30, 40)[1]);
    ASSERT_EQ(0, px(out, 69, 109)[2]);
    ASSERT_EQ(255, px(out, 29, 40)[1]);   // untouched around it
    ASSERT_EQ(255, px(out, 30, 39)[1]);
    ASSERT_EQ(255, px(out, 70, 109)[1]);
    ASSERT_EQ(255, px(white, 30, 40)[1]);  // the source is not modified
}

UTEST(libstb_font_tests, test_draw_text_alpha_scales_the_coverage) {
    const stb::image out = load().draw_text(filled(100, 100, 3, 0), "A", 100, 0, 0, stb::color(200, 200, 200, 128));
    ASSERT_EQ(100, px(out, 10, 10)[0]);   // 200 * 128 / 255 over black
}

UTEST(libstb_font_tests, test_draw_text_gray_and_rgba_destinations) {
    const stb::font f = load();
    const stb::image gray = f.draw_text(filled(100, 100, 1, 0), "A", 100, 0, 0, stb::color(255, 255, 255));
    ASSERT_EQ(1, gray.channels());
    ASSERT_EQ(255, gray(10, 10));
    ASSERT_EQ(0, gray(9, 10));
    const stb::image rgba = f.draw_text(filled(100, 100, 4, 0), "A", 100, 0, 0, stb::color(10, 20, 30));
    ASSERT_EQ(4, rgba.channels());
    ASSERT_EQ(30, px(rgba, 10, 10)[2]);
    ASSERT_EQ(255, px(rgba, 10, 10)[3]);  // opaque text on transparent black
    ASSERT_EQ(0, px(rgba, 9, 10)[3]);
}

UTEST(libstb_font_tests, test_draw_text_is_clipped_at_the_edges) {
    const stb::font f = load();
    const stb::image white = filled(50, 50, 1, 255);
    const stb::image part = f.draw_text(white, "A", 100, -30, -10, stb::color());
    ASSERT_EQ(0, part(0, 0));      // glyph x -20..19, y 0..69: its right part lands in the image
    ASSERT_EQ(0, part(19, 49));
    ASSERT_EQ(255, part(20, 0));
    const stb::image away = f.draw_text(white, "A", 100, 500, 500, stb::color());
    ASSERT_EQ(255, away(25, 25));  // fully outside: an unchanged copy
}

UTEST(libstb_font_tests, test_draw_text_errors) {
    const stb::font f = load();
    ASSERT_THROWS(f.draw_text(filled(10, 10, 3, 0), "", 100, 0, 0), std::invalid_argument);
    ASSERT_THROWS(f.draw_text(stb::image(), "A", 100, 0, 0), std::invalid_argument);
    ASSERT_THROWS(f.draw_text(filled(10, 10, 3, 0), "A", 0, 0, 0), std::invalid_argument);
}

UTEST(libstb_font_tests, test_draw_text_takes_named_and_hex_colors) {
    const stb::font f = load();
    using namespace stb::color_literals;
    const stb::image a = f.draw_text(filled(100, 100, 3, 0), "A", 100, 0, 0, "red"_c);
    ASSERT_EQ(255, px(a, 10, 10)[0]);
    ASSERT_EQ(0, px(a, 10, 10)[1]);
    const stb::image b = f.draw_text(filled(100, 100, 3, 0), "A", 100, 0, 0, stb::color("#0a141e"));
    ASSERT_EQ(10, px(b, 10, 10)[0]);
    ASSERT_EQ(30, px(b, 10, 10)[2]);
}

UTEST(libstb_font_tests, test_canvas_draws_many_texts_on_one_copy) {
    const stb::image white = filled(300, 100, 3, 255);
    stb::canvas c(white);
    c.set_font(load(), 100).set_color("red");
    c.add_text("A", 0, 0);
    c.add_text("A", 100, 0).color(stb::color(0, 0, 255));      // this text only
    c.add_text("A", 200, 0);
    const stb::image out = c.finish();
    ASSERT_EQ(300, out.width());
    ASSERT_EQ(255, px(out, 10, 10)[0]);                              // red
    ASSERT_EQ(0, px(out, 10, 10)[1]);
    ASSERT_EQ(255, px(out, 110, 10)[2]);                             // blue
    ASSERT_EQ(0, px(out, 110, 10)[0]);
    ASSERT_EQ(0, px(out, 210, 10)[1]);                               // back to red
    ASSERT_EQ(255, px(white, 10, 10)[1]);                            // the input image is untouched
}

UTEST(libstb_font_tests, test_canvas_text_takes_the_defaults_when_it_is_added) {
    const stb::image white = filled(200, 100, 3, 255);
    stb::canvas c(white);
    c.set_font(load(), 100).set_color("red");
    c.add_text("A", 0, 0);
    c.set_color("blue");                                             // later: must not touch the first text
    c.add_text("A", 100, 0);
    const stb::image out = c.finish();
    ASSERT_EQ(255, px(out, 10, 10)[0]);
    ASSERT_EQ(0, px(out, 10, 10)[2]);
    ASSERT_EQ(255, px(out, 110, 10)[2]);
    ASSERT_EQ(0, px(out, 110, 10)[0]);
}

UTEST(libstb_font_tests, test_canvas_matches_draw_text_and_needs_a_font) {
    const stb::image white = filled(100, 100, 3, 255);
    {
        stb::canvas c(white);
        c.add_text("A", 0, 0);
        ASSERT_THROWS(c.finish(), std::invalid_argument);   // no font anywhere
    }
    stb::canvas c(white);
    c.set_font(load(), 100).add_text("A", 5, 7).color(stb::color(10, 20, 30, 200));
    const stb::image got = c.finish();
    const stb::image once = load().draw_text(white, "A", 100, 5, 7, stb::color(10, 20, 30, 200));
    ASSERT_TRUE(std::memcmp(got.data(), once.data(), std::size_t(once.stride()) * once.height()) == 0);
    // a text can bring its own font instead of the canvas default
    stb::canvas d(white);
    d.add_text("A", 5, 7).font(load(), 100).color(stb::color(10, 20, 30, 200));
    const stb::image own = d.finish();
    ASSERT_TRUE(std::memcmp(own.data(), once.data(), std::size_t(once.stride()) * once.height()) == 0);
}

UTEST(libstb_font_tests, test_canvas_set_size_and_order) {
    const stb::image white = filled(200, 200, 1, 255);
    stb::canvas c(white);
    c.set_font(load(), 100);
    c.add_text("A", 0, 0).size(40);
    const stb::image at_40 = load().draw_text(white, "A", 40, 0, 0);
    stb::image out = c.finish();
    ASSERT_TRUE(std::memcmp(out.data(), at_40.data(), std::size_t(at_40.stride()) * at_40.height()) == 0);
    // drawn in the order added: the later text is on top (opaque white over opaque black)
    stb::canvas d(white);
    d.set_font(load(), 100);
    d.add_text("A", 0, 0).color(stb::color(0, 0, 0));
    d.add_text("A", 0, 0).color(stb::color(255, 255, 255));
    const stb::image top = d.finish();
    ASSERT_EQ(255, top(10, 10));
}

UTEST(libstb_font_tests, test_composite_in_place_matches_composite) {
    const stb::image base = filled(20, 20, 3, 100);
    const stb::image over(4, 4, 4, std::vector<std::uint8_t>(64, 200));
    stb::image a = base.copy();
    a.composite_in_place(over, 18, -2);
    const stb::image b = base.composite(over, 18, -2);
    ASSERT_TRUE(std::memcmp(a.data(), b.data(), std::size_t(b.stride()) * b.height()) == 0);
    ASSERT_EQ(100, px(base, 19, 0)[0]);              // composite() left its source alone
}

UTEST(libstb_font_tests, test_draw_text_align_puts_x_at_the_centre_or_right_edge) {
    const stb::font f = load();
    const stb::image white = filled(400, 100, 1, 255);
    const float w = f.measure("A", 100).width;
    const stb::color black;
    // The glyph's ink starts 10 px into the layout box (see above).
    const int centre0 = 200 - int(std::lround(w * 0.5f)) + 10;
    const stb::image c = f.draw_text(white, "A", 100, 200, 0, black, stb::text_align::center);
    ASSERT_EQ(0, c(centre0, 10));
    ASSERT_EQ(255, c(centre0 - 1, 10));
    const int right0 = 300 - int(std::lround(w)) + 10;
    const stb::image r = f.draw_text(white, "A", 100, 300, 0, black, stb::text_align::right);
    ASSERT_EQ(0, r(right0, 10));
    ASSERT_EQ(255, r(right0 - 1, 10));
    // left is the default
    const stb::image l = f.draw_text(white, "A", 100, 50, 0, black, stb::text_align::left);
    const stb::image d = f.draw_text(white, "A", 100, 50, 0);
    ASSERT_TRUE(std::memcmp(l.data(), d.data(), std::size_t(d.stride()) * d.height()) == 0);
}

UTEST(libstb_font_tests, test_draw_text_align_centres_every_line_on_x) {
    const stb::font f = load();
    const stb::image white = filled(400, 400, 1, 255);
    const stb::text_size all = f.measure("A\nAAA", 100);
    const float pitch = all.height / float(all.lines);
    const int row2 = int(std::lround(pitch)) + 10;
    const stb::image img = f.draw_text(white, "A\nAAA", 100, 200, 0, stb::color(), stb::text_align::center);
    const int a0 = 200 - int(std::lround(f.measure("A", 100).width * 0.5f)) + 10;
    const int a1 = 200 - int(std::lround(f.measure("AAA", 100).width * 0.5f)) + 10;
    ASSERT_TRUE(a1 < a0);                 // the longer line starts further left
    ASSERT_EQ(0, img(a0, 10));
    ASSERT_EQ(255, img(a0 - 1, 10));
    ASSERT_EQ(0, img(a1, row2));
    ASSERT_EQ(255, img(a1 - 1, row2));
    ASSERT_EQ(255, img(a1, 10));          // line 1 is not wide enough to reach there
    // a blank line only moves the next one down
    const stb::image gap = f.draw_text(white, "A\n\nA", 100, 200, 0, stb::color(), stb::text_align::center);
    ASSERT_EQ(0, gap(a0, int(std::lround(2 * f.measure("A\n\nA", 100).height / 3)) + 10));
}

UTEST(libstb_font_tests, test_canvas_align) {
    const stb::font f = load();
    stb::canvas c(filled(400, 100, 1, 255));
    c.set_font(f, 100).set_align(stb::text_align::right);
    c.add_text("A", 300, 0);                                         // the canvas default
    c.add_text("A", 100, 0).align(stb::text_align::left);        // this text only
    const stb::image out = c.finish();
    const int right0 = 300 - int(std::lround(f.measure("A", 100).width)) + 10;
    ASSERT_EQ(0, out(right0, 10));
    ASSERT_EQ(0, out(110, 10));
}


UTEST(libstb_font_tests, test_canvas_background_box_and_padding) {
    const stb::font f = load();
    const stb::text_size s = f.measure("A", 100);
    const int w = int(std::lround(s.width)), h = int(std::lround(s.height));
    stb::canvas c(filled(300, 300, 3, 255));
    c.set_font(f, 100);
    c.add_text("A", 50, 60).bg_color("red").padding(10).color(stb::color(0, 0, 255));
    const stb::image out = c.finish();
    // the box is the layout box grown by 10 on every side, in red; outside it the image is white
    ASSERT_EQ(255, px(out, 50 - 10, 60 - 10)[0]);
    ASSERT_EQ(0, px(out, 50 - 10, 60 - 10)[1]);
    ASSERT_EQ(0, px(out, 50 + w + 9, 60 + h + 9)[1]);
    ASSERT_EQ(255, px(out, 50 - 11, 60 + 5)[1]);
    ASSERT_EQ(255, px(out, 50 + w + 10, 60 + 5)[1]);
    ASSERT_EQ(255, px(out, 50 + 5, 60 - 11)[1]);
    ASSERT_EQ(255, px(out, 50 + 5, 60 + h + 10)[1]);
    // the text itself is drawn over the background
    ASSERT_EQ(255, px(out, 60, 70)[2]);
    ASSERT_EQ(0, px(out, 60, 70)[0]);
}

UTEST(libstb_font_tests, test_canvas_background_follows_align_and_clips) {
    const stb::font f = load();
    const int w = int(std::lround(f.measure("A", 100).width));
    stb::canvas c(filled(400, 200, 1, 255));
    c.set_font(f, 100);
    c.add_text("A", 300, 0).align(stb::text_align::right).bg_color(stb::color(0, 0, 0)).padding(5);
    c.add_text("A", 0, 0).bg_color(stb::color(0, 0, 0)).padding(1000).color(stb::color(0, 0, 0));  // clipped, no huge allocation
    const stb::image out = c.finish();
    ASSERT_EQ(0, out(0, 0));
    ASSERT_EQ(0, out(399, 199));
    stb::canvas d(filled(400, 200, 1, 255));
    d.set_font(f, 100);
    d.add_text("A", 300, 20).align(stb::text_align::right).bg_color(stb::color(0, 0, 0)).padding(5).color(stb::color(255, 255, 255));
    const stb::image r = d.finish();
    ASSERT_EQ(0, r(300 - w - 5, 20 - 5));
    ASSERT_EQ(255, r(300 - w - 6, 20 + 3));
    ASSERT_EQ(0, r(300 + 4, 20 + 3));
    ASSERT_EQ(255, r(300 + 5, 20 + 3));
}

UTEST(libstb_font_tests, test_canvas_font_without_size_keeps_the_size_and_finish_is_final) {
    const stb::image white = filled(100, 100, 3, 255);
    stb::canvas c(white);
    c.set_size(100);
    c.add_text("A", 5, 7).font(load());                    // size from the canvas
    auto& kept = c.add_text("B", 0, 0);
    ASSERT_THROWS(kept.padding(-1), std::invalid_argument);
    stb::canvas c2(white);
    c2.set_font(load(), 100).add_text("A", 5, 7);
    const stb::image once = load().draw_text(white, "A", 100, 5, 7);
    stb::image a = c2.finish();
    ASSERT_TRUE(std::memcmp(a.data(), once.data(), std::size_t(once.stride()) * once.height()) == 0);
    ASSERT_THROWS(c2.add_text("A", 0, 0), std::invalid_argument);
    ASSERT_THROWS(c2.set_color("red"), std::invalid_argument);
    ASSERT_THROWS(c2.finish(), std::invalid_argument);
    ASSERT_TRUE(c2.finished());
}

// The same scene is built by test_canvas_matches_the_cpp_golden_hash in tests/python/test_font.py:
// both must reach this value, so C++ and Python draw byte-identical pixels.
UTEST(libstb_font_tests, test_canvas_matches_the_python_golden_hash) {
    const stb::font f = load();
    stb::canvas c(filled(200, 120, 3, 128));
    c.set_font(f, 40).set_color("white");
    c.add_text("AA", 10, 5);
    c.add_text("A\nAAA", 100, 5).align(stb::text_align::center).color("#ffcc00").bg_color("#222").padding(6);
    c.add_text("AA", 190, 70).align(stb::text_align::right).color({255, 255, 255, 120}).bg_color({0, 0, 0, 90}).padding(4).size(24);
    c.add_text("A", 5, 80).font(f).size(30).color("red");
    const stb::image out = c.finish();
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (int row = 0; row < out.height(); ++row) {
        const std::uint8_t* p = out.data() + std::size_t(row) * out.stride();
        for (int i = 0; i < out.width() * out.channels(); ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    }
    ASSERT_TRUE(h == 0xd7f088b011c4d0ddull);
}

// ---- shapes on a canvas ----

static bool is_color(const stb::image& im, int x, int y, int r, int g, int b) {
    const std::uint8_t* p = px(im, x, y);
    return p[0] == r && p[1] == g && p[2] == b;
}

UTEST(libstb_shape_tests, test_rect_has_sharp_edges_and_defaults_to_the_canvas_colour) {
    stb::canvas c(filled(30, 30, 3, 255));
    c.set_color("blue");
    c.add_rect(5, 5, 10, 8);                                   // filled with the canvas colour
    c.add_rect(20, 20, 5, 5).fill("red");
    const stb::image out = c.finish();
    ASSERT_TRUE(is_color(out, 5, 5, 0, 0, 255) && is_color(out, 14, 12, 0, 0, 255));
    ASSERT_TRUE(is_color(out, 4, 5, 255, 255, 255) && is_color(out, 15, 5, 255, 255, 255));
    ASSERT_TRUE(is_color(out, 5, 4, 255, 255, 255) && is_color(out, 5, 13, 255, 255, 255));
    ASSERT_TRUE(is_color(out, 20, 20, 255, 0, 0) && is_color(out, 24, 24, 255, 0, 0));
}

UTEST(libstb_shape_tests, test_stroke_is_drawn_inside_the_box) {
    stb::canvas c(filled(30, 30, 3, 255));
    c.add_rect(0, 0, 20, 20).no_fill().stroke(3, "black");
    const stb::image out = c.finish();
    ASSERT_TRUE(is_color(out, 0, 10, 0, 0, 0) && is_color(out, 2, 10, 0, 0, 0));
    ASSERT_TRUE(is_color(out, 3, 10, 255, 255, 255) && is_color(out, 10, 10, 255, 255, 255));   // not filled
    ASSERT_TRUE(is_color(out, 19, 10, 0, 0, 0) && is_color(out, 20, 10, 255, 255, 255));        // not outside the box
    ASSERT_TRUE(is_color(out, 10, 0, 0, 0, 0) && is_color(out, 10, 19, 0, 0, 0));
}

UTEST(libstb_shape_tests, test_rounded_corners_and_circle_area) {
    stb::canvas c(filled(40, 40, 3, 255));
    c.set_color("black");
    c.add_rect(0, 0, 20, 20).radius(10);                       // a circle
    c.add_ellipse(20, 0, 20, 20);
    const stb::image out = c.finish();
    ASSERT_TRUE(is_color(out, 0, 0, 255, 255, 255));            // the corner is cut away
    ASSERT_TRUE(is_color(out, 10, 10, 0, 0, 0));
    bool soft = false;
    double rounded = 0, ellipse = 0;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            const int a = px(out, x, y)[0], b = px(out, x + 20, y)[0];
            soft = soft || (a > 10 && a < 245);
            rounded += (255 - a) / 255.0;
            ellipse += (255 - b) / 255.0;
        }
    }
    ASSERT_TRUE(soft);                                          // edge pixels are partly covered
    const double area = 3.14159265358979 * 10 * 10;
    ASSERT_TRUE(std::fabs(rounded - area) < 2.0);               // the soft edge keeps the area right
    ASSERT_TRUE(std::fabs(ellipse - area) < 2.0);
}

UTEST(libstb_shape_tests, test_line_width_alpha_clipping_and_order) {
    stb::canvas c(filled(20, 12, 3, 255));
    c.set_color("black");
    c.add_line(2, 5, 17, 5);                                    // width 1: exactly row 5
    c.add_line(2, 8, 17, 8).stroke(3, stb::color(0, 0, 0, 128));
    const stb::image out = c.finish();
    ASSERT_TRUE(is_color(out, 2, 5, 0, 0, 0) && is_color(out, 17, 5, 0, 0, 0));
    ASSERT_TRUE(is_color(out, 1, 5, 255, 255, 255) && is_color(out, 18, 5, 255, 255, 255));
    ASSERT_TRUE(is_color(out, 10, 4, 255, 255, 255) && is_color(out, 10, 6, 255, 255, 255));
    for (int row = 7; row <= 9; ++row) {                                    // translucent, 3 px wide: rows 7 to 9
        ASSERT_TRUE(px(out, 10, row)[0] >= 126 && px(out, 10, row)[0] <= 128);
    }
    ASSERT_TRUE(is_color(out, 10, 6, 255, 255, 255) && is_color(out, 10, 10, 255, 255, 255));
    // an even width straddles pixel rows: width 2 at row 8 covers half of rows 7 and 9
    stb::canvas two(filled(20, 12, 3, 255));
    two.add_line(2, 8, 17, 8).stroke(2, "black");
    const stb::image t = two.finish();
    ASSERT_TRUE(is_color(t, 10, 8, 0, 0, 0));
    ASSERT_TRUE(px(t, 10, 7)[0] >= 126 && px(t, 10, 7)[0] <= 129 && px(t, 10, 9)[0] >= 126 && px(t, 10, 9)[0] <= 129);

    stb::canvas d(filled(20, 20, 3, 255));
    d.add_rect(-1000000, -1000000, 2000000, 2000000).fill("black");   // clipped, no giant layer
    d.add_rect(5, 5, 5, 5).fill("red");                                // later = on top
    const stb::image big = d.finish();
    ASSERT_TRUE(is_color(big, 0, 0, 0, 0, 0) && is_color(big, 19, 19, 0, 0, 0));
    ASSERT_TRUE(is_color(big, 5, 5, 255, 0, 0));
    stb::canvas e(filled(20, 20, 3, 255));
    e.add_rect(100, 100, 5, 5);                                         // entirely outside: nothing happens
    ASSERT_TRUE(is_color(e.finish(), 0, 0, 255, 255, 255));
}

UTEST(libstb_shape_tests, test_shapes_and_texts_are_drawn_in_the_order_added_and_need_no_font) {
    const stb::font f = load();
    stb::canvas c(filled(100, 100, 3, 255));
    c.set_font(f, 100).set_color("black");
    c.add_text("A", 0, 0);
    c.add_rect(0, 0, 100, 100).fill("white");                           // covers the text
    ASSERT_TRUE(is_color(c.finish(), 10, 10, 255, 255, 255));
    stb::canvas d(filled(10, 10, 3, 255));
    d.add_rect(0, 0, 5, 5);                                             // no font needed for shapes
    ASSERT_TRUE(is_color(d.finish(), 0, 0, 0, 0, 0));                   // black: the default canvas colour
}

UTEST(libstb_shape_tests, test_errors) {
    stb::canvas c(filled(20, 20, 3, 255));
    ASSERT_THROWS(c.add_rect(0, 0, 0, 5), std::invalid_argument);
    ASSERT_THROWS(c.add_ellipse(0, 0, 5, -1), std::invalid_argument);
    auto& e = c.add_ellipse(0, 0, 5, 5);
    ASSERT_THROWS(e.radius(2), std::invalid_argument);
    ASSERT_THROWS(e.stroke(0), std::invalid_argument);
    ASSERT_THROWS(e.stroke(-1, "red"), std::invalid_argument);
    auto& r = c.add_rect(0, 0, 5, 5);
    ASSERT_THROWS(r.radius(-1), std::invalid_argument);
    auto& l = c.add_line(0, 0, 5, 5);
    ASSERT_THROWS(l.fill("red"), std::invalid_argument);
    ASSERT_THROWS(l.no_fill(), std::invalid_argument);
    ASSERT_THROWS(l.radius(1), std::invalid_argument);
    c.finish();
    ASSERT_THROWS(c.add_rect(0, 0, 5, 5), std::invalid_argument);
    ASSERT_THROWS(r.fill("red"), std::invalid_argument);
}

// Shapes and texts together; test_canvas_shapes_match_the_cpp_golden_hash in tests/python/test_font.py
// builds the same scene and must reach the same value.
UTEST(libstb_shape_tests, test_canvas_shapes_match_the_python_golden_hash) {
    const stb::font f = load();
    stb::canvas c(filled(200, 120, 3, 128));
    c.set_font(f, 30).set_color("white");
    c.add_rect(10, 10, 80, 50).fill("#224488").radius(12).stroke(3, "white");
    c.add_ellipse(100, 10, 90, 50).fill({255, 200, 0, 160}).stroke(2, {0, 0, 0, 200});
    c.add_line(5, 100, 195, 70).stroke(3, "red");
    c.add_line(5, 110, 195, 110);
    c.add_rect(60, 75, 70, 36).no_fill().stroke(2, "black");
    c.add_text("AA", 70, 80).color("black");
    const stb::image out = c.finish();
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (int row = 0; row < out.height(); ++row) {
        const std::uint8_t* p = out.data() + std::size_t(row) * out.stride();
        for (int i = 0; i < out.width() * out.channels(); ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    }
    ASSERT_TRUE(h == 0x49444ac71d1c9d17ull);
}
