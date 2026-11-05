#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "test_common.hpp"

// stb::rasterize_fill, rasterize_stroke, fill and stroke (stb/raster.hpp). The exact pixels of curves
// and joins are checked against plutovg in test_raster_reference.cpp; here the behaviour: what is
// inside, what the clip and the matrix do, ordering of spans, argument handling.

namespace {

using stb::clip_rect;
using stb::fill_rule;
using stb::matrix;
using stb::outline;
using stb::span;
using stb::span_view;
using stb::stroke_style;

// The coverage of an outline as a w x h image of bytes (0 where nothing is reported).
struct coverage_map {
    int w, h;
    std::vector<unsigned char> v;
    std::size_t batches = 0;
    bool ordered = true;   // spans arrived top to bottom, left to right
    bool disjoint = true;  // no pixel twice
    int last_y = -1, last_end = 0;

    coverage_map(int w_, int h_) : w(w_), h(h_), v(std::size_t(w_) * std::size_t(h_), 0) {}
    unsigned char at(int x, int y) const { return v[std::size_t(y) * std::size_t(w) + std::size_t(x)]; }

    void add(span_view view) {
        ++batches;
        for (const span& s : view) {
            if (s.coverage == 0 || s.len <= 0) disjoint = false;  // never reported
            if (s.y < last_y || (s.y == last_y && s.x < last_end)) ordered = false;
            last_y = s.y;
            last_end = s.x + s.len;
            for (int x = s.x; x < s.x + s.len; ++x) {
                if (x < 0 || x >= w || s.y < 0 || s.y >= h) {
                    ordered = false;  // outside the clip
                    continue;
                }
                if (v[std::size_t(s.y) * std::size_t(w) + std::size_t(x)] != 0) disjoint = false;
                v[std::size_t(s.y) * std::size_t(w) + std::size_t(x)] = s.coverage;
            }
        }
    }
};

coverage_map fill_map(const outline& p, fill_rule rule, int w, int h, const matrix& m = {}) {
    coverage_map c(w, h);
    stb::rasterize_fill(p, rule, m, clip_rect{0, 0, w, h}, [&](span_view v) { c.add(v); });
    return c;
}

coverage_map stroke_map(const outline& p, const stroke_style& st, int w, int h, const matrix& m = {}) {
    coverage_map c(w, h);
    stb::rasterize_stroke(p, st, m, clip_rect{0, 0, w, h}, [&](span_view v) { c.add(v); });
    return c;
}

int count_nonzero(const coverage_map& c) {
    int n = 0;
    for (unsigned char b : c.v) n += b != 0;
    return n;
}

}  // namespace

UTEST(stb_raster, a_pixel_aligned_rect_is_fully_covered_inside_and_empty_outside) {
    outline p;
    p.add_rect(4, 3, 10, 6);
    const coverage_map c = fill_map(p, fill_rule::non_zero, 20, 12);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 20; ++x) {
            const bool inside = x >= 4 && x < 14 && y >= 3 && y < 9;
            EXPECT_EQ(int(c.at(x, y)), inside ? 255 : 0);
        }
    }
    EXPECT_TRUE(c.ordered);
    EXPECT_TRUE(c.disjoint);
}

UTEST(stb_raster, edges_at_fractions_get_partial_coverage) {
    outline p;
    p.add_rect(2.5f, 2.f, 4.f, 3.f);  // x from 2.5 to 6.5
    const coverage_map c = fill_map(p, fill_rule::non_zero, 10, 8);
    EXPECT_EQ(int(c.at(2, 3)), 128);  // half of the pixel
    EXPECT_EQ(int(c.at(3, 3)), 255);
    EXPECT_EQ(int(c.at(6, 3)), 128);
    EXPECT_EQ(int(c.at(7, 3)), 0);
}

