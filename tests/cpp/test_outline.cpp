#include <cmath>
#include <vector>

#include "test_common.hpp"

// stb::matrix and stb::outline: the geometry the rasterizer works from.

namespace {

using stb::matrix;
using stb::outline;
using stb::outline_command;
using stb::outline_op;
using stb::point;

// Built by a function, not a braced list: a comma inside braces is a macro-argument separator for
// the (non-conforming) MSVC preprocessor, which then splits "point{0, 0}" inside EXPECT_TRUE.
point pt(float x, float y) {
    point p;
    p.x = x;
    p.y = y;
    return p;
}

// (not "near": <windows.h> #defines that as nothing)
bool close_to(float a, float b) { return std::fabs(a - b) < 1e-4f; }
bool close_to(point a, point b) { return close_to(a.x, b.x) && close_to(a.y, b.y); }

std::vector<outline_op> ops(const outline& p) {
    std::vector<outline_op> r;
    for (const outline_command& c : p) r.push_back(c.op);
    return r;
}

}  // namespace

// ------------------------------------------------------------------------------ matrix

UTEST(stb_matrix, identity_by_default) {
    const matrix m;
    EXPECT_TRUE(m.is_identity());
    EXPECT_TRUE(m.map(3, -4) == pt(3, -4));
}

UTEST(stb_matrix, map_uses_the_svg_convention) {
    const matrix m{2, 3, 5, 7, 11, 13};
    const point p = m.map(1, 1);
    EXPECT_EQ(p.x, 2 * 1 + 5 * 1 + 11);
    EXPECT_EQ(p.y, 3 * 1 + 7 * 1 + 13);
}

UTEST(stb_matrix, builder_calls_apply_to_the_point_first) {
    // translate(10, 0).scale(2): the point is scaled first, then moved.
    matrix m;
    m.translate(10, 0).scale(2);
    EXPECT_TRUE(close_to(m.map(1, 1), pt(12, 2)));
    // The same with then(): scale first, translate second.
    const matrix n = matrix::scaling(2).then(matrix::translation(10, 0));
    EXPECT_TRUE(m == n);
}

UTEST(stb_matrix, rotation_turns_x_towards_y) {
    const matrix r = matrix::rotation(1.57079632679f);
    EXPECT_TRUE(close_to(r.map(1, 0), pt(0, 1)));
    EXPECT_TRUE(close_to(r.map(0, 1), pt(-1, 0)));
}

UTEST(stb_matrix, shear) {
    const matrix s = matrix::shearing(0.f, 0.f);
    EXPECT_TRUE(s.is_identity());
    const matrix t = matrix::shearing(0.7853981634f, 0.f);  // x += tan(45 degrees) * y
    EXPECT_TRUE(close_to(t.map(0, 2), pt(2, 2)));
}

UTEST(stb_matrix, inverse_undoes_the_matrix) {
    matrix m;
    m.translate(5, -3).rotate(0.4f).scale(2, 3);
    const auto inv = m.inverse();
    ASSERT_TRUE(inv.has_value());
    const point back = inv->map(m.map(7, 9));
    EXPECT_TRUE(close_to(back, pt(7, 9)));
}

UTEST(stb_matrix, singular_matrix_has_no_inverse) {
    EXPECT_FALSE(matrix::scaling(0, 1).inverse().has_value());
}

// -------------------------------------------------------------------------------- outline

UTEST(stb_outline, builder_records_commands) {
    outline p;
    p.move_to(1, 2).line_to(3, 4).quad_to(5, 6, 7, 8).cubic_to(1, 1, 2, 2, 3, 3).close();
    ASSERT_EQ(p.size(), 5u);
    EXPECT_TRUE(ops(p) == (std::vector<outline_op>{outline_op::move_to, outline_op::line_to, outline_op::quad_to,
                                                outline_op::cubic_to, outline_op::close}));
    EXPECT_TRUE(p.commands()[2].p[0] == pt(5, 6));
    EXPECT_TRUE(p.commands()[2].p[1] == pt(7, 8));
}

UTEST(stb_outline, empty_outline_starts_at_the_origin) {
    outline p;
    EXPECT_TRUE(p.empty());
    EXPECT_FALSE(p.current_point().has_value());
    EXPECT_FALSE(p.bounds().has_value());
    p.line_to(5, 5);
    ASSERT_EQ(p.size(), 2u);
    EXPECT_TRUE(p.commands()[0].op == outline_op::move_to);
    EXPECT_TRUE(p.commands()[0].p[0] == pt(0, 0));
}

UTEST(stb_outline, a_command_after_close_starts_a_new_contour_at_the_old_start) {
    outline p;
    p.move_to(10, 10).line_to(20, 10).line_to(20, 20).close().line_to(30, 30);
    ASSERT_EQ(p.size(), 6u);
    EXPECT_TRUE(p.commands()[4].op == outline_op::move_to);
    EXPECT_TRUE(p.commands()[4].p[0] == pt(10, 10));
    EXPECT_TRUE(p.commands()[5].op == outline_op::line_to);
}

UTEST(stb_outline, close_on_empty_or_closed_outline_does_nothing) {
    outline p;
    p.close();
    EXPECT_TRUE(p.empty());
    p.move_to(0, 0).line_to(1, 1).close().close();
    EXPECT_EQ(p.size(), 3u);
}

UTEST(stb_outline, current_point_follows_the_last_command_and_close) {
    outline p;
    p.move_to(1, 2);
    EXPECT_TRUE(*p.current_point() == pt(1, 2));
    p.line_to(3, 4);
    EXPECT_TRUE(*p.current_point() == pt(3, 4));
    p.close();
    EXPECT_TRUE(*p.current_point() == pt(1, 2));
}

