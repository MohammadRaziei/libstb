#include "stb/outline.hpp"

#include <algorithm>
#include <cmath>

#include "draw/raster/dash.hpp"

namespace stb {

// ----------------------------------------------------------------------------- matrix

matrix matrix::translation(float tx, float ty) noexcept { return matrix{1, 0, 0, 1, tx, ty}; }
matrix matrix::scaling(float sx, float sy) noexcept { return matrix{sx, 0, 0, sy, 0, 0}; }

matrix matrix::rotation(float radians) noexcept {
    const float cs = std::cos(radians);
    const float sn = std::sin(radians);
    return matrix{cs, sn, -sn, cs, 0, 0};
}

matrix matrix::shearing(float shx, float shy) noexcept { return matrix{1, std::tan(shy), std::tan(shx), 1, 0, 0}; }

matrix matrix::then(const matrix& n) const noexcept {
    matrix r;
    r.a = a * n.a + b * n.c;
    r.b = a * n.b + b * n.d;
    r.c = c * n.a + d * n.c;
    r.d = c * n.b + d * n.d;
    r.e = e * n.a + f * n.c + n.e;
    r.f = e * n.b + f * n.d + n.f;
    return r;
}

point matrix::map(point p) const noexcept { return point{p.x * a + p.y * c + e, p.x * b + p.y * d + f}; }

std::optional<matrix> matrix::inverse() const noexcept {
    const float det = a * d - b * c;
    if (det == 0.f) return std::nullopt;
    const float inv = 1.f / det;
    matrix r;
    r.a = d * inv;
    r.b = -b * inv;
    r.c = -c * inv;
    r.d = a * inv;
    r.e = (c * f - d * e) * inv;
    r.f = (b * e - a * f) * inv;
    return r;
}

// ------------------------------------------------------------------------------- outline

namespace {

// The control-point factor of a quarter circle drawn with one cubic.
constexpr float kappa = 0.55228474983079339840f;

outline_command make(outline_op op, point a = {}, point b = {}, point c = {}) {
    outline_command cmd;
    cmd.op = op;
    cmd.p[0] = a;
    cmd.p[1] = b;
    cmd.p[2] = c;
    return cmd;
}

// A box [x, x + w] x [y, y + h] with positive sides: a negative w or h moves the origin instead.
void normalise(float& x, float& y, float& w, float& h) {
    if (w < 0) x += w, w = -w;
    if (h < 0) y += h, h = -h;
}

}  // namespace

outline::outline(const std::vector<outline_command>& commands) { add_commands(commands); }

void outline::ensure_start() {
    if (has_start_) return;
    if (commands_.empty()) {
        move_to(0, 0);
    } else {
        move_to(start_);  // after close: a new contour from where the last one began
    }
}

outline& outline::move_to(float x, float y) {
    commands_.push_back(make(outline_op::move_to, {x, y}));
    start_ = current_ = {x, y};
    has_start_ = true;
    return *this;
}

outline& outline::line_to(float x, float y) {
    ensure_start();
    commands_.push_back(make(outline_op::line_to, {x, y}));
    current_ = {x, y};
    return *this;
}

outline& outline::quad_to(float cx, float cy, float x, float y) {
    ensure_start();
    commands_.push_back(make(outline_op::quad_to, {cx, cy}, {x, y}));
    current_ = {x, y};
    return *this;
}

outline& outline::cubic_to(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    ensure_start();
    commands_.push_back(make(outline_op::cubic_to, {c1x, c1y}, {c2x, c2y}, {x, y}));
    current_ = {x, y};
    return *this;
}

outline& outline::close() {
    if (!has_start_) return *this;  // empty outline, or already closed: nothing to close
    commands_.push_back(make(outline_op::close));
    current_ = start_;
    has_start_ = false;
    return *this;
}

outline& outline::add_rect(float x, float y, float w, float h) {
    normalise(x, y, w, h);
    if (w == 0 || h == 0) return *this;
    reserve(commands_.size() + 6);
    move_to(x, y).line_to(x + w, y).line_to(x + w, y + h).line_to(x, y + h).line_to(x, y);
    return close();
}

outline& outline::add_rounded_rect(float x, float y, float w, float h, float rx, float ry) {
    normalise(x, y, w, h);
    if (w == 0 || h == 0) return *this;
    rx = std::min(std::fabs(rx), w * 0.5f);
    ry = std::min(std::fabs(ry), h * 0.5f);
    if (rx == 0.f && ry == 0.f) return add_rect(x, y, w, h);

    const float right = x + w;
    const float bottom = y + h;
    const float cpx = rx * kappa;
    const float cpy = ry * kappa;

    reserve(commands_.size() + 10);
    move_to(x, y + ry);
    cubic_to(x, y + ry - cpy, x + rx - cpx, y, x + rx, y);
    line_to(right - rx, y);
    cubic_to(right - rx + cpx, y, right, y + ry - cpy, right, y + ry);
    line_to(right, bottom - ry);
    cubic_to(right, bottom - ry + cpy, right - rx + cpx, bottom, right - rx, bottom);
    line_to(x + rx, bottom);
    cubic_to(x + rx - cpx, bottom, x, bottom - ry + cpy, x, bottom - ry);
    line_to(x, y + ry);
    return close();
}

outline& outline::add_ellipse(float cx, float cy, float rx, float ry) {
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    if (rx == 0 || ry == 0) return *this;

    const float left = cx - rx;
    const float top = cy - ry;
    const float right = cx + rx;
    const float bottom = cy + ry;
    const float cpx = rx * kappa;
    const float cpy = ry * kappa;

    reserve(commands_.size() + 6);
    move_to(cx, top);
    cubic_to(cx + cpx, top, right, cy - cpy, right, cy);
    cubic_to(right, cy + cpy, cx + cpx, bottom, cx, bottom);
    cubic_to(cx - cpx, bottom, left, cy + cpy, left, cy);
    cubic_to(left, cy - cpy, cx - cpx, top, cx, top);
    return close();
}

outline& outline::add_commands(const std::vector<outline_command>& commands, const matrix& m) {
    // `commands` may be our own list (add_outline(*this)): work from a count, not from iterators.
    const bool identity = m.is_identity();
    const std::size_t n = commands.size();
    reserve(commands_.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
        outline_command c = commands[i];
        if (!identity) {
            for (int k = 0; k < point_count(c.op); ++k) c.p[k] = m.map(c.p[k]);
        }
        switch (c.op) {
            case outline_op::move_to: move_to(c.p[0]); break;
            case outline_op::line_to: line_to(c.p[0]); break;
            case outline_op::quad_to: quad_to(c.p[0], c.p[1]); break;
            case outline_op::cubic_to: cubic_to(c.p[0], c.p[1], c.p[2]); break;
            case outline_op::close: close(); break;
        }
    }
    return *this;
}

outline& outline::add_outline(const outline& other, const matrix& m) {
    if (&other == this) {
        const std::vector<outline_command> copy = commands_;
        return add_commands(copy, m);
    }
    return add_commands(other.commands_, m);
}

outline& outline::transform(const matrix& m) {
    if (m.is_identity()) return *this;
    for (outline_command& c : commands_) {
        for (int k = 0; k < point_count(c.op); ++k) c.p[k] = m.map(c.p[k]);
    }
    start_ = m.map(start_);
    current_ = m.map(current_);
    return *this;
}

outline outline::dashed(const std::vector<float>& dashes, float offset) const {
    if (!detail::valid_dashes(dashes.data(), dashes.size()) || !std::isfinite(offset)) return *this;
    return outline(detail::dash_commands(detail::to_cubic_commands(*this), dashes.data(), dashes.size(), offset));
}

outline outline::transformed(const matrix& m) const {
    outline r = *this;
    return r.transform(m);
}

void outline::clear() noexcept {
    commands_.clear();
    start_ = current_ = {};
    has_start_ = false;
}

std::optional<point> outline::current_point() const noexcept {
    if (commands_.empty()) return std::nullopt;
    return current_;
}

std::optional<outline_bounds> outline::bounds() const noexcept {
    std::optional<outline_bounds> r;
    for (const outline_command& c : commands_) {
        for (int k = 0; k < point_count(c.op); ++k) {
            const point p = c.p[k];
            if (!r) {
                r = outline_bounds{p.x, p.y, p.x, p.y};
                continue;
            }
            r->x0 = std::min(r->x0, p.x);
            r->y0 = std::min(r->y0, p.y);
            r->x1 = std::max(r->x1, p.x);
            r->y1 = std::max(r->y1, p.y);
        }
    }
    return r;
}

}  // namespace stb