UTEST(stb_raster, even_odd_leaves_a_hole_where_non_zero_fills) {
    outline p;
    p.add_rect(2, 2, 20, 20);
    p.add_rect(8, 8, 8, 8);  // same direction as the outer one
    const coverage_map nz = fill_map(p, fill_rule::non_zero, 24, 24);
    const coverage_map eo = fill_map(p, fill_rule::even_odd, 24, 24);
    EXPECT_EQ(int(nz.at(12, 12)), 255);
    EXPECT_EQ(int(eo.at(12, 12)), 0);
    EXPECT_EQ(int(eo.at(4, 4)), 255);
}

UTEST(stb_raster, an_open_contour_is_closed_for_filling) {
    outline p;
    p.move_to(2, 2).line_to(18, 2).line_to(18, 18).line_to(2, 18);  // no close()
    const coverage_map c = fill_map(p, fill_rule::non_zero, 20, 20);
    EXPECT_EQ(int(c.at(10, 10)), 255);
}

UTEST(stb_raster, the_clip_limits_what_is_reported) {
    outline p;
    p.add_rect(0, 0, 30, 30);
    int min_x = 1000, max_x = -1000, min_y = 1000, max_y = -1000;
    stb::rasterize_fill(p, fill_rule::non_zero, {}, clip_rect{5, 7, 10, 4}, [&](span_view v) {
        for (const span& s : v) {
            min_x = STB_MIN(min_x, s.x);
            max_x = STB_MAX(max_x, s.x + s.len);
            min_y = STB_MIN(min_y, s.y);
            max_y = STB_MAX(max_y, s.y + 1);
        }
    });
    EXPECT_EQ(min_x, 5);
    EXPECT_EQ(max_x, 15);
    EXPECT_EQ(min_y, 7);
    EXPECT_EQ(max_y, 11);
}

UTEST(stb_raster, a_clip_cuts_a_curve_the_same_way_as_drawing_everything_and_cropping) {
    outline p;
    p.add_ellipse(20, 20, 17.5f, 13.25f);
    const coverage_map full = fill_map(p, fill_rule::non_zero, 40, 40);
    coverage_map cut(40, 40);
    stb::rasterize_fill(p, fill_rule::non_zero, {}, clip_rect{9, 12, 17, 15}, [&](span_view v) { cut.add(v); });
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            const bool in_clip = x >= 9 && x < 26 && y >= 12 && y < 27;
            EXPECT_EQ(int(cut.at(x, y)), in_clip ? int(full.at(x, y)) : 0);
        }
    }
}

UTEST(stb_raster, nothing_is_reported_for_an_empty_outline_empty_clip_or_a_outline_outside) {
    int calls = 0;
    auto count = [&](span_view) { ++calls; };
    stb::rasterize_fill(outline{}, fill_rule::non_zero, {}, clip_rect{0, 0, 10, 10}, count);
    outline p;
    p.add_rect(1, 1, 5, 5);
    stb::rasterize_fill(p, fill_rule::non_zero, {}, clip_rect{0, 0, 0, 10}, count);
    stb::rasterize_fill(p, fill_rule::non_zero, {}, clip_rect{0, 0, 10, -3}, count);
    stb::rasterize_fill(p, fill_rule::non_zero, {}, clip_rect{20, 20, 10, 10}, count);
    stb::rasterize_stroke(outline{}, stroke_style{}, {}, clip_rect{0, 0, 10, 10}, count);
    EXPECT_EQ(calls, 0);
}

UTEST(stb_raster, the_matrix_moves_the_outline) {
    outline p;
    p.add_rect(0, 0, 4, 4);
    const coverage_map c = fill_map(p, fill_rule::non_zero, 20, 20, matrix::translation(10, 6));
    EXPECT_EQ(int(c.at(10, 6)), 255);
    EXPECT_EQ(int(c.at(13, 9)), 255);
    EXPECT_EQ(int(c.at(9, 6)), 0);
    EXPECT_EQ(int(c.at(14, 6)), 0);
    EXPECT_EQ(count_nonzero(c), 16);
}

