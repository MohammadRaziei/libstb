#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "test_common.hpp"

// matrix::map (arrays), matrix::map_rect, outline::arc_to and outline::tight_bounds.

namespace {

using stb::matrix;
using stb::outline;
using stb::outline_bounds;
using stb::outline_command;
using stb::outline_op;
using stb::point;

// Built by a function, not a braced list (a comma inside braces splits a macro argument on MSVC).
point pt(float x, float y) {
    point p;
    p.x = x;
    p.y = y;
    return p;
}

outline_bounds box(float x0, float y0, float x1, float y1) {
    outline_bounds b;
    b.x0 = x0;
    b.y0 = y0;
    b.x1 = x1;
    b.y1 = y1;
    return b;
}

bool same_box(const outline_bounds& a, const outline_bounds& b, float eps) {
    return std::fabs(a.x0 - b.x0) <= eps && std::fabs(a.y0 - b.y0) <= eps && std::fabs(a.x1 - b.x1) <= eps &&
           std::fabs(a.y1 - b.y1) <= eps;
}

// The tight box of an outline, or an obviously wrong box when there is none.
outline_bounds tight(const outline& o) {
    const auto b = o.tight_bounds();
    return b ? *b : box(1e9f, 1e9f, -1e9f, -1e9f);
}

outline arc(float rx, float ry, float rot, bool large, bool sweep, float x0, float y0, float x1, float y1) {
    outline o;
    o.move_to(x0, y0).arc_to(rx, ry, rot, large, sweep, x1, y1);
    return o;
}

// A small deterministic generator for the property test (no <random>: the numbers must not depend on the library).
struct lcg {
    std::uint32_t s = 12345u;
    float next(float lo, float hi) {
        s = s * 1664525u + 1013904223u;
        return lo + (hi - lo) * (float(s >> 8) / float(1u << 24));
    }
};

}  // namespace

// ---------------------------------------------------------------------- matrix

UTEST(libstb_geometry_extras_tests, test_map_array_gives_the_bits_of_map_and_may_work_in_place) {
    const matrix m{2.f, 0.5f, -1.f, 3.f, 10.f, -20.f};
    std::vector<point> src;
    for (int i = 0; i < 17; ++i) src.push_back(pt(float(i) * 1.7f - 5.f, float(i * i) * 0.3f));
    std::vector<point> dst(src.size());
    m.map(src.data(), dst.data(), src.size());
    bool all = true;
    for (std::size_t i = 0; i < src.size(); ++i) all = all && dst[i] == m.map(src[i]);
    ASSERT_TRUE(all);

    std::vector<point> inplace = src;
    m.map(inplace.data(), inplace.data(), inplace.size());
    ASSERT_TRUE(inplace == dst);

    m.map(src.data(), dst.data(), 0);  // nothing to do, nothing touched
    ASSERT_TRUE(inplace == dst);
}

UTEST(libstb_geometry_extras_tests, test_map_rect_is_the_box_of_the_four_mapped_corners) {
    ASSERT_TRUE(same_box(matrix{}.map_rect(box(1, 2, 4, 6)), box(1, 2, 4, 6), 0.f));
    ASSERT_TRUE(same_box(matrix::scaling(2, 3).map_rect(box(1, 2, 4, 6)), box(2, 6, 8, 18), 0.f));
    ASSERT_TRUE(same_box(matrix::translation(10, -1).map_rect(box(0, 0, 3, 3)), box(10, -1, 13, 2), 0.f));
    // x' = -y, y' = x: a quarter turn. The 4 x 2 box at the origin becomes the 2 x 4 box left of it.
    ASSERT_TRUE(same_box(matrix{0, 1, -1, 0, 0, 0}.map_rect(box(0, 0, 4, 2)), box(-2, 0, 0, 4), 0.f));
    // A 45 degree turn of a square: the box grows to sqrt(2) times the side.
    const outline_bounds r = matrix::rotation(0.78539816f).map_rect(box(0, 0, 10, 10));
    ASSERT_TRUE(std::fabs(r.width() - 14.142136f) < 1e-3f);
    ASSERT_TRUE(std::fabs(r.height() - 14.142136f) < 1e-3f);
    // A mirrored matrix still gives x0 <= x1 and y0 <= y1.
    const outline_bounds f = matrix::scaling(-1, -1).map_rect(box(1, 2, 4, 6));
    ASSERT_TRUE(same_box(f, box(-4, -6, -1, -2), 0.f));
}