UTEST(stb_outline, bounds_hold_every_point_including_controls) {
    outline p;
    p.move_to(0, 0).quad_to(10, -20, 30, 5).cubic_to(40, 40, -5, 8, 2, 2);
    const auto b = p.bounds();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->x0, -5.f);
    EXPECT_EQ(b->y0, -20.f);
    EXPECT_EQ(b->x1, 40.f);
    EXPECT_EQ(b->y1, 40.f);
    EXPECT_EQ(b->width(), 45.f);
    EXPECT_EQ(b->height(), 60.f);
}

UTEST(stb_outline, rect_is_one_closed_clockwise_contour) {
    outline p;
    p.add_rect(10, 20, 30, 40);
    const auto b = p.bounds();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->x0, 10.f);
    EXPECT_EQ(b->y0, 20.f);
    EXPECT_EQ(b->x1, 40.f);
    EXPECT_EQ(b->y1, 60.f);
    EXPECT_TRUE(p.commands().front().op == outline_op::move_to);
    EXPECT_TRUE(p.commands().back().op == outline_op::close);
    EXPECT_TRUE(p.commands()[1].p[0] == pt(40, 20));  // first goes right, then down: clockwise
    EXPECT_TRUE(p.commands()[2].p[0] == pt(40, 60));
}

UTEST(stb_outline, negative_sides_give_the_same_rect) {
    outline a, b;
    a.add_rect(10, 20, 30, 40);
    b.add_rect(40, 60, -30, -40);
    EXPECT_TRUE(a == b);
}

UTEST(stb_outline, empty_shapes_add_nothing) {
    outline p;
    p.add_rect(0, 0, 0, 5).add_rect(0, 0, 5, 0).add_rounded_rect(0, 0, 0, 0, 3).add_ellipse(0, 0, 0, 4).add_circle(1, 1, 0);
    EXPECT_TRUE(p.empty());
}

UTEST(stb_outline, rounded_rect_without_radius_is_a_rect_and_radius_is_limited) {
    outline plain, zero;
    plain.add_rect(0, 0, 20, 10);
    zero.add_rounded_rect(0, 0, 20, 10, 0);
    EXPECT_TRUE(plain == zero);

    outline big, half;
    big.add_rounded_rect(0, 0, 20, 10, 100);
    half.add_rounded_rect(0, 0, 20, 10, 10, 5);
    EXPECT_TRUE(big == half);
    const auto b = big.bounds();
    EXPECT_EQ(b->x0, 0.f);
    EXPECT_EQ(b->x1, 20.f);
    EXPECT_EQ(b->y0, 0.f);
    EXPECT_EQ(b->y1, 10.f);
}

UTEST(stb_outline, ellipse_touches_its_box) {
    outline p;
    p.add_ellipse(50, 40, 30, 20);
    const auto b = p.bounds();
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(close_to(b->x0, 20) && close_to(b->x1, 80) && close_to(b->y0, 20) && close_to(b->y1, 60));
    // Four cubics: move, 4 x cubic, close.
    EXPECT_EQ(p.size(), 6u);
}

UTEST(stb_outline, add_outline_moves_the_commands) {
    outline a;
    a.add_rect(0, 0, 10, 10);
    outline b;
    b.add_outline(a, matrix::translation(100, 50));
    const auto bb = b.bounds();
    EXPECT_EQ(bb->x0, 100.f);
    EXPECT_EQ(bb->y0, 50.f);
    EXPECT_EQ(bb->x1, 110.f);
    EXPECT_EQ(bb->y1, 60.f);
}

UTEST(stb_outline, add_outline_to_itself_doubles_it) {
    outline a;
    a.add_rect(0, 0, 10, 10);
    const std::size_t n = a.size();
    a.add_outline(a, matrix::translation(20, 0));
    ASSERT_EQ(a.size(), 2 * n);
    EXPECT_EQ(a.bounds()->x1, 30.f);
}

UTEST(stb_outline, transform_in_place_and_on_a_copy_agree) {
    outline a;
    a.move_to(1, 2).quad_to(3, 4, 5, 6).close();
    const matrix m = matrix::scaling(2, 3).then(matrix::translation(1, 1));
    const outline c = a.transformed(m);
    outline d = a;
    d.transform(m);
    EXPECT_TRUE(c == d);
    EXPECT_TRUE(c.commands()[1].p[0] == pt(7, 13));  // (3, 4) scaled to (6, 12), then moved
    EXPECT_TRUE(*d.current_point() == pt(3, 7));  // after close: the (moved) start (1, 2)
    d.line_to(0, 0);                                   // a new contour from the moved start
    EXPECT_TRUE(d.commands()[d.size() - 2].p[0] == pt(3, 7));
}

UTEST(stb_outline, built_from_a_command_list_follows_the_same_rules) {
    std::vector<outline_command> list(2);
    list[0].op = outline_op::line_to;  // no move_to first
    list[0].p[0] = {4, 5};
    list[1].op = outline_op::close;
    const outline p(list);
    ASSERT_EQ(p.size(), 3u);
    EXPECT_TRUE(p.commands()[0].op == outline_op::move_to);
    EXPECT_TRUE(p.commands()[0].p[0] == pt(0, 0));
}

UTEST(stb_outline, clear_resets_everything) {
    outline p;
    p.add_circle(5, 5, 3);
    p.clear();
    EXPECT_TRUE(p.empty());
    EXPECT_FALSE(p.current_point().has_value());
    p.line_to(1, 1);
    EXPECT_TRUE(p.commands()[0].op == outline_op::move_to);
}
