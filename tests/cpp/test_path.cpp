#include <cmath>
#include <vector>

#include "test_common.hpp"

// stb::matrix and stb::path: the geometry the rasterizer works from.

namespace {

using stb::matrix;
using stb::path;
using stb::path_command;
using stb::path_op;
using stb::point;

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
bool near(point a, point b) { return near(a.x, b.x) && near(a.y, b.y); }

std::vector<path_op> ops(const path& p) {
    std::vector<path_op> r;
    for (const path_command& c : p) r.push_back(c.op);
    return r;
}

}  // namespace

// ------------------------------------------------------------------------------ matrix

UTEST(stb_matrix, identity_by_default) {
    const matrix m;
    EXPECT_TRUE(m.is_identity());
    EXPECT_TRUE(m.map(3, -4) == (point{3, -4}));
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
    EXPECT_TRUE(near(m.map(1, 1), point{12, 2}));
    // The same with then(): scale first, translate second.
    const matrix n = matrix::scaling(2).then(matrix::translation(10, 0));
    EXPECT_TRUE(m == n);
}

UTEST(stb_matrix, rotation_turns_x_towards_y) {
    const matrix r = matrix::rotation(1.57079632679f);
    EXPECT_TRUE(near(r.map(1, 0), point{0, 1}));
    EXPECT_TRUE(near(r.map(0, 1), point{-1, 0}));
}

UTEST(stb_matrix, shear) {
    const matrix s = matrix::shearing(0.f, 0.f);
    EXPECT_TRUE(s.is_identity());
    const matrix t = matrix::shearing(0.7853981634f, 0.f);  // x += tan(45 degrees) * y
    EXPECT_TRUE(near(t.map(0, 2), point{2, 2}));
}

UTEST(stb_matrix, inverse_undoes_the_matrix) {
    matrix m;
    m.translate(5, -3).rotate(0.4f).scale(2, 3);
    const auto inv = m.inverse();
    ASSERT_TRUE(inv.has_value());
    const point back = inv->map(m.map(7, 9));
    EXPECT_TRUE(near(back, point{7, 9}));
}

UTEST(stb_matrix, singular_matrix_has_no_inverse) {
    EXPECT_FALSE(matrix::scaling(0, 1).inverse().has_value());
}

// -------------------------------------------------------------------------------- path

UTEST(stb_path, builder_records_commands) {
    path p;
    p.move_to(1, 2).line_to(3, 4).quad_to(5, 6, 7, 8).cubic_to(1, 1, 2, 2, 3, 3).close();
    ASSERT_EQ(p.size(), 5u);
    EXPECT_TRUE(ops(p) == (std::vector<path_op>{path_op::move_to, path_op::line_to, path_op::quad_to,
                                                path_op::cubic_to, path_op::close}));
    EXPECT_TRUE(p.commands()[2].p[0] == (point{5, 6}));
    EXPECT_TRUE(p.commands()[2].p[1] == (point{7, 8}));
}

UTEST(stb_path, empty_path_starts_at_the_origin) {
    path p;
    EXPECT_TRUE(p.empty());
    EXPECT_FALSE(p.current_point().has_value());
    EXPECT_FALSE(p.bounds().has_value());
    p.line_to(5, 5);
    ASSERT_EQ(p.size(), 2u);
    EXPECT_TRUE(p.commands()[0].op == path_op::move_to);
    EXPECT_TRUE(p.commands()[0].p[0] == (point{0, 0}));
}

UTEST(stb_path, a_command_after_close_starts_a_new_contour_at_the_old_start) {
    path p;
    p.move_to(10, 10).line_to(20, 10).line_to(20, 20).close().line_to(30, 30);
    ASSERT_EQ(p.size(), 6u);
    EXPECT_TRUE(p.commands()[4].op == path_op::move_to);
    EXPECT_TRUE(p.commands()[4].p[0] == (point{10, 10}));
    EXPECT_TRUE(p.commands()[5].op == path_op::line_to);
}

UTEST(stb_path, close_on_empty_or_closed_path_does_nothing) {
    path p;
    p.close();
    EXPECT_TRUE(p.empty());
    p.move_to(0, 0).line_to(1, 1).close().close();
    EXPECT_EQ(p.size(), 3u);
}

UTEST(stb_path, current_point_follows_the_last_command_and_close) {
    path p;
    p.move_to(1, 2);
    EXPECT_TRUE(*p.current_point() == (point{1, 2}));
    p.line_to(3, 4);
    EXPECT_TRUE(*p.current_point() == (point{3, 4}));
    p.close();
    EXPECT_TRUE(*p.current_point() == (point{1, 2}));
}