UTEST(stb_raster, nan_and_infinity_draw_nothing_and_huge_values_are_survivable) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    testutil::log_capture log(stb::logging::level::warning);  // one warning per call that had to ignore or move something

    outline a;
    a.move_to(1, 1).line_to(nan, 5).line_to(10, 10).close();
    EXPECT_EQ(count_nonzero(fill_map(a, fill_rule::non_zero, 12, 12)), 0);
    EXPECT_EQ(count_nonzero(stroke_map(a, stroke_style{2.f}, 12, 12)), 0);

    outline b;
    b.add_rect(0, 0, 5, inf);
    EXPECT_EQ(count_nonzero(fill_map(b, fill_rule::non_zero, 12, 12)), 0);

    outline c;
    c.add_rect(1, 1, 5, 5);
    EXPECT_EQ(count_nonzero(fill_map(c, fill_rule::non_zero, 12, 12, matrix{nan, 0, 0, 1, 0, 0})), 0);

    outline huge;
    huge.add_rect(-1e30f, -1e30f, 2e30f, 2e30f);
    const int warnings_before_huge = log.count("libstb.raster");
    const coverage_map h = fill_map(huge, fill_rule::non_zero, 12, 12);
    EXPECT_EQ(int(h.at(5, 5)), 255);
    EXPECT_EQ(warnings_before_huge, 4);        // the NaN outline (filled and stroked), the infinite one, the NaN matrix
    EXPECT_EQ(log.count("libstb.raster"), 5);  // and the coordinates moved to the limit
}

UTEST(stb_raster, a_stroke_of_a_horizontal_line_covers_its_width) {
    outline p;
    p.move_to(2, 10).line_to(18, 10);
    const coverage_map c = stroke_map(p, stroke_style{4.f}, 20, 20);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            const bool inside = x >= 2 && x < 18 && y >= 8 && y < 12;  // butt caps: ends at the points
            EXPECT_EQ(int(c.at(x, y)), inside ? 255 : 0);
        }
    }
}

UTEST(stb_raster, caps_extend_an_open_line) {
    outline p;
    p.move_to(6, 10).line_to(14, 10);
    stroke_style butt{4.f};
    stroke_style square{4.f};
    square.cap = stb::line_cap::square;
    stroke_style round{4.f};
    round.cap = stb::line_cap::round;
    const coverage_map cb = stroke_map(p, butt, 20, 20);
    const coverage_map cs = stroke_map(p, square, 20, 20);
    const coverage_map cr = stroke_map(p, round, 20, 20);
    EXPECT_EQ(int(cb.at(5, 10)), 0);
    EXPECT_EQ(int(cs.at(5, 10)), 255);  // square: half a width past the end
    EXPECT_EQ(int(cs.at(4, 10)), 255);
    EXPECT_EQ(int(cr.at(5, 10)), 255);  // round: reaches the same distance on the axis...
    EXPECT_TRUE(cr.at(4, 8) < 255);      // ...but not in the corner
    EXPECT_TRUE(cs.at(4, 8) == 255);
}

UTEST(stb_raster, a_closed_contour_has_no_caps_and_joins_its_corners) {
    outline p;
    p.add_rect(5, 5, 10, 10);
    stroke_style st{2.f};
    const coverage_map miter = stroke_map(p, st, 20, 20);
    st.join = stb::line_join::bevel;
    const coverage_map bevel = stroke_map(p, st, 20, 20);
    EXPECT_EQ(int(miter.at(4, 4)), 255);  // the mitered outer corner is a full square
    EXPECT_TRUE(bevel.at(4, 4) < 255);    // cut off by the bevel
    EXPECT_EQ(int(miter.at(10, 10)), 0);  // the inside stays empty
}

