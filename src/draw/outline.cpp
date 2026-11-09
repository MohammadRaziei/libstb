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

void matrix::map(const point* src, point* dst, std::size_t n) const noexcept {
    for (std::size_t i = 0; i < n; ++i) dst[i] = map(src[i]);  // reads src[i] before writing dst[i]: in place is fine
}

outline_bounds matrix::map_rect(const outline_bounds& r) const noexcept {
    const point corners[4] = {map(r.x0, r.y0), map(r.x1, r.y0), map(r.x1, r.y1), map(r.x0, r.y1)};
    outline_bounds out{corners[0].x, corners[0].y, corners[0].x, corners[0].y};
    for (int i = 1; i < 4; ++i) {
        out.x0 = std::min(out.x0, corners[i].x);
        out.y0 = std::min(out.y0, corners[i].y);
        out.x1 = std::max(out.x1, corners[i].x);
        out.y1 = std::max(out.y1, corners[i].y);
    }
    return out;
}

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

outline& outline::arc_to(float rx_in, float ry_in, float x_axis_rotation_deg, bool large_arc, bool sweep, float x,
                         float y) {
    ensure_start();
    const point from = current_;
    if (!std::isfinite(rx_in) || !std::isfinite(ry_in) || !std::isfinite(x_axis_rotation_deg) ||
        !std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(x) || !std::isfinite(y))
        return line_to(x, y);
    if (from.x == x && from.y == y) return *this;  // F.6.2: the same end points leave the arc out
    double rx = std::fabs(double(rx_in)), ry = std::fabs(double(ry_in));
    if (rx == 0.0 || ry == 0.0) return line_to(x, y);

    // F.6.5: from the end points to the centre. All in double: the result is rounded to float once.
    constexpr double pi = 3.14159265358979323846;
    const double phi = double(x_axis_rotation_deg) * pi / 180.0;
    const double cos_phi = std::cos(phi), sin_phi = std::sin(phi);
    const double hx = (double(from.x) - double(x)) * 0.5, hy = (double(from.y) - double(y)) * 0.5;
    const double x1p = cos_phi * hx + sin_phi * hy;
    const double y1p = -sin_phi * hx + cos_phi * hy;

    const double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);  // F.6.6: radii too small
    if (lambda > 1.0) {
        const double k = std::sqrt(lambda);
        rx *= k;
        ry *= k;
    }
    const double rx2 = rx * rx, ry2 = ry * ry;
    const double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
    double coef = 0.0;
    if (den > 0.0) {
        coef = std::sqrt(std::max(0.0, (rx2 * ry2 - den) / den));
        if (large_arc == sweep) coef = -coef;
    }
    const double cxp = coef * rx * y1p / ry;
    const double cyp = -coef * ry * x1p / rx;
    const double cx = cos_phi * cxp - sin_phi * cyp + (double(from.x) + double(x)) * 0.5;
    const double cy = sin_phi * cxp + cos_phi * cyp + (double(from.y) + double(y)) * 0.5;

    const double ux = (x1p - cxp) / rx, uy = (y1p - cyp) / ry;
    const double vx = (-x1p - cxp) / rx, vy = (-y1p - cyp) / ry;
    const double theta1 = std::atan2(uy, ux);
    double delta = std::atan2(vy, vx) - theta1;
    if (!sweep && delta > 0.0) delta -= 2.0 * pi;
    if (sweep && delta < 0.0) delta += 2.0 * pi;

    // One cubic per quarter turn (a tiny overshoot over a quarter does not make another segment).
    const int segments = std::max(1, int(std::ceil(std::fabs(delta) / (pi * 0.5) - 1e-9)));
    const double step = delta / segments;
    const double t = 4.0 / 3.0 * std::tan(step * 0.25);
    auto at = [&](double a, double& px, double& py, double& dx, double& dy) {
        const double ca = std::cos(a), sa = std::sin(a);
        px = cx + rx * ca * cos_phi - ry * sa * sin_phi;
        py = cy + rx * ca * sin_phi + ry * sa * cos_phi;
        dx = -rx * sa * cos_phi - ry * ca * sin_phi;
        dy = -rx * sa * sin_phi + ry * ca * cos_phi;
    };
    reserve(commands_.size() + std::size_t(segments));
    for (int i = 0; i < segments; ++i) {
        double p1x, p1y, d1x, d1y, p2x, p2y, d2x, d2y;
        at(theta1 + step * i, p1x, p1y, d1x, d1y);
        at(theta1 + step * (i + 1), p2x, p2y, d2x, d2y);
        const bool last = i == segments - 1;
        cubic_to(float(p1x + t * d1x), float(p1y + t * d1y), float(p2x - t * d2x), float(p2y - t * d2y),
                 last ? x : float(p2x), last ? y : float(p2y));  // the last one ends exactly where asked
    }
    return *this;
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