UTEST(stb_path, bounds_hold_every_point_including_controls) {
    path p;
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

UTEST(stb_path, rect_is_one_closed_clockwise_contour) {
    path p;
    p.add_rect(10, 20, 30, 40);
    const auto b = p.bounds();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->x0, 10.f);
    EXPECT_EQ(b->y0, 20.f);
    EXPECT_EQ(b->x1, 40.f);
    EXPECT_EQ(b->y1, 60.f);
    EXPECT_TRUE(p.commands().front().op == path_op::move_to);
    EXPECT_TRUE(p.commands().back().op == path_op::close);
    EXPECT_TRUE(p.commands()[1].p[0] == (point{40, 20}));  // first goes right, then down: clockwise
    EXPECT_TRUE(p.commands()[2].p[0] == (point{40, 60}));
}

UTEST(stb_path, negative_sides_give_the_same_rect) {
    path a, b;
    a.add_rect(10, 20, 30, 40);
    b.add_rect(40, 60, -30, -40);
    EXPECT_TRUE(a == b);
}

UTEST(stb_path, empty_shapes_add_nothing) {
    path p;
    p.add_rect(0, 0, 0, 5).add_rect(0, 0, 5, 0).add_rounded_rect(0, 0, 0, 0, 3).add_ellipse(0, 0, 0, 4).add_circle(1, 1, 0);
    EXPECT_TRUE(p.empty());
}

UTEST(stb_path, rounded_rect_without_radius_is_a_rect_and_radius_is_limited) {
    path plain, zero;
    plain.add_rect(0, 0, 20, 10);
    zero.add_rounded_rect(0, 0, 20, 10, 0);
    EXPECT_TRUE(plain == zero);

    path big, half;
    big.add_rounded_rect(0, 0, 20, 10, 100);
    half.add_rounded_rect(0, 0, 20, 10, 10, 5);
    EXPECT_TRUE(big == half);
    const auto b = big.bounds();
    EXPECT_EQ(b->x0, 0.f);
    EXPECT_EQ(b->x1, 20.f);
    EXPECT_EQ(b->y0, 0.f);
    EXPECT_EQ(b->y1, 10.f);
}

UTEST(stb_path, ellipse_touches_its_box) {
    path p;
    p.add_ellipse(50, 40, 30, 20);
    const auto b = p.bounds();
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(near(b->x0, 20) && near(b->x1, 80) && near(b->y0, 20) && near(b->y1, 60));
    // Four cubics: move, 4 x cubic, close.
    EXPECT_EQ(p.size(), 6u);
}

UTEST(stb_path, add_path_moves_the_commands) {
    path a;
    a.add_rect(0, 0, 10, 10);
    path b;
    b.add_path(a, matrix::translation(100, 50));
    const auto bb = b.bounds();
    EXPECT_EQ(bb->x0, 100.f);
    EXPECT_EQ(bb->y0, 50.f);
    EXPECT_EQ(bb->x1, 110.f);
    EXPECT_EQ(bb->y1, 60.f);
}

UTEST(stb_path, add_path_to_itself_doubles_it) {
    path a;
    a.add_rect(0, 0, 10, 10);
    const std::size_t n = a.size();
    a.add_path(a, matrix::translation(20, 0));
    ASSERT_EQ(a.size(), 2 * n);
    EXPECT_EQ(a.bounds()->x1, 30.f);
}

UTEST(stb_path, transform_in_place_and_on_a_copy_agree) {
    path a;
    a.move_to(1, 2).quad_to(3, 4, 5, 6).close();
    const matrix m = matrix::scaling(2, 3).then(matrix::translation(1, 1));
    const path c = a.transformed(m);
    path d = a;
    d.transform(m);
    EXPECT_TRUE(c == d);
    EXPECT_TRUE(c.commands()[1].p[0] == (point{7, 13}));  // (3, 4) scaled to (6, 12), then moved
    EXPECT_TRUE(*d.current_point() == (point{3, 7}));  // after close: the (moved) start (1, 2)
    d.line_to(0, 0);                                   // a new contour from the moved start
    EXPECT_TRUE(d.commands()[d.size() - 2].p[0] == (point{3, 7}));
}

UTEST(stb_path, built_from_a_command_list_follows_the_same_rules) {
    std::vector<path_command> list(2);
    list[0].op = path_op::line_to;  // no move_to first
    list[0].p[0] = {4, 5};
    list[1].op = path_op::close;
    const path p(list);
    ASSERT_EQ(p.size(), 3u);
    EXPECT_TRUE(p.commands()[0].op == path_op::move_to);
    EXPECT_TRUE(p.commands()[0].p[0] == (point{0, 0}));
}

UTEST(stb_path, clear_resets_everything) {
    path p;
    p.add_circle(5, 5, 3);
    p.clear();
    EXPECT_TRUE(p.empty());
    EXPECT_FALSE(p.current_point().has_value());
    p.line_to(1, 1);
    EXPECT_TRUE(p.commands()[0].op == path_op::move_to);
}