UTEST(stb_raster, a_miter_beyond_the_limit_becomes_a_bevel) {
    outline p;
    p.move_to(2, 20).line_to(36, 10).line_to(2, 12);  // a sharp corner
    stroke_style spiky{3.f};
    spiky.miter_limit = 20.f;
    stroke_style cut{3.f};
    cut.miter_limit = 1.f;
    EXPECT_TRUE(count_nonzero(stroke_map(p, spiky, 40, 24)) > count_nonzero(stroke_map(p, cut, 40, 24)));
}

UTEST(stb_raster, the_width_is_scaled_by_the_matrix) {
    outline p;
    p.move_to(1, 2).line_to(9, 2);
    const coverage_map thin = stroke_map(p, stroke_style{1.f}, 40, 20);
    const coverage_map scaled = stroke_map(p, stroke_style{1.f}, 40, 20, matrix::scaling(3));
    EXPECT_TRUE(count_nonzero(scaled) > 2 * count_nonzero(thin));
}

UTEST(stb_raster, width_zero_negative_or_not_a_number_draws_nothing) {
    outline p;
    p.move_to(1, 5).line_to(15, 5);
    EXPECT_EQ(count_nonzero(stroke_map(p, stroke_style{0.f}, 20, 10)), 0);
    EXPECT_EQ(count_nonzero(stroke_map(p, stroke_style{-2.f}, 20, 10)), 0);
    {
        testutil::log_capture log(stb::logging::level::warning);
        EXPECT_EQ(count_nonzero(stroke_map(p, stroke_style{std::nanf("")}, 20, 10)), 0);
        EXPECT_EQ(log.count("libstb.raster"), 1);  // only a NaN width is said: zero and negative are just "nothing"
    }
}

UTEST(stb_raster, dashes_cut_the_line_into_pieces) {
    outline p;
    p.move_to(0, 5).line_to(24, 5);
    stroke_style st{2.f};
    st.dashes = {4.f, 4.f};
    const coverage_map c = stroke_map(p, st, 24, 10);
    for (int x = 0; x < 24; ++x) {
        const bool on = (x / 4) % 2 == 0;
        EXPECT_EQ(int(c.at(x, 4)), on ? 255 : 0);
        EXPECT_EQ(int(c.at(x, 5)), on ? 255 : 0);
        EXPECT_EQ(int(c.at(x, 3)), 0);
    }
}

UTEST(stb_raster, the_dash_offset_moves_the_pattern_and_an_odd_list_alternates) {
    outline p;
    p.move_to(0, 5).line_to(24, 5);
    stroke_style st{2.f};
    st.dashes = {4.f, 4.f};
    st.dash_offset = 4.f;  // starts in the gap
    const coverage_map c = stroke_map(p, st, 24, 10);
    EXPECT_EQ(int(c.at(1, 4)), 0);
    EXPECT_EQ(int(c.at(5, 4)), 255);

    stroke_style odd{2.f};
    odd.dashes = {4.f};  // 4 on, 4 off, 4 on...
    const coverage_map o = stroke_map(p, odd, 24, 10);
    EXPECT_EQ(int(o.at(1, 4)), 255);
    EXPECT_EQ(int(o.at(5, 4)), 0);
    EXPECT_EQ(int(o.at(9, 4)), 255);
}

UTEST(stb_raster, an_unusable_dash_pattern_gives_a_solid_line) {
    outline p;
    p.move_to(0, 5).line_to(24, 5);
    stroke_style solid{2.f};
    const int expected = count_nonzero(stroke_map(p, solid, 24, 10));
    testutil::log_capture log(stb::logging::level::warning);
    for (const std::vector<float>& d : {std::vector<float>{}, std::vector<float>{0.f, 0.f}, std::vector<float>{3.f, -1.f},
                                        std::vector<float>{std::nanf(""), 2.f}}) {
        stroke_style st{2.f};
        st.dashes = d;
        EXPECT_EQ(count_nonzero(stroke_map(p, st, 24, 10)), expected);
    }
    EXPECT_EQ(log.count("libstb.raster"), 3);  // the empty pattern is a solid line by request; the other three are said
}

