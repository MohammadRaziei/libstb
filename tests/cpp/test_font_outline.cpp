#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "test_common.hpp"
#include "util/file.hpp"

// The glyph-index API of stb::font: outlines as path commands, per-glyph and font-wide metrics in
// font units, kerning by glyph pair and the face count of a file. The fonts are the synthetic ones
// of tests/data (make_test_font.py and make_outline_test_fonts.py), whose outlines are known:
//   libstb-test.ttf         rectangles, unitsPerEm 1000, legacy `kern` (A, B) = -100
//   libstb-curves-test.ttf  TrueType quadratics, a composite, unitsPerEm 2048, kern -100 and GPOS -50
//   libstb-cff-test.otf     a CFF cubic
//   libstb-test.ttc         the first two in one collection

namespace {

using stb::path_command;
using stb::path_op;
using stb::point;

std::string data(const char* name) { return std::string(LIBSTB_TEST_DATA_DIR) + "/" + name; }
stb::font rects() { return stb::font::open(data("libstb-test.ttf")); }
stb::font curves() { return stb::font::open(data("libstb-curves-test.ttf")); }
stb::font cff() { return stb::font::open(data("libstb-cff-test.otf")); }

path_command cmd(path_op op, point a = {}, point b = {}, point c = {}) {
    path_command x;
    x.op = op, x.p[0] = a, x.p[1] = b, x.p[2] = c;
    return x;
}

bool near(float a, float b) { return a - b < 1e-4f && b - a < 1e-4f; }

}  // namespace

// ------------------------------------------------------------- font-wide metrics

UTEST(libstb_font_outline_tests, test_design_metrics_are_the_font_units_of_the_file) {
    stb::font_design_metrics d = rects().design_metrics();
    ASSERT_EQ(1000, d.units_per_em);
    ASSERT_EQ(800, d.ascent);
    ASSERT_EQ(-200, d.descent);
    ASSERT_EQ(0, d.line_gap);
    ASSERT_TRUE(d.x_min == 0 && d.y_min == 0 && d.x_max == 500 && d.y_max == 700);

    d = curves().design_metrics();  // a different grid: nothing may assume 1000 units per em
    ASSERT_EQ(2048, d.units_per_em);
    ASSERT_EQ(1600, d.ascent);
    ASSERT_EQ(-400, d.descent);
    ASSERT_EQ(100, d.line_gap);
    ASSERT_TRUE(d.x_min == 0 && d.y_min == 0 && d.x_max == 1050 && d.y_max == 1420);

    d = cff().design_metrics();
    ASSERT_EQ(1000, d.units_per_em);
    ASSERT_EQ(800, d.ascent);
    ASSERT_TRUE(d.x_max == 1000 && d.y_max == 1000);
}

UTEST(libstb_font_outline_tests, test_design_metrics_agree_with_the_pixel_metrics) {
    const stb::font f = curves();
    const stb::font_design_metrics d = f.design_metrics();
    const stb::font_metrics m = f.metrics(100.0f);
    const float scale = 100.0f / float(d.ascent - d.descent);  // metrics() scales (ascent - descent) to the pixel height
    ASSERT_TRUE(near(m.ascent, float(d.ascent) * scale));
    ASSERT_TRUE(near(m.descent, float(d.descent) * scale));
    ASSERT_TRUE(near(m.line_gap, float(d.line_gap) * scale));
}

// ----------------------------------------------------------------- glyph indices

UTEST(libstb_font_outline_tests, test_glyph_index_matches_has_glyph_and_shaping) {
    const stb::font f = rects();
    ASSERT_EQ(std::uint32_t(6), f.glyph_count());
    for (char32_t cp : {U'A', U'B', U' ', U'\u00E9', U'\U0001F600'}) {
        ASSERT_TRUE(f.glyph_index(cp) != 0);
        ASSERT_TRUE(f.has_glyph(cp));
        ASSERT_TRUE(f.glyph_index(cp) < f.glyph_count());
    }
    ASSERT_EQ(std::uint32_t(0), f.glyph_index(U'Z'));  // .notdef
    ASSERT_FALSE(f.has_glyph(U'Z'));
    ASSERT_TRUE(f.glyph_index(U'A') != f.glyph_index(U'B'));
    ASSERT_THROWS(f.glyph_index(char32_t(0x110000)), std::invalid_argument);

    const std::vector<std::uint32_t> shaped = f.shape_glyphs("AB");  // the indices shaping decides are the same ones
    ASSERT_EQ(std::size_t(2), shaped.size());
    ASSERT_EQ(f.glyph_index(U'A'), shaped[0]);
    ASSERT_EQ(f.glyph_index(U'B'), shaped[1]);
}

