#include "draw/raster/dash.hpp"

#include <algorithm>
#include <cmath>

namespace stb::detail {

namespace {

constexpr float coord_limit = 1000000.f;
constexpr std::size_t max_commands = std::size_t(1) << 22;

bool finite(point p) noexcept { return std::isfinite(p.x) && std::isfinite(p.y); }

point limited(point p) noexcept {
    p.x = std::max(-coord_limit, std::min(coord_limit, p.x));
    p.y = std::max(-coord_limit, std::min(coord_limit, p.y));
    return p;
}

outline_command make(outline_op op, point a = {}, point b = {}, point c = {}) {
    outline_command cmd;
    cmd.op = op;
    cmd.p[0] = a;
    cmd.p[1] = b;
    cmd.p[2] = c;
    return cmd;
}

// --- flattening cubics -------------------------------------------------------------------------------

struct bezier {
    float x1, y1, x2, y2, x3, y3, x4, y4;
};

// Splits a cubic at t = 1/2 into `first` and `second` (`first` may alias `b`'s storage neighbour: the
// callers keep a stack and write the halves into adjacent slots).
void split_bezier(const bezier& b, bezier& first, bezier& second) noexcept {
    float c = (b.x2 + b.x3) * 0.5f;
    first.x2 = (b.x1 + b.x2) * 0.5f;
    second.x3 = (b.x3 + b.x4) * 0.5f;
    first.x1 = b.x1;
    second.x4 = b.x4;
    first.x3 = (first.x2 + c) * 0.5f;
    second.x2 = (second.x3 + c) * 0.5f;
    first.x4 = second.x1 = (first.x3 + second.x2) * 0.5f;

    c = (b.y2 + b.y3) * 0.5f;
    first.y2 = (b.y1 + b.y2) * 0.5f;
    second.y3 = (b.y3 + b.y4) * 0.5f;
    first.y1 = b.y1;
    second.y4 = b.y4;
    first.y3 = (first.y2 + c) * 0.5f;
    second.y2 = (second.y3 + c) * 0.5f;
    first.y4 = second.y1 = (first.y3 + second.y2) * 0.5f;
}

// Calls `emit(point)` for the end of each line segment that approximates the cubic from `from`.
template <class Emit>
void flatten_cubic(point from, point c1, point c2, point to, Emit&& emit) {
    constexpr float threshold = 0.25f;

    bezier stack[32];
    stack[0] = {from.x, from.y, c1.x, c1.y, c2.x, c2.y, to.x, to.y};
    bezier* b = stack;
    while (b >= stack) {
        const float y4y1 = b->y4 - b->y1;
        const float x4x1 = b->x4 - b->x1;
        float l = std::fabs(x4x1) + std::fabs(y4y1);
        float d;
        if (l > 1.f) {
            d = std::fabs(x4x1 * (b->y1 - b->y2) - y4y1 * (b->x1 - b->x2)) +
                std::fabs(x4x1 * (b->y1 - b->y3) - y4y1 * (b->x1 - b->x3));
        } else {
            d = std::fabs(b->x1 - b->x2) + std::fabs(b->y1 - b->y2) + std::fabs(b->x1 - b->x3) + std::fabs(b->y1 - b->y3);
            l = 1.f;
        }

        if (d < threshold * l || b == stack + 31) {
            emit(point{b->x4, b->y4});
            --b;
        } else {
            split_bezier(*b, *(b + 1), *b);
            ++b;
        }
    }
}

// --- the dasher -----------------------------------------------------------------------------------------

class dasher {
public:
    dasher(const float* dashes, std::size_t n, float offset, std::vector<outline_command>& out)
        : dashes_(dashes), n_(n), out_(out) {
        float sum = 0.f;
        for (std::size_t i = 0; i < n; ++i) sum += dashes[i];
        if (n % 2 == 1) sum *= 2.f;

        start_phase_ = std::fmod(offset, sum);
        if (start_phase_ < 0.f) start_phase_ += sum;
        while (start_phase_ > 0.f && start_phase_ >= dashes_[start_index_ % n_]) {
            start_phase_ -= dashes_[start_index_ % n_];
            start_toggle_ = !start_toggle_;
            ++start_index_;
        }
        phase_ = start_phase_;
        index_ = start_index_;
        toggle_ = start_toggle_;
    }