UTEST(stb_raster, outline_dashed_returns_straight_dashes) {
    outline p;
    p.move_to(0, 0).line_to(20, 0);
    const outline d = p.dashed({5.f, 5.f});
    // 5 on, 5 off, 5 on, 5 off: two dashes (move + line each); the third starts at x = 20 (a point).
    EXPECT_TRUE(d.size() >= 4);
    for (const stb::outline_command& c : d) {
        EXPECT_TRUE(c.op == stb::outline_op::move_to || c.op == stb::outline_op::line_to);
    }
    EXPECT_TRUE(d.commands()[0].op == stb::outline_op::move_to);
    EXPECT_EQ(d.commands()[1].p[0].x, 5.f);
    EXPECT_EQ(d.commands()[2].p[0].x, 10.f);
}

UTEST(stb_raster, outline_dashed_with_an_unusable_pattern_is_a_copy) {
    outline p;
    p.add_circle(5, 5, 3);
    EXPECT_TRUE(p.dashed({}) == p);
    EXPECT_TRUE(p.dashed({0.f}) == p);
    EXPECT_TRUE(p.dashed({-1.f, 2.f}) == p);
}

UTEST(stb_raster, a_dot_gets_round_caps_and_nothing_with_butt_caps) {
    outline p;
    p.move_to(10, 10).line_to(10, 10);
    stroke_style round{8.f};
    round.cap = stb::line_cap::round;
    EXPECT_EQ(int(stroke_map(p, round, 20, 20).at(10, 10)), 255);
    EXPECT_EQ(count_nonzero(stroke_map(p, stroke_style{8.f}, 20, 20)), 0);
}

// --- drawing on an image -------------------------------------------------------------------------------

namespace {

stb::color pixel(const stb::image& img, int x, int y) {
    const std::uint8_t* p = img.data() + std::size_t(y) * img.stride() + std::size_t(x) * std::size_t(img.channels());
    return stb::color(p[0], p[1], p[2], img.channels() == 4 ? p[3] : 255);
}

bool same_pixels(const stb::image& a, const stb::image& b) {
    if (a.width() != b.width() || a.height() != b.height() || a.channels() != b.channels()) return false;
    const std::size_t row = std::size_t(a.width()) * std::size_t(a.channels());
    for (int y = 0; y < a.height(); ++y) {
        if (std::memcmp(a.data() + std::size_t(y) * a.stride(), b.data() + std::size_t(y) * b.stride(), row) != 0) return false;
    }
    return true;
}

stb::image white(int w, int h) {
    stb::image img(w, h, stb::pixel_format::rgba);
    img.fill(stb::color(255, 255, 255, 255));
    return img;
}

}  // namespace

UTEST(stb_raster, fill_paints_the_inside_and_leaves_the_rest) {
    stb::image img = white(20, 20);
    outline p;
    p.add_rect(5, 5, 10, 10);
    stb::fill(img, p, stb::color(255, 0, 0));
    EXPECT_TRUE(pixel(img, 10, 10) == stb::color(255, 0, 0, 255));
    EXPECT_TRUE(pixel(img, 2, 2) == stb::color(255, 255, 255, 255));
    EXPECT_TRUE(pixel(img, 15, 15) == stb::color(255, 255, 255, 255));
}

UTEST(stb_raster, fill_blends_the_colour_alpha_and_the_opacity) {
    stb::image img = white(10, 10);
    outline p;
    p.add_rect(0, 0, 10, 10);
    stb::fill(img, p, stb::color(0, 0, 0, 255), fill_rule::non_zero, stb::draw_options{{}, stb::blend_mode::normal, 0.5f});
    const stb::color c = pixel(img, 5, 5);
    EXPECT_TRUE(c.r() >= 126 && c.r() <= 129);  // half way between white and black

    stb::image img2 = white(10, 10);
    stb::fill(img2, p, stb::color(0, 0, 0, 0));  // a fully transparent colour paints nothing
    EXPECT_TRUE(pixel(img2, 5, 5) == stb::color(255, 255, 255, 255));
}