// ----------------------------------------------------------------------- tight_bounds

namespace {

struct box_acc {
    bool any = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    void add(double x, double y) {
        if (!any) {
            x0 = x1 = x;
            y0 = y1 = y;
            any = true;
            return;
        }
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
};

// Appends the roots in the open interval (0, 1) of  a2 t^2 + a1 t + a0 = 0 to out[n...]; returns the new n.
// A degenerate quadratic is a line (or nothing).
int add_unit_roots(double a2, double a1, double a0, double* out, int n) {
    auto keep = [&](double t) {
        if (t > 0.0 && t < 1.0) out[n++] = t;
    };
    if (a2 == 0.0) {
        if (a1 != 0.0) keep(-a0 / a1);
        return n;
    }
    const double disc = a1 * a1 - 4.0 * a2 * a0;
    if (disc < 0.0) return n;
    const double root = std::sqrt(disc);
    const double q = -0.5 * (a1 + (a1 < 0.0 ? -root : root));  // no cancellation between a1 and the root
    if (q != 0.0) {
        keep(q / a2);
        keep(a0 / q);
    }  // q == 0: a1 == 0 and disc == 0, the double root is t = 0, outside the open interval
    return n;
}

bool all_finite(const outline_command& c) {
    for (int k = 0; k < point_count(c.op); ++k)
        if (!std::isfinite(c.p[k].x) || !std::isfinite(c.p[k].y)) return false;
    return true;
}

}  // namespace

std::optional<outline_bounds> outline::tight_bounds() const noexcept {
    box_acc box;
    point cur{}, start{};
    for (const outline_command& c : commands_) {
        if (!all_finite(c)) continue;
        switch (c.op) {
            case outline_op::move_to:
                cur = start = c.p[0];
                box.add(cur.x, cur.y);
                break;
            case outline_op::line_to:
                box.add(c.p[0].x, c.p[0].y);
                cur = c.p[0];
                break;
            case outline_op::quad_to: {
                const double x[3] = {cur.x, c.p[0].x, c.p[1].x}, y[3] = {cur.y, c.p[0].y, c.p[1].y};
                // B'(t) / 2 = (p1 - p0) + t (p0 - 2 p1 + p2): a line in t, one root per axis.
                double t[2];
                int n = add_unit_roots(0.0, x[0] - 2.0 * x[1] + x[2], x[1] - x[0], t, 0);
                n = add_unit_roots(0.0, y[0] - 2.0 * y[1] + y[2], y[1] - y[0], t, n);
                for (int i = 0; i < n; ++i) {
                    const double u = 1.0 - t[i];
                    box.add(u * u * x[0] + 2.0 * u * t[i] * x[1] + t[i] * t[i] * x[2],
                            u * u * y[0] + 2.0 * u * t[i] * y[1] + t[i] * t[i] * y[2]);
                }
                box.add(c.p[1].x, c.p[1].y);
                cur = c.p[1];
                break;
            }
            case outline_op::cubic_to: {
                const double x[4] = {cur.x, c.p[0].x, c.p[1].x, c.p[2].x}, y[4] = {cur.y, c.p[0].y, c.p[1].y, c.p[2].y};
                // B'(t) / 3 = a (1 - t)^2 + 2 b (1 - t) t + d t^2 with a, b, d the differences of the control
                // points: a quadratic in t, up to two roots per axis.
                double t[4];
                int n = 0;
                {
                    const double a = x[1] - x[0], b = x[2] - x[1], d = x[3] - x[2];
                    n = add_unit_roots(a - 2.0 * b + d, 2.0 * (b - a), a, t, n);
                }
                {
                    const double a = y[1] - y[0], b = y[2] - y[1], d = y[3] - y[2];
                    n = add_unit_roots(a - 2.0 * b + d, 2.0 * (b - a), a, t, n);
                }
                for (int i = 0; i < n; ++i) {
                    const double u = 1.0 - t[i];
                    const double w0 = u * u * u, w1 = 3.0 * u * u * t[i], w2 = 3.0 * u * t[i] * t[i], w3 = t[i] * t[i] * t[i];
                    box.add(w0 * x[0] + w1 * x[1] + w2 * x[2] + w3 * x[3], w0 * y[0] + w1 * y[1] + w2 * y[2] + w3 * y[3]);
                }
                box.add(c.p[2].x, c.p[2].y);
                cur = c.p[2];
                break;
            }
            case outline_op::close: cur = start; break;
        }
    }
    if (!box.any) return std::nullopt;
    return outline_bounds{float(box.x0), float(box.y0), float(box.x1), float(box.y1)};
}

}  // namespace stb