    void move_to(point p) {
        if (start_toggle_) out_.push_back(make(outline_op::move_to, p));
        current_ = p;
        phase_ = start_phase_;
        index_ = start_index_;
        toggle_ = start_toggle_;
    }

    // A straight line to p1 (a close command comes here too, as the line back to the contour start).
    void line_to(point p1) {
        const point p0 = current_;
        const float dx = p1.x - p0.x;
        const float dy = p1.y - p0.y;
        const float dist0 = std::sqrt(dx * dx + dy * dy);
        float dist1 = 0.f;

        while (dist0 - dist1 > dashes_[index_ % n_] - phase_) {
            if (out_.size() >= max_commands) return;
            dist1 += dashes_[index_ % n_] - phase_;
            const float a = dist1 / dist0;
            const point p{p0.x + a * dx, p0.y + a * dy};
            out_.push_back(make(toggle_ ? outline_op::line_to : outline_op::move_to, p));

            phase_ = 0.f;
            toggle_ = !toggle_;
            ++index_;
        }

        if (toggle_) out_.push_back(make(outline_op::line_to, p1));

        phase_ += dist0 - dist1;
        current_ = p1;
    }

private:
    const float* dashes_;
    std::size_t n_;
    std::vector<outline_command>& out_;
    float start_phase_ = 0.f;
    float phase_ = 0.f;
    std::size_t start_index_ = 0;
    std::size_t index_ = 0;
    bool start_toggle_ = true;
    bool toggle_ = true;
    point current_;
};

}  // namespace

std::vector<outline_command> to_cubic_commands(const outline& p) {
    std::vector<outline_command> out;
    out.reserve(p.size());

    point start, cur;
    for (const outline_command& c : p) {
        for (int k = 0; k < point_count(c.op); ++k) {
            if (!finite(c.p[k])) return {};
        }
        switch (c.op) {
            case outline_op::move_to:
                start = cur = limited(c.p[0]);
                out.push_back(make(outline_op::move_to, cur));
                break;
            case outline_op::line_to:
                cur = limited(c.p[0]);
                out.push_back(make(outline_op::line_to, cur));
                break;
            case outline_op::quad_to: {
                // The cubic with the same shape: its controls are two thirds of the way from the
                // end points to the quadratic's control.
                const point q = limited(c.p[0]);
                const point e = limited(c.p[1]);
                const point cp1{2.f / 3.f * q.x + 1.f / 3.f * cur.x, 2.f / 3.f * q.y + 1.f / 3.f * cur.y};
                const point cp2{2.f / 3.f * q.x + 1.f / 3.f * e.x, 2.f / 3.f * q.y + 1.f / 3.f * e.y};
                out.push_back(make(outline_op::cubic_to, cp1, cp2, e));
                cur = e;
                break;
            }
            case outline_op::cubic_to:
                cur = limited(c.p[2]);
                out.push_back(make(outline_op::cubic_to, limited(c.p[0]), limited(c.p[1]), cur));
                break;
            case outline_op::close:
                out.push_back(make(outline_op::close, start));
                cur = start;
                break;
        }
    }
    return out;
}

bool valid_dashes(const float* dashes, std::size_t n) noexcept {
    if (n == 0) return false;
    float sum = 0.f;
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(dashes[i]) || dashes[i] < 0.f) return false;
        sum += dashes[i];
    }
    return sum > 0.f && std::isfinite(sum);
}

std::vector<outline_command> dash_commands(const std::vector<outline_command>& cubics, const float* dashes, std::size_t n,
                                        float offset) {
    std::vector<outline_command> out;
    out.reserve(cubics.size() * 2);
    dasher d(dashes, n, offset, out);

    point current;
    for (const outline_command& c : cubics) {
        switch (c.op) {
            case outline_op::move_to:
                d.move_to(c.p[0]);
                current = c.p[0];
                break;
            case outline_op::line_to:
            case outline_op::close:
                d.line_to(c.p[0]);
                current = c.p[0];
                break;
            case outline_op::cubic_to:
                flatten_cubic(current, c.p[0], c.p[1], c.p[2], [&](point p) { d.line_to(p); });
                current = c.p[2];
                break;
            case outline_op::quad_to:
                break;  // not in the list: to_cubic_commands converted them
        }
        if (out.size() >= max_commands) break;
    }
    return out;
}

}  // namespace stb::detail