// ------------------------------------------------------------------------- arc_to

UTEST(libstb_geometry_extras_tests, test_half_circle_goes_the_way_sweep_says) {
    // Clockwise on screen (y down) from the left point to the right one passes over the top (y = -1).
    const outline over = arc(1, 1, 0, false, true, -1, 0, 1, 0);
    ASSERT_EQ(3u, over.size());  // move_to and two quarter turns
    ASSERT_TRUE(same_box(tight(over), box(-1, -1, 1, 0), 1e-4f));
    ASSERT_TRUE(over.commands().back().p[2] == pt(1, 0));  // ends exactly where asked
    ASSERT_TRUE(*over.current_point() == pt(1, 0));
    ASSERT_TRUE(same_box(tight(arc(1, 1, 0, false, false, -1, 0, 1, 0)), box(-1, 0, 1, 1), 1e-4f));
}

UTEST(libstb_geometry_extras_tests, test_the_four_arcs_through_two_points) {
    // From (1, 0) to (0, 1) with radius 1: centre (0, 0) for the arcs around the origin, (1, 1) for the others.
    ASSERT_TRUE(same_box(tight(arc(1, 1, 0, false, true, 1, 0, 0, 1)), box(0, 0, 1, 1), 1e-4f));   // quarter, clockwise
    ASSERT_TRUE(same_box(tight(arc(1, 1, 0, true, true, 1, 0, 0, 1)), box(0, 0, 2, 2), 1e-4f));    // three quarters round (1, 1)
    ASSERT_TRUE(same_box(tight(arc(1, 1, 0, true, false, 1, 0, 0, 1)), box(-1, -1, 1, 1), 1e-4f)); // three quarters round (0, 0)
    ASSERT_TRUE(same_box(tight(arc(1, 1, 0, false, false, 1, 0, 0, 1)), box(0, 0, 1, 1), 1e-4f));  // quarter round (1, 1)

    // The two quarters are different curves: their first control points differ (kappa = 0.5523 of the radius).
    const outline cw = arc(1, 1, 0, false, true, 1, 0, 0, 1);
    const outline ccw = arc(1, 1, 0, false, false, 1, 0, 0, 1);
    ASSERT_EQ(2u, cw.size());
    ASSERT_TRUE(std::fabs(cw.commands()[1].p[0].x - 1.f) < 1e-4f && std::fabs(cw.commands()[1].p[0].y - 0.55228f) < 1e-4f);
    ASSERT_TRUE(std::fabs(ccw.commands()[1].p[0].x - 0.44772f) < 1e-4f && std::fabs(ccw.commands()[1].p[0].y) < 1e-4f);
}

UTEST(libstb_geometry_extras_tests, test_radii_that_are_too_small_are_scaled_up) {
    // The points are 4 apart: the smallest circle through them has radius 2, centre (2, 0).
    ASSERT_TRUE(same_box(tight(arc(1, 1, 0, false, true, 0, 0, 4, 0)), box(0, -2, 4, 0), 1e-4f));
    // Negative radii are taken as positive.
    ASSERT_TRUE(same_box(tight(arc(-1, -1, 0, false, true, -1, 0, 1, 0)), box(-1, -1, 1, 0), 1e-4f));
}

