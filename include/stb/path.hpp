#pragma once

// Vector geometry: points, affine matrices and paths. No pixels here: a path only says WHERE
// something is; stb/raster.hpp turns it into coverage and stb::canvas::add_path draws it.

#include <cstddef>
#include <optional>
#include <vector>

#include "stb/export.hpp"

namespace stb {

struct point {
    float x = 0, y = 0;
};
constexpr bool operator==(point a, point b) noexcept { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(point a, point b) noexcept { return !(a == b); }

// A 2D affine transform, in the SVG / canvas convention:
//     x' = a * x + c * y + e
//     y' = b * x + d * y + f
// The default value is the identity. Building one reads like the SVG transform attribute: each call
// changes the matrix so that its operation is applied to a point FIRST, before everything the
// matrix already did:
//
//     stb::matrix m;
//     m.translate(100, 50).rotate(0.3f).scale(2);   // scale, then rotate, then translate
//
// matrix::map and matrix::then do their arithmetic in the library (compiled without fused
// multiply-add), so the same path gives the same pixels on every platform.
struct STB_API matrix {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    static matrix translation(float tx, float ty) noexcept;
    static matrix scaling(float sx, float sy) noexcept;
    static matrix scaling(float s) noexcept { return scaling(s, s); }
    // `radians` turn x towards y (clockwise on a screen whose y grows downwards).
    static matrix rotation(float radians) noexcept;
    // x += tan(shx) * y and y += tan(shy) * x.
    static matrix shearing(float shx, float shy) noexcept;

    matrix& translate(float tx, float ty) noexcept { return *this = translation(tx, ty).then(*this); }
    matrix& scale(float sx, float sy) noexcept { return *this = scaling(sx, sy).then(*this); }
    matrix& scale(float s) noexcept { return scale(s, s); }
    matrix& rotate(float radians) noexcept { return *this = rotation(radians).then(*this); }
    matrix& shear(float shx, float shy) noexcept { return *this = shearing(shx, shy).then(*this); }

    // The matrix that applies `*this` first and `next` second.
    matrix then(const matrix& next) const noexcept;

    point map(point p) const noexcept;
    point map(float x, float y) const noexcept { return map(point{x, y}); }

    // The inverse, or nullopt if the matrix cannot be inverted (its determinant is 0).
    std::optional<matrix> inverse() const noexcept;

    bool is_identity() const noexcept { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
};
constexpr bool operator==(const matrix& x, const matrix& y) noexcept {
    return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.e == y.e && x.f == y.f;
}
constexpr bool operator!=(const matrix& x, const matrix& y) noexcept { return !(x == y); }

// One step of a path (and of a glyph outline).
//   move_to    p[0]                      starts a contour
//   line_to    p[0]                      a straight line to p[0]
//   quad_to    p[0] control, p[1] end    a quadratic curve (TrueType outlines)
//   cubic_to   p[0], p[1] controls, p[2] end   a cubic curve (CFF / OpenType "PostScript" outlines)
//   close      (no points)               ends the contour
// The points a command does not use are zero. A TrueType font gives only line_to and quad_to, a CFF
// font only line_to and cubic_to: the two are never converted into each other, so the outline is
// exactly what the font holds.
enum class path_op { move_to, line_to, quad_to, cubic_to, close };

// How many of a command's points are used: 1, 1, 2, 3 and 0, in the order of path_op.
constexpr int point_count(path_op op) noexcept {
    return op == path_op::move_to || op == path_op::line_to ? 1 : op == path_op::quad_to ? 2 : op == path_op::cubic_to ? 3 : 0;
}

struct path_command {
    path_op op = path_op::close;
    point p[3] = {};
};
constexpr bool operator==(const path_command& a, const path_command& b) noexcept {
    return a.op == b.op && a.p[0] == b.p[0] && a.p[1] == b.p[1] && a.p[2] == b.p[2];
}
constexpr bool operator!=(const path_command& a, const path_command& b) noexcept { return !(a == b); }

// The smallest axis-aligned box holding all the points of a path, control points included (so a
// curve may bulge less than the box says, never more).
struct path_bounds {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // x0 <= x1 and y0 <= y1
    float width() const noexcept { return x1 - x0; }
    float height() const noexcept { return y1 - y0; }
};

// A vector shape: any number of contours made of straight lines and quadratic or cubic curves, in
// float coordinates (x to the right, y down, like pixels). A path is a plain value: copying it
// copies the commands, moving it is free.
//
//     stb::path p;
//     p.move_to(10, 10).line_to(90, 10).quad_to(120, 50, 90, 90).close();   // chain the calls
//     p.add_rounded_rect(20, 20, 60, 40, 8);                                // shapes in one call
//     p.add_path(other, stb::matrix::translation(200, 0));                  // another path, moved
//
// move_to starts a contour; line_to, quad_to and cubic_to continue it from the current point;
// close joins its end to its start. Calling a drawing command on an empty path starts at (0, 0), and
// after close a new contour begins at the point the closed one started from (the SVG rule), so
// there is never a contour without a start. Nothing is checked about the coordinates: NaN or
// infinity are kept as they are, and the rasterizer decides what to do with them (it skips
// them).
//
// Quadratic curves stay quadratic in the path; it is the rasterizer that converts them.
class STB_API path {
public:
    path() = default;
    // A path made of `commands` (for example font::outline(glyph)). They go through the same
    // rules as the builder calls, so a list that starts without move_to still gives a valid path.
    explicit path(const std::vector<path_command>& commands);

    path& move_to(float x, float y);
    path& move_to(point p) { return move_to(p.x, p.y); }
    path& line_to(float x, float y);
    path& line_to(point p) { return line_to(p.x, p.y); }
    path& quad_to(float cx, float cy, float x, float y);
    path& quad_to(point control, point to) { return quad_to(control.x, control.y, to.x, to.y); }
    path& cubic_to(float c1x, float c1y, float c2x, float c2y, float x, float y);
    path& cubic_to(point c1, point c2, point to) { return cubic_to(c1.x, c1.y, c2.x, c2.y, to.x, to.y); }
    path& close();

    // Whole shapes, each a closed contour of its own, drawn clockwise on screen (x to the right,
    // y down) so they all fill the same way. A negative w or h is the same box as a positive one
    // on the other side of x or y, and radii are taken as absolute values; nothing is added when
    // w, h or a radius of an ellipse is 0 (add_rounded_rect with both radii 0 is a plain
    // rectangle). A rounded rectangle's radii are limited to half the side.
    path& add_rect(float x, float y, float w, float h);
    path& add_rounded_rect(float x, float y, float w, float h, float rx, float ry);
    path& add_rounded_rect(float x, float y, float w, float h, float radius) { return add_rounded_rect(x, y, w, h, radius, radius); }
    path& add_ellipse(float cx, float cy, float rx, float ry);
    path& add_circle(float cx, float cy, float r) { return add_ellipse(cx, cy, r, r); }

    // Appends the commands of another path or of a command list (a glyph outline), optionally
    // moved through `m`. Adding a path to itself is allowed.
    path& add_path(const path& other, const matrix& m = {});
    path& add_commands(const std::vector<path_command>& commands, const matrix& m = {});

    // A copy cut into dashes: `dashes` are alternating "on" and "off" lengths in path units (an odd count
    // repeats the list once more with on and off swapped), `offset` is how far into the pattern the path
    // starts. The result has only straight lines, one contour per dash (curves are flattened to within a
    // quarter of a unit, closed contours become open ones). A pattern with a negative or non-finite entry,
    // or whose sum is 0, gives an unchanged copy.
    path dashed(const std::vector<float>& dashes, float offset = 0.f) const;

    // Moves every point through `m`, in place, or on a copy.
    path& transform(const matrix& m);
    path transformed(const matrix& m) const;

    void clear() noexcept;
    void reserve(std::size_t commands) { commands_.reserve(commands); }

    bool empty() const noexcept { return commands_.empty(); }
    std::size_t size() const noexcept { return commands_.size(); }
    const std::vector<path_command>& commands() const noexcept { return commands_; }
    std::vector<path_command>::const_iterator begin() const noexcept { return commands_.begin(); }
    std::vector<path_command>::const_iterator end() const noexcept { return commands_.end(); }

    // Where the next line_to would start; nullopt for an empty path. After close it is the start
    // of the closed contour.
    std::optional<point> current_point() const noexcept;

    // The control-point box (see path_bounds); nullopt when the path has no points.
    std::optional<path_bounds> bounds() const noexcept;

    friend bool operator==(const path& a, const path& b) noexcept { return a.commands_ == b.commands_; }
    friend bool operator!=(const path& a, const path& b) noexcept { return !(a == b); }

private:
    // A drawing command needs a current point: add the implicit move_to when there is none.
    void ensure_start();

    std::vector<path_command> commands_;
    point start_;    // where the open contour began (what close returns to)
    point current_;  // the last point
    bool has_start_ = false;  // false on an empty path and right after close
};

}  // namespace stb