UTEST(stb_raster, fill_uses_the_blend_mode_and_the_transform) {
    stb::image img = white(20, 20);
    outline p;
    p.add_rect(0, 0, 4, 4);
    stb::draw_options o;
    o.transform = matrix::translation(8, 8);
    o.blend = stb::blend_mode::multiply;
    stb::fill(img, p, stb::color(255, 128, 0), fill_rule::non_zero, o);
    EXPECT_TRUE(pixel(img, 9, 9) == stb::color(255, 128, 0, 255));  // white * colour = colour
    EXPECT_TRUE(pixel(img, 2, 2) == stb::color(255, 255, 255, 255));
}

UTEST(stb_raster, stroke_draws_the_outline_and_not_the_inside) {
    stb::image img = white(30, 30);
    outline p;
    p.add_rect(5, 5, 20, 20);
    stb::stroke(img, p, stb::color(0, 0, 255), stroke_style{3.f});
    EXPECT_TRUE(pixel(img, 15, 5) == stb::color(0, 0, 255, 255));    // on the edge
    EXPECT_TRUE(pixel(img, 15, 15) == stb::color(255, 255, 255, 255));  // inside
}

UTEST(stb_raster, drawing_partly_or_wholly_outside_the_image_is_fine) {
    stb::image img = white(10, 10);
    outline p;
    p.add_circle(0, 0, 6);
    stb::fill(img, p, stb::color(0, 255, 0));
    EXPECT_TRUE(pixel(img, 1, 1) == stb::color(0, 255, 0, 255));
    EXPECT_TRUE(pixel(img, 9, 9) == stb::color(255, 255, 255, 255));

    outline q;
    q.add_rect(100, 100, 5, 5);
    stb::fill(img, q, stb::color(255, 0, 0));
    stb::stroke(img, q, stb::color(255, 0, 0));
    EXPECT_TRUE(pixel(img, 9, 9) == stb::color(255, 255, 255, 255));
}

UTEST(stb_raster, fill_and_stroke_check_their_arguments) {
    outline p;
    p.add_rect(1, 1, 3, 3);
    stb::image empty;
    ASSERT_THROWS(stb::fill(empty, p, stb::color(0, 0, 0)), std::invalid_argument);
    ASSERT_THROWS(stb::stroke(empty, p, stb::color(0, 0, 0)), std::invalid_argument);

    stb::image img = white(8, 8);
    stb::draw_options bad;
    bad.opacity = 1.5f;
    ASSERT_THROWS(stb::fill(img, p, stb::color(0, 0, 0), fill_rule::non_zero, bad), std::invalid_argument);
    bad.opacity = -0.1f;
    ASSERT_THROWS(stb::stroke(img, p, stb::color(0, 0, 0), stroke_style{}, bad), std::invalid_argument);
}

UTEST(stb_raster, a_glyph_outline_can_be_filled) {
    // The outline of any outline works: here a triangle given as raw commands, like font::outline returns.
    std::vector<stb::outline_command> cmds(4);
    cmds[0].op = stb::outline_op::move_to;
    cmds[0].p[0] = {2, 2};
    cmds[1].op = stb::outline_op::line_to;
    cmds[1].p[0] = {18, 2};
    cmds[2].op = stb::outline_op::line_to;
    cmds[2].p[0] = {10, 18};
    cmds[3].op = stb::outline_op::close;
    stb::image img = white(20, 20);
    stb::fill(img, outline(cmds), stb::color(0, 0, 0));
    EXPECT_TRUE(pixel(img, 10, 6) == stb::color(0, 0, 0, 255));
    EXPECT_TRUE(pixel(img, 2, 17) == stb::color(255, 255, 255, 255));
}