UTEST(libstb_geometry_extras_tests, test_special_cases_of_the_svg_rules) {
    outline same;
    same.move_to(3, 4);
    same.arc_to(5, 5, 0, true, true, 3, 4);  // same end points: nothing
    ASSERT_EQ(1u, same.size());

    const outline flat = arc(0, 7, 30, false, true, 0, 0, 5, 5);  // a radius of 0: a straight line
    ASSERT_EQ(2u, flat.size());
    ASSERT_TRUE(flat.commands()[1].op == outline_op::line_to && flat.commands()[1].p[0] == pt(5, 5));

    outline empty;  // an empty outline: the arc starts at (0, 0)
    empty.arc_to(1, 1, 0, false, true, 2, 0);
    ASSERT_TRUE(empty.commands()[0].op == outline_op::move_to && empty.commands()[0].p[0] == pt(0, 0));
    ASSERT_TRUE(same_box(tight(empty), box(0, -1, 2, 0), 1e-4f));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    outline bad;
    bad.move_to(0, 0).arc_to(1, 1, 0, false, true, nan, 3);  // not a number: a line, which the rasterizer skips
    ASSERT_EQ(2u, bad.size());
    ASSERT_TRUE(bad.commands()[1].op == outline_op::line_to);
}

UTEST(libstb_geometry_extras_tests, test_a_rotated_ellipse_is_the_same_as_the_ellipse_with_swapped_radii) {
    // Rotating the axes by 90 degrees swaps what rx and ry mean.
    const bool flags[2] = {false, true};
    bool all = true;
    for (bool large : flags) {
        for (bool sweep : flags) {
            const outline a = arc(2, 1, 90, large, sweep, 0, 0, 1, 1.5f);
            const outline b = arc(1, 2, 0, large, sweep, 0, 0, 1, 1.5f);
            all = all && a.size() == b.size();
            for (std::size_t i = 0; all && i < a.size(); ++i)
                for (int k = 0; k < 3; ++k)
                    all = all && std::fabs(a.commands()[i].p[k].x - b.commands()[i].p[k].x) < 1e-3f &&
                          std::fabs(a.commands()[i].p[k].y - b.commands()[i].p[k].y) < 1e-3f;
        }
    }
    ASSERT_TRUE(all);
}

UTEST(libstb_geometry_extras_tests, test_an_arc_stays_on_its_ellipse) {
    // Every point of the cubics (sampled) is within 0.1 percent of the circle round its centre.
    const outline o = arc(10, 10, 0, true, true, 10, 0, 0, 10);  // three quarters round (10, 10)
    point cur = pt(10, 0);
    float worst = 0;
    for (const outline_command& c : o) {
        if (c.op != outline_op::cubic_to) {
            if (c.op == outline_op::move_to) cur = c.p[0];
            continue;
        }
        for (int i = 0; i <= 20; ++i) {
            const float t = float(i) / 20.f, u = 1.f - t;
            const float x = u * u * u * cur.x + 3 * u * u * t * c.p[0].x + 3 * u * t * t * c.p[1].x + t * t * t * c.p[2].x;
            const float y = u * u * u * cur.y + 3 * u * u * t * c.p[0].y + 3 * u * t * t * c.p[1].y + t * t * t * c.p[2].y;
            const float off = std::fabs(std::hypot(x - 10.f, y - 10.f) - 10.f);
            worst = STB_MAX(worst, off);
        }
        cur = c.p[2];
    }
    ASSERT_TRUE(worst < 0.01f);
}

// ---------------------------------------------------------------------- tight_bounds

UTEST(libstb_geometry_extras_tests, test_tight_bounds_of_nothing_and_of_lines) {
    ASSERT_FALSE(outline().tight_bounds().has_value());
    outline r;
    r.add_rect(2, 3, 10, 20);
    ASSERT_TRUE(same_box(tight(r), box(2, 3, 12, 23), 0.f));
    ASSERT_TRUE(same_box(tight(r), *r.bounds(), 0.f));  // lines: the same as the control point box
    outline dot;
    dot.move_to(5, 6);  // a contour with one point still has bounds
    ASSERT_TRUE(same_box(tight(dot), box(5, 6, 5, 6), 0.f));
}