// ---------------------------------------------------------------------- glyph_info

UTEST(libstb_font_outline_tests, test_glyph_info_is_in_font_units) {
    const stb::font f = rects();
    stb::glyph_metrics m = f.glyph_info(f.glyph_index(U'A'));
    ASSERT_TRUE(m.advance == 600 && m.lsb == 100);
    ASSERT_TRUE(m.x_min == 100 && m.y_min == 0 && m.x_max == 500 && m.y_max == 700);
    ASSERT_TRUE(m.has_outline);

    m = f.glyph_info(f.glyph_index(U' '));  // advance but nothing to draw
    ASSERT_TRUE(m.advance == 300);
    ASSERT_FALSE(m.has_outline);
    ASSERT_TRUE(m.x_min == 0 && m.y_min == 0 && m.x_max == 0 && m.y_max == 0);

    // the same advance font::advance gives in pixels: 1000 units per (ascent - descent), 100 px -> 0.1 px per unit
    ASSERT_TRUE(near(f.advance(U'A', 100.0f), f.glyph_info(f.glyph_index(U'A')).advance * 0.1f));

    const stb::glyph_metrics v = curves().glyph_info(curves().glyph_index(U'V'));  // a composite glyph
    ASSERT_TRUE(v.advance == 1200 && v.lsb == 250);
    ASSERT_TRUE(v.x_min == 250 && v.y_min == 20 && v.x_max == 1050 && v.y_max == 1420);
}

UTEST(libstb_font_outline_tests, test_an_index_past_the_glyphs_is_nothing_not_an_error) {
    const stb::font f = rects();
    for (std::uint32_t g : {f.glyph_count(), f.glyph_count() + 1, std::uint32_t(65535), std::uint32_t(0xFFFFFFFFu)}) {
        const stb::glyph_metrics m = f.glyph_info(g);
        ASSERT_TRUE(m.advance == 0 && m.lsb == 0 && !m.has_outline);
        ASSERT_TRUE(f.outline(g).empty());
        ASSERT_TRUE(f.glyph_kerning(g, f.glyph_index(U'B')) == 0.0f);
        ASSERT_TRUE(f.glyph_kerning(f.glyph_index(U'A'), g) == 0.0f);
    }
}

// ------------------------------------------------------------------------ outlines

UTEST(libstb_font_outline_tests, test_a_rectangle_is_move_lines_and_close) {
    const stb::font f = rects();
    const std::vector<path_command> want = {cmd(path_op::move_to, {100, 0}),   cmd(path_op::line_to, {100, 700}),
                                            cmd(path_op::line_to, {500, 700}), cmd(path_op::line_to, {500, 0}),
                                            cmd(path_op::line_to, {100, 0}),   cmd(path_op::close)};
    ASSERT_TRUE(f.outline(f.glyph_index(U'A')) == want);
    ASSERT_TRUE(f.outline(f.glyph_index(U' ')).empty());
}

UTEST(libstb_font_outline_tests, test_a_truetype_quadratic_stays_a_quad) {
    const stb::font f = curves();
    const std::vector<path_command> want = {cmd(path_op::move_to, {0, 0}), cmd(path_op::line_to, {1000, 0}),
                                            cmd(path_op::quad_to, {1000, 1000}, {0, 1000}),  // control, end
                                            cmd(path_op::line_to, {0, 0}), cmd(path_op::close)};
    ASSERT_TRUE(f.outline(f.glyph_index(U'Q')) == want);
}