// --- stb::canvas::add_outline ------------------------------------------------------------------------------

UTEST(stb_raster_canvas, add_outline_fills_with_the_canvas_colour_and_gives_the_pixels_of_stb_fill) {
    outline p;
    p.add_circle(15, 15, 10);
    p.add_rect(2, 2, 6, 6);

    stb::image base = white(30, 30);
    stb::canvas c(base);
    c.set_color(stb::color(200, 30, 30));
    c.add_outline(p);
    const stb::image out = c.finish();

    stb::image expected = white(30, 30);
    stb::fill(expected, p, stb::color(200, 30, 30));
    EXPECT_TRUE(same_pixels(out, expected));
}

UTEST(stb_raster_canvas, a_outline_item_fills_then_strokes_with_its_own_settings) {
    outline p;
    p.add_rect(6, 6, 18, 18);

    stb::image base = white(30, 30);
    stb::canvas c(base);
    c.add_outline(p).fill(stb::color(0, 255, 0)).stroke(4.f, stb::color(0, 0, 255)).join(stb::line_join::round).opacity(1.f);
    const stb::image out = c.finish();

    stroke_style st{4.f};
    st.join = stb::line_join::round;
    stb::image expected = white(30, 30);
    stb::fill(expected, p, stb::color(0, 255, 0));
    stb::stroke(expected, p, stb::color(0, 0, 255), st);
    EXPECT_TRUE(same_pixels(out, expected));
    EXPECT_TRUE(pixel(out, 15, 15) == stb::color(0, 255, 0, 255));  // the fill shows inside
    EXPECT_TRUE(pixel(out, 6, 15) == stb::color(0, 0, 255, 255));   // the outline on the edge
}

UTEST(stb_raster_canvas, no_fill_and_dash_and_transform) {
    outline p;
    p.move_to(0, 0).line_to(20, 0);

    stb::image base = white(30, 30);
    stb::canvas c(base);
    c.add_outline(p).no_fill().stroke(2.f, stb::color(0, 0, 0)).dash({4.f, 4.f}).transform(matrix::translation(5, 10));
    const stb::image out = c.finish();
    EXPECT_TRUE(pixel(out, 6, 10) == stb::color(0, 0, 0, 255));        // on a dash
    EXPECT_TRUE(pixel(out, 11, 10) == stb::color(255, 255, 255, 255));  // in a gap
}

UTEST(stb_raster_canvas, items_are_drawn_in_the_order_they_were_added) {
    outline p;
    p.add_rect(0, 0, 10, 10);
    stb::image base = white(10, 10);
    stb::canvas c(base);
    c.add_outline(p).fill(stb::color(255, 0, 0));
    c.add_outline(p).fill(stb::color(0, 0, 255));
    EXPECT_TRUE(pixel(c.finish(), 5, 5) == stb::color(0, 0, 255, 255));
}

UTEST(stb_raster_canvas, outline_item_checks_its_arguments_and_the_canvas_state) {
    outline p;
    p.add_rect(0, 0, 4, 4);
    stb::image base = white(8, 8);
    stb::canvas c(base);
    stb::canvas::outline_item& item = c.add_outline(p);
    ASSERT_THROWS(item.stroke(0.f), std::invalid_argument);
    ASSERT_THROWS(item.stroke(-1.f, stb::color(0, 0, 0)), std::invalid_argument);
    ASSERT_THROWS(item.miter_limit(0.5f), std::invalid_argument);
    ASSERT_THROWS(item.opacity(2.f), std::invalid_argument);
    c.finish();
    ASSERT_THROWS(item.fill(stb::color(0, 0, 0)), std::invalid_argument);
    ASSERT_THROWS(c.add_outline(p), std::invalid_argument);
}