UTEST(libstb_geometry_extras_tests, test_tight_bounds_of_curves_leave_out_the_control_points) {
    outline q;
    q.move_to(0, 0).quad_to(50, 100, 100, 0);  // the curve peaks at y = 50, its control point is at 100
    ASSERT_TRUE(same_box(*q.bounds(), box(0, 0, 100, 100), 0.f));
    ASSERT_TRUE(same_box(tight(q), box(0, 0, 100, 50), 1e-4f));

    outline c;
    c.move_to(0, 0).cubic_to(0, 100, 100, 100, 100, 0);  // y(0.5) = 75
    ASSERT_TRUE(same_box(tight(c), box(0, 0, 100, 75), 1e-4f));

    outline loop;  // a cubic that goes beyond both of its end points in x: the extremes are inside the curve
    loop.move_to(0, 0).cubic_to(100, 0, -100, 100, 0, 100);
    const outline_bounds b = tight(loop);
    ASSERT_TRUE(b.x0 < -1.f && b.x1 > 1.f);
    ASSERT_TRUE(b.x1 < 100.f && b.x0 > -100.f);  // and the control points are not reached
}

UTEST(libstb_geometry_extras_tests, test_tight_bounds_follow_close_and_skip_non_finite_commands) {
    outline o;
    o.move_to(0, 0).line_to(10, 10).close().move_to(30, 5);  // the new contour starts at (30, 5)
    ASSERT_TRUE(same_box(tight(o), box(0, 0, 30, 10), 0.f));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    outline bad;
    bad.move_to(0, 0).line_to(10, 10).line_to(nan, 50).quad_to(inf, 0, 5, 5).line_to(20, 2);
    ASSERT_TRUE(same_box(tight(bad), box(0, 0, 20, 10), 0.f));
    outline only_bad;
    only_bad.move_to(nan, nan);
    ASSERT_FALSE(only_bad.tight_bounds().has_value());
}

UTEST(libstb_geometry_extras_tests, test_tight_bounds_match_a_dense_sampling_of_random_curves) {
    lcg rng;
    float worst = 0;
    bool inside = true;
    for (int n = 0; n < 400; ++n) {
        const bool cubic = n % 2 == 0;
        point p[4];
        for (point& q : p) q = pt(rng.next(-50.f, 50.f), rng.next(-50.f, 50.f));
        outline o;
        o.move_to(p[0]);
        if (cubic) o.cubic_to(p[1], p[2], p[3]);
        else o.quad_to(p[1], p[2]);

        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (int i = 0; i <= 20000; ++i) {
            const double t = i / 20000.0, u = 1.0 - t;
            double x, y;
            if (cubic) {
                x = u * u * u * p[0].x + 3 * u * u * t * p[1].x + 3 * u * t * t * p[2].x + t * t * t * p[3].x;
                y = u * u * u * p[0].y + 3 * u * u * t * p[1].y + 3 * u * t * t * p[2].y + t * t * t * p[3].y;
            } else {
                x = u * u * p[0].x + 2 * u * t * p[1].x + t * t * p[2].x;
                y = u * u * p[0].y + 2 * u * t * p[1].y + t * t * p[2].y;
            }
            x0 = STB_MIN(x0, x), y0 = STB_MIN(y0, y), x1 = STB_MAX(x1, x), y1 = STB_MAX(y1, y);
        }
        const outline_bounds t = tight(o);
        const outline_bounds c = *o.bounds();
        const float errs[4] = {std::fabs(t.x0 - float(x0)), std::fabs(t.y0 - float(y0)), std::fabs(t.x1 - float(x1)),
                               std::fabs(t.y1 - float(y1))};
        for (const float e : errs) worst = STB_MAX(worst, e);
        inside = inside && t.x0 >= c.x0 && t.y0 >= c.y0 && t.x1 <= c.x1 && t.y1 <= c.y1;  // never beyond the control box
    }
    ASSERT_TRUE(worst < 0.01f);  // sampling misses the exact extreme by less than that at 20000 steps
    ASSERT_TRUE(inside);
}