UTEST(libstb_font_outline_tests, test_a_contour_of_only_off_curve_points_starts_on_an_implied_point) {
    const stb::font f = curves();
    const std::vector<path_command> o = f.outline(f.glyph_index(U'O'));
    ASSERT_EQ(std::size_t(6), o.size());  // move_to, four quads, close
    ASSERT_TRUE(o.front().op == path_op::move_to);
    ASSERT_TRUE(o.back().op == path_op::close);
    std::set<std::pair<float, float>> controls, ends;
    for (std::size_t i = 1; i + 1 < o.size(); ++i) {
        ASSERT_TRUE(o[i].op == path_op::quad_to);
        controls.insert({o[i].p[0].x, o[i].p[0].y});
        ends.insert({o[i].p[1].x, o[i].p[1].y});
    }
    // the four off-curve points of the glyph are the controls; the implied on-curve points between them are where it passes
    ASSERT_TRUE((controls == std::set<std::pair<float, float>>{{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
    ASSERT_TRUE((ends == std::set<std::pair<float, float>>{{500, 0}, {1000, 500}, {500, 1000}, {0, 500}}));
    ASSERT_TRUE(o.front().p[0] == o[4].p[1]);  // closed: the last curve ends where the contour began
}

UTEST(libstb_font_outline_tests, test_a_composite_is_its_component_moved) {
    const stb::font f = curves();
    const std::vector<path_command> a = f.outline(f.glyph_index(U'A')), v = f.outline(f.glyph_index(U'V'));
    ASSERT_EQ(a.size(), v.size());
    ASSERT_TRUE(!a.empty());
    for (std::size_t i = 0; i < a.size(); ++i) {
        ASSERT_TRUE(a[i].op == v[i].op);
        for (int k = 0; k < stb::point_count(a[i].op); ++k) {
            ASSERT_TRUE(v[i].p[k].x == a[i].p[k].x + 50);
            ASSERT_TRUE(v[i].p[k].y == a[i].p[k].y + 20);
        }
    }
}

UTEST(libstb_font_outline_tests, test_a_cff_cubic_stays_a_cubic) {
    const stb::font f = cff();
    const std::vector<path_command> want = {cmd(path_op::move_to, {0, 0}), cmd(path_op::line_to, {1000, 0}),
                                            cmd(path_op::cubic_to, {1000, 500}, {500, 1000}, {0, 1000}),  // two controls, end
                                            cmd(path_op::line_to, {0, 0}), cmd(path_op::close)};
    ASSERT_TRUE(f.outline(f.glyph_index(U'C')) == want);
    ASSERT_TRUE(f.outline(f.glyph_index(U' ')).empty());
    ASSERT_FALSE(f.glyph_info(f.glyph_index(U' ')).has_outline);
    ASSERT_TRUE(f.glyph_info(f.glyph_index(U'C')).has_outline);
}

UTEST(libstb_font_outline_tests, test_every_outline_of_every_font_is_well_formed) {
    struct entry {
        stb::font font;
        bool truetype;
    };
    const std::vector<entry> fonts = {{rects(), true}, {curves(), true}, {cff(), false}};
    for (const entry& e : fonts)
        for (std::uint32_t g = 0; g < e.font.glyph_count(); ++g) {
            const std::vector<path_command> o = e.font.outline(g);
            const stb::glyph_metrics m = e.font.glyph_info(g);
            ASSERT_EQ(m.has_outline, !o.empty());
            if (o.empty()) continue;
            ASSERT_TRUE(o.front().op == path_op::move_to);
            ASSERT_TRUE(o.back().op == path_op::close);
            bool open = false;
            for (const path_command& c : o) {
                // the points a command does not use are zero
                for (int k = stb::point_count(c.op); k < 3; ++k) ASSERT_TRUE(c.p[k] == point{});
                if (c.op == path_op::move_to) {
                    ASSERT_FALSE(open);
                    open = true;
                } else if (c.op == path_op::close) {
                    ASSERT_TRUE(open);
                    open = false;
                } else {
                    ASSERT_TRUE(open);
                }
                ASSERT_TRUE(e.truetype ? c.op != path_op::cubic_to : c.op != path_op::quad_to);  // the two are never mixed up
                if (c.op != path_op::close) {  // the point the command arrives at is inside the ink box
                    const point end = c.p[stb::point_count(c.op) - 1];
                    ASSERT_TRUE(end.x >= m.x_min && end.x <= m.x_max && end.y >= m.y_min && end.y <= m.y_max);
                }
            }
            ASSERT_FALSE(open);
        }
}

UTEST(libstb_font_outline_tests, test_the_buffer_form_reuses_the_callers_vector) {
    const stb::font f = curves();
    std::vector<path_command> buffer(50, cmd(path_op::cubic_to, {9, 9}, {9, 9}, {9, 9}));  // old contents must not survive
    const std::size_t capacity = buffer.capacity();
    f.outline(f.glyph_index(U'Q'), buffer);
    ASSERT_TRUE(buffer == f.outline(f.glyph_index(U'Q')));
    ASSERT_TRUE(buffer.capacity() >= capacity);  // not shrunk: that is the point of the form
    f.outline(f.glyph_index(U' '), buffer);
    ASSERT_TRUE(buffer.empty());
}

UTEST(libstb_font_outline_tests, test_path_command_helpers) {
    ASSERT_EQ(1, stb::point_count(path_op::move_to));
    ASSERT_EQ(1, stb::point_count(path_op::line_to));
    ASSERT_EQ(2, stb::point_count(path_op::quad_to));
    ASSERT_EQ(3, stb::point_count(path_op::cubic_to));
    ASSERT_EQ(0, stb::point_count(path_op::close));
    ASSERT_TRUE((point{1, 2} == point{1, 2}) && (point{1, 2} != point{1, 3}));
    ASSERT_TRUE(cmd(path_op::line_to, {1, 2}) == cmd(path_op::line_to, {1, 2}));
    ASSERT_TRUE(cmd(path_op::line_to, {1, 2}) != cmd(path_op::move_to, {1, 2}));
    ASSERT_TRUE(cmd(path_op::quad_to, {1, 2}, {3, 4}) != cmd(path_op::quad_to, {1, 2}, {3, 5}));
}

// ------------------------------------------------------------------------ kerning

UTEST(libstb_font_outline_tests, test_kerning_modes_pick_gpos_or_the_kern_table) {
    const stb::font f = curves();  // `kern` says -100, GPOS says -50
    const std::uint32_t a = f.glyph_index(U'A'), b = f.glyph_index(U'B');
    ASSERT_TRUE(f.glyph_kerning(a, b) == -50.0f);  // automatic is the default: GPOS first
    ASSERT_TRUE(f.glyph_kerning(a, b, stb::kerning_mode::automatic) == -50.0f);
    ASSERT_TRUE(f.glyph_kerning(a, b, stb::kerning_mode::kern_table) == -100.0f);
    ASSERT_TRUE(f.glyph_kerning(a, b, stb::kerning_mode::none) == 0.0f);
    ASSERT_TRUE(f.glyph_kerning(b, a) == 0.0f);  // only the (A, B) pair is kerned
    // kerning() is the automatic mode in pixels
    ASSERT_TRUE(near(f.kerning(U'A', U'B', 100.0f), -50.0f * 100.0f / 2000.0f));

    const stb::font r = rects();  // only a `kern` table: every mode but none finds it
    const std::uint32_t ra = r.glyph_index(U'A'), rb = r.glyph_index(U'B');
    ASSERT_TRUE(r.glyph_kerning(ra, rb) == -100.0f);
    ASSERT_TRUE(r.glyph_kerning(ra, rb, stb::kerning_mode::kern_table) == -100.0f);
    ASSERT_TRUE(r.glyph_kerning(ra, rb, stb::kerning_mode::none) == 0.0f);
    ASSERT_TRUE(near(r.kerning(U'A', U'B', 100.0f), r.glyph_kerning(ra, rb) * 0.1f));

    const stb::font c = cff();  // no kerning at all
    ASSERT_TRUE(c.glyph_kerning(c.glyph_index(U'C'), c.glyph_index(U'C')) == 0.0f);
}

// ---------------------------------------------------------------------- face_count

UTEST(libstb_font_outline_tests, test_face_count_of_a_font_and_of_a_collection) {
    ASSERT_EQ(1, stb::font::face_count(data("libstb-test.ttf")));
    ASSERT_EQ(1, stb::font::face_count(data("libstb-cff-test.otf")));
    ASSERT_EQ(2, stb::font::face_count(data("libstb-test.ttc")));

    const std::vector<std::uint8_t> bytes = stb::detail::read_file(data("libstb-test.ttc"));
    ASSERT_EQ(2, stb::font::face_count(bytes));
    const stb::font first = stb::font::from_memory(bytes, 0), second = stb::font::from_memory(bytes, 1);
    ASSERT_EQ(std::uint32_t(6), first.glyph_count());   // libstb-test.ttf
    ASSERT_EQ(std::uint32_t(7), second.glyph_count());  // libstb-curves-test.ttf
    ASSERT_EQ(2048, second.design_metrics().units_per_em);
    ASSERT_THROWS(stb::font::from_memory(bytes, 2), stb::decode_error);  // the index stays below face_count
    ASSERT_EQ(std::uint32_t(7), stb::font::open(data("libstb-test.ttc"), 1).glyph_count());
}

UTEST(libstb_font_outline_tests, test_face_count_rejects_what_is_not_a_font) {
    const std::string junk = "this is not a font, nor a collection of fonts";
    ASSERT_THROWS(stb::font::face_count(std::vector<std::uint8_t>(junk.begin(), junk.end())), stb::decode_error);
    ASSERT_THROWS(stb::font::face_count(std::vector<std::uint8_t>()), stb::decode_error);
    ASSERT_THROWS(stb::font::face_count(std::filesystem::path("/definitely/missing.ttc")), stb::io_error);
}
