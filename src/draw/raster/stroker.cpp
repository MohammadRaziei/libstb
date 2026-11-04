#include "draw/raster/stroker.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "draw/raster/fixed.hpp"

namespace stb::detail {

namespace {

constexpr fixed small_cubic_threshold = angle_pi / 8;
constexpr fixed epsilon = 2;  // 1/32 pixel: differences below it count as "the same point"
constexpr fixed arc_cubic_angle = angle_pi / 2;

constexpr bool is_small(fixed v) noexcept { return v > -epsilon && v < epsilon; }
constexpr bool is_small(fvec v) noexcept { return is_small(v.x) && is_small(v.y); }

fvec operator+(fvec a, fvec b) noexcept { return {a.x + b.x, a.y + b.y}; }
fvec operator-(fvec a, fvec b) noexcept { return {a.x - b.x, a.y - b.y}; }

// The direction of a vector (the callers deal with vectors too short to have one).
fixed angle_of(fvec v) noexcept { return atan2_fix(v.x, v.y); }

// --- cubic helpers ------------------------------------------------------------------------------------

// Splits the cubic base[0..3] at its middle into base[0..3] and base[3..6].
void cubic_split(fvec* base) noexcept {
    fixed a, b, c;

    base[6].x = base[3].x;
    a = base[0].x + base[1].x;
    b = base[1].x + base[2].x;
    c = base[2].x + base[3].x;
    base[5].x = c >> 1;
    c += b;
    base[4].x = c >> 2;
    base[1].x = a >> 1;
    a += b;
    base[2].x = a >> 2;
    base[3].x = (a + c) >> 3;

    base[6].y = base[3].y;
    a = base[0].y + base[1].y;
    b = base[1].y + base[2].y;
    c = base[2].y + base[3].y;
    base[5].y = c >> 1;
    c += b;
    base[4].y = c >> 2;
    base[1].y = a >> 1;
    a += b;
    base[2].y = a >> 2;
    base[3].y = (a + c) >> 3;
}

fixed angle_mean(fixed a1, fixed a2) noexcept { return a1 + angle_diff(a1, a2) / 2; }

// Is the cubic so straight that offsetting its control polygon gives a good offset curve? Also
// returns the directions at its start, middle and end.
bool cubic_is_small_enough(const fvec* base, fixed& angle_in, fixed& angle_mid, fixed& angle_out) noexcept {
    const fvec d1 = base[2] - base[3];
    const fvec d2 = base[1] - base[2];
    const fvec d3 = base[0] - base[1];

    const bool close1 = is_small(d1);
    const bool close2 = is_small(d2);
    const bool close3 = is_small(d3);

    if (close1) {
        if (close2) {
            if (!close3) angle_in = angle_mid = angle_out = angle_of(d3);
            // all three tiny: keep the angles the caller passed in
        } else {
            if (close3) {
                angle_in = angle_mid = angle_out = angle_of(d2);
            } else {
                angle_in = angle_mid = angle_of(d2);
                angle_out = angle_of(d3);
            }
        }
    } else {
        if (close2) {
            if (close3) {
                angle_in = angle_mid = angle_out = angle_of(d1);
            } else {
                angle_in = angle_of(d1);
                angle_out = angle_of(d3);
                angle_mid = angle_mean(angle_in, angle_out);
            }
        } else {
            if (close3) {
                angle_in = angle_of(d1);
                angle_mid = angle_out = angle_of(d2);
            } else {
                angle_in = angle_of(d1);
                angle_mid = angle_of(d2);
                angle_out = angle_of(d3);
            }
        }
    }

    const fixed theta1 = fx_abs(angle_diff(angle_in, angle_mid));
    const fixed theta2 = fx_abs(angle_diff(angle_mid, angle_out));
    return theta1 < small_cubic_threshold && theta2 < small_cubic_threshold;
}

// --- one side of the stroke -----------------------------------------------------------------------------

constexpr std::uint8_t tag_on = 1;
constexpr std::uint8_t tag_cubic = 2;
constexpr std::uint8_t tag_begin = 4;  // first point of a contour
constexpr std::uint8_t tag_end = 8;    // last point of a contour
constexpr std::uint8_t tag_begin_end = tag_begin | tag_end;

// The points of one border (the left or the right of the line) as the stroker builds it.
struct border {
    std::vector<fvec> points;
    std::vector<std::uint8_t> tags;
    int start = -1;       // index of the first point of the contour being built, -1 between contours
    bool movable = false;  // may the last point be replaced by the next one (a line extended)?

    void reset() {
        points.clear();
        tags.clear();
        start = -1;
        movable = false;
    }

    void line_to(fvec to, bool next_movable) {
        if (movable) {
            points.back() = to;  // the previous point only marked the end of a straight run
        } else {
            if (int(points.size()) > start && is_small(points.back() - to)) return;  // nothing to add
            points.push_back(to);
            tags.push_back(tag_on);
        }
        movable = next_movable;
    }

    void cubic_to(fvec c1, fvec c2, fvec to) {
        points.push_back(c1);
        points.push_back(c2);
        points.push_back(to);
        tags.push_back(tag_cubic);
        tags.push_back(tag_cubic);
        tags.push_back(tag_on);
        movable = false;
    }

    // An arc of a circle with the given centre and radius, from `angle_start` through `angle_diff`
    // (which may be negative), as one cubic per quarter turn at most.
    void arc_to(fvec center, fixed radius, fixed angle_start, fixed angle_total) {
        int arcs = 1;
        while (angle_total > arc_cubic_angle * arcs || -angle_total > arc_cubic_angle * arcs) ++arcs;

        fixed coef = tan_fix(angle_total / (4 * arcs));
        coef += coef / 3;

        fvec a0 = from_polar(radius, angle_start);
        fvec a1{mul_fix(-a0.y, coef), mul_fix(a0.x, coef)};
        a0 = a0 + center;
        a1 = a1 + a0;

        for (int i = 1; i <= arcs; ++i) {
            fvec a3 = from_polar(radius, angle_start + i * angle_total / arcs);
            fvec a2{mul_fix(a3.y, coef), mul_fix(-a3.x, coef)};
            a3 = a3 + center;
            a2 = a2 + a3;
            cubic_to(a1, a2, a3);
            a1 = {a3.x - a2.x + a3.x, a3.y - a2.y + a3.y};
        }
    }

    // Starts a new contour (ending the previous one).
    void move_to(fvec to) {
        if (start >= 0) close(false);
        start = int(points.size());
        movable = false;
        line_to(to, false);
    }

    // Ends the contour: its last point replaces the first (they are the same place), the contour is
    // optionally reversed, and the first and last points are marked.
    void close(bool reverse) {
        const std::size_t first = std::size_t(start);
        std::size_t count = points.size();

        if (count <= first + 1) {
            points.resize(first);
            tags.resize(first);
        } else {
            --count;
            points[first] = points[count];
            tags[first] = tags[count];
            points.resize(count);
            tags.resize(count);

            if (reverse) {
                std::reverse(points.begin() + std::ptrdiff_t(first) + 1, points.end());
                std::reverse(tags.begin() + std::ptrdiff_t(first) + 1, tags.end());
            }
            tags[first] |= tag_begin;
            tags[count - 1] |= tag_end;
        }
        start = -1;
        movable = false;
    }

    // Is every contour properly begun and ended, and how many points and contours does it hold?
    bool counts(std::size_t& n_points, std::size_t& n_contours) const {
        n_points = points.size();
        n_contours = 0;
        bool in_contour = false;
        for (std::uint8_t t : tags) {
            if (t & tag_begin) {
                if (in_contour) return false;
                in_contour = true;
            } else if (!in_contour) {
                return false;
            }
            if (t & tag_end) {
                in_contour = false;
                ++n_contours;
            }
        }
        return !in_contour;
    }
};

// --- the stroker ----------------------------------------------------------------------------------------

constexpr fixed side_to_rotate(int side) noexcept { return angle_pi2 - side * angle_pi; }

class stroker {
public:
    stroker(fixed radius, line_cap cap, line_join join, fixed miter_limit)
        : radius_(radius), cap_(cap), join_(join), join_saved_(join), miter_limit_(std::max<fixed>(miter_limit, 0x10000)) {}

    fixed_outline run(const fixed_outline& in);

private:
    void begin_subpath(fvec to, bool open);
    void end_subpath();
    void line_to(fvec to);
    void cubic_to(fvec c1, fvec c2, fvec to);

    void arc_to(int side);
    void cap(fixed angle, int side);
    void inside(int side, fixed line_length);
    void outside(int side, fixed line_length);
    void process_corner(fixed line_length);
    void subpath_start(fixed start_angle, fixed line_length);
    void add_reverse_left(bool open);
    bool parse(const fixed_outline& in);

    // The settings.
    const fixed radius_;
    const line_cap cap_;
    line_join join_;
    const line_join join_saved_;
    const fixed miter_limit_;

    // Where the stroker is in the contour.
    fixed angle_in_ = 0;
    fixed angle_out_ = 0;
    fvec center_;
    fixed line_length_ = 0;
    bool first_point_ = false;
    bool subpath_open_ = false;
    fixed subpath_angle_ = 0;
    fvec subpath_start_;
    fixed subpath_line_length_ = 0;
    bool handle_wide_strokes_ = false;

    border borders_[2];
};

void stroker::arc_to(int side) {
    border& b = borders_[side];
    const fixed rotate = side_to_rotate(side);
    fixed total = angle_diff(angle_in_, angle_out_);
    if (total == angle_pi) total = -rotate * 2;
    b.arc_to(center_, radius_, angle_in_ + rotate, total);
    b.movable = false;
}

// The cap at the end of an open contour, on `side`, for a line leaving in direction `angle`.
void stroker::cap(fixed angle, int side) {
    if (cap_ == line_cap::round) {
        angle_in_ = angle;
        angle_out_ = angle + angle_pi;
        arc_to(side);
        return;
    }

    border& b = borders_[side];
    fvec middle = from_polar(radius_, angle);
    fvec delta{side ? middle.y : -middle.y, side ? -middle.x : middle.x};

    if (cap_ == line_cap::square) {
        middle = middle + center_;
    } else {  // butt
        middle = center_;
    }

    delta = delta + middle;
    b.line_to(delta, false);

    delta = {middle.x - delta.x + middle.x, middle.y - delta.y + middle.y};
    b.line_to(delta, false);
}

// The corner as seen from its inside: the two offset lines cross, so the border just goes to the
// crossing point (or, when the segments are too short for that, around the corner point).
void stroker::inside(int side, fixed line_length) {
    border& b = borders_[side];
    const fixed rotate = side_to_rotate(side);
    const fixed theta = angle_diff(angle_in_, angle_out_) / 2;

    bool intersect;
    fvec sigma;
    if (!b.movable || line_length == 0 || theta > 0x59C000 || theta < -0x59C000) {
        intersect = false;
    } else {
        sigma = unit_vector(theta);  // (cos, sin) of half the turn
        const fixed min_length = fx_abs(mul_div(radius_, sigma.y, sigma.x));
        intersect = min_length && line_length_ >= min_length && line_length >= min_length;
    }

    fvec delta;
    if (!intersect) {
        delta = from_polar(radius_, angle_out_ + rotate) + center_;
        b.movable = false;
    } else {
        const fixed phi = angle_in_ + theta + rotate;
        const fixed length = div_fix(radius_, sigma.x);
        delta = from_polar(length, phi) + center_;
    }
    b.line_to(delta, false);
}

// The corner as seen from its outside: the join.
void stroker::outside(int side, fixed line_length) {
    border& b = borders_[side];

    if (join_ == line_join::round) {
        arc_to(side);
        return;
    }

    const fixed rotate = side_to_rotate(side);
    bool bevel = join_ == line_join::bevel;
    fixed phi = 0;
    fvec sigma;

    if (!bevel) {
        fixed theta = angle_diff(angle_in_, angle_out_) / 2;
        if (theta == angle_pi2) theta = -rotate;
        phi = angle_in_ + theta + rotate;
        sigma = from_polar(miter_limit_, theta);
        if (sigma.x < 0x10000) bevel = true;  // the miter would be longer than the limit
    }

    if (bevel) {
        const fvec delta = from_polar(radius_, angle_out_ + rotate) + center_;
        b.movable = false;
        b.line_to(delta, false);
    } else {
        const fixed length = mul_div(radius_, miter_limit_, sigma.x);
        fvec delta = from_polar(length, phi) + center_;
        b.line_to(delta, false);
        if (line_length == 0) {
            delta = from_polar(radius_, angle_out_ + rotate) + center_;
            b.line_to(delta, false);
        }
    }
}

void stroker::process_corner(fixed line_length) {
    const fixed turn = angle_diff(angle_in_, angle_out_);
    if (turn == 0) return;  // straight on

    const int inside_side = turn < 0 ? 1 : 0;
    inside(inside_side, line_length);
    outside(1 - inside_side, line_length);
}

void stroker::subpath_start(fixed start_angle, fixed line_length) {
    const fvec delta = from_polar(radius_, start_angle + angle_pi2);
    borders_[0].move_to(center_ + delta);
    borders_[1].move_to(center_ - delta);

    subpath_angle_ = start_angle;
    first_point_ = false;
    subpath_line_length_ = line_length;
}

void stroker::begin_subpath(fvec to, bool open) {
    first_point_ = true;
    center_ = to;
    subpath_open_ = open;
    // A join other than round, or a butt cap, can leave a stroke wider than the line is long on a
    // curve: those cases need the extra check in cubic_to.
    handle_wide_strokes_ = join_ != line_join::round || (subpath_open_ && cap_ == line_cap::butt);
    subpath_start_ = to;
    angle_in_ = 0;
}

void stroker::line_to(fvec to) {
    fvec delta = to - center_;
    if (delta.x == 0 && delta.y == 0) return;

    const fixed line_length = length(delta);
    const fixed angle = angle_of(delta);
    delta = from_polar(radius_, angle + angle_pi2);

    if (first_point_) {
        subpath_start(angle, line_length);
    } else {
        angle_out_ = angle;
        process_corner(line_length);
    }

    // The end of the segment on both sides (side 1 is borders_[0], the one on the other side of delta).
    for (int i = 0, side = 1; side >= 0; --side, ++i) {
        borders_[i].line_to(to + delta, true);
        delta = {-delta.x, -delta.y};
    }

    angle_in_ = angle;
    center_ = to;
    line_length_ = line_length;
}

void stroker::cubic_to(fvec control1, fvec control2, fvec to) {
    // A curve that never leaves its start: nothing to draw.
    if (is_small(center_ - control1) && is_small(control1 - control2) && is_small(control2 - to)) {
        center_ = to;
        return;
    }

    fvec bez_stack[37];
    fvec* arc = bez_stack;
    fvec* const limit = bez_stack + 32;
    bool first_arc = true;

    arc[0] = to;
    arc[1] = control2;
    arc[2] = control1;
    arc[3] = center_;

    while (arc >= bez_stack) {
        fixed angle_in = angle_in_, angle_mid = angle_in_, angle_out = angle_in_;

        // Split until the piece is nearly straight (or the stack is full).
        if (arc < limit && !cubic_is_small_enough(arc, angle_in, angle_mid, angle_out)) {
            if (first_point_) angle_in_ = angle_in;
            cubic_split(arc);
            arc += 3;
            continue;
        }

        if (first_arc) {
            first_arc = false;
            if (first_point_) {
                subpath_start(angle_in, 0);
            } else {
                angle_out_ = angle_in;
                process_corner(0);
            }
        } else if (fx_abs(angle_diff(angle_in_, angle_in)) > small_cubic_threshold / 4) {
            // The direction changes between two pieces of the same curve: a round join hides it.
            center_ = arc[3];
            angle_out_ = angle_in;
            join_ = line_join::round;
            process_corner(0);
            join_ = join_saved_;
        }

        {
            const fixed theta1 = angle_diff(angle_in, angle_mid) / 2;
            const fixed theta2 = angle_diff(angle_mid, angle_out) / 2;
            const fixed phi1 = angle_mean(angle_in, angle_mid);
            const fixed phi2 = angle_mean(angle_mid, angle_out);
            const fixed length1 = div_fix(radius_, cos_fix(theta1));
            const fixed length2 = div_fix(radius_, cos_fix(theta2));
            fixed alpha0 = 0;

            if (handle_wide_strokes_) alpha0 = atan2_fix(arc[0].x - arc[3].x, arc[0].y - arc[3].y);

            for (int side = 0; side <= 1; ++side) {
                border& b = borders_[side];
                const fixed rotate = side_to_rotate(side);

                const fvec ctrl1 = from_polar(length1, phi1 + rotate) + arc[2];
                const fvec ctrl2 = from_polar(length2, phi2 + rotate) + arc[1];
                const fvec end = from_polar(radius_, angle_out + rotate) + arc[0];

                if (handle_wide_strokes_) {
                    // When the stroke is wider than the curve is curved, the offset curve can turn back on
                    // itself; detect it by the offset's direction flipping and cut the turn off with a
                    // corner (this is what keeps a thick stroke of a tight curve from folding over).
                    const fvec start = b.points.back();
                    const fixed alpha1 = atan2_fix(end.x - start.x, end.y - start.y);

                    if (fx_abs(angle_diff(alpha0, alpha1)) > angle_pi / 2) {
                        const fixed beta = atan2_fix(arc[3].x - start.x, arc[3].y - start.y);
                        const fixed gamma = atan2_fix(arc[0].x - end.x, arc[0].y - end.y);
                        const fixed blen = length(end - start);
                        const fixed sin_a = fx_abs(sin_fix(alpha1 - gamma));
                        const fixed sin_b = fx_abs(sin_fix(beta - gamma));
                        const fixed alen = mul_div(blen, sin_a, sin_b);
                        const fvec delta = from_polar(alen, beta) + start;

                        b.movable = false;
                        b.line_to(delta, false);
                        b.line_to(end, false);
                        b.cubic_to(ctrl2, ctrl1, start);
                        b.line_to(end, false);
                        continue;
                    }
                }
                b.cubic_to(ctrl1, ctrl2, end);
            }
        }

        arc -= 3;
        angle_in_ = angle_out;
    }

    center_ = to;
    line_length_ = 0;
}

// Joins the left border's contour, reversed, onto the right border (they make one closed contour for
// an open line: down one side, round the cap, back up the other side).
void stroker::add_reverse_left(bool open) {
    border& right = borders_[0];
    border& left = borders_[1];

    const int new_points = int(left.points.size()) - left.start;
    if (new_points <= 0) return;

    for (std::size_t i = left.points.size(); i > std::size_t(left.start); --i) {
        right.points.push_back(left.points[i - 1]);
        std::uint8_t t = left.tags[i - 1];
        if (open) {
            t &= std::uint8_t(~tag_begin_end);
        } else {
            const std::uint8_t both = t & tag_begin_end;
            if (both == tag_begin || both == tag_end) t ^= tag_begin_end;
        }
        right.tags.push_back(t);
    }
    left.points.resize(std::size_t(left.start));
    left.tags.resize(std::size_t(left.start));
    right.movable = false;
    left.movable = false;
}

void stroker::end_subpath() {
    if (subpath_open_) {
        cap(angle_in_, 0);           // the cap at the end
        add_reverse_left(true);      // back along the other side
        center_ = subpath_start_;
        cap(subpath_angle_ + angle_pi, 0);  // the cap at the start
        borders_[0].close(false);
        return;
    }

    // A closed contour: finish the way back to the start and join the last segment to the first.
    if (center_.x != subpath_start_.x || center_.y != subpath_start_.y) line_to(subpath_start_);

    angle_out_ = subpath_angle_;
    const fixed turn = angle_diff(angle_in_, angle_out_);
    if (turn != 0) {
        const int inside_side = turn < 0 ? 1 : 0;
        inside(inside_side, subpath_line_length_);
        outside(1 - inside_side, subpath_line_length_);
    }
    borders_[0].close(false);
    borders_[1].close(true);
}

bool stroker::parse(const fixed_outline& in) {
    borders_[0].reset();
    borders_[1].reset();

    std::size_t first = 0;
    for (std::size_t n = 0; n < in.contours.size(); ++n) {
        const std::size_t last = std::size_t(in.contours[n]);
        if (last <= first) {  // a contour of a single point has no line
            first = last + 1;
            continue;
        }

        const fvec v_start = in.points[first];
        if (in.tags[first] != point_tag::on) return false;

        begin_subpath(v_start, in.open[n] != 0);

        bool closed_by_curve = false;
        std::size_t i = first;
        while (i < last) {
            ++i;
            if (in.tags[i] == point_tag::on) {
                line_to(in.points[i]);
                continue;
            }
            if (i + 1 > last || in.tags[i + 1] != point_tag::cubic) return false;
            i += 2;
            const fvec c1 = in.points[i - 2];
            const fvec c2 = in.points[i - 1];
            if (i <= last) {
                cubic_to(c1, c2, in.points[i]);
                continue;
            }
            cubic_to(c1, c2, v_start);
            closed_by_curve = true;
            break;
        }
        (void)closed_by_curve;

        if (first_point_) {
            // The contour never moved (all its points were the same): a dot, drawn with the caps.
            subpath_open_ = true;
            subpath_start(0, 0);
        }
        end_subpath();
        first = last + 1;
    }
    return true;
}

fixed_outline stroker::run(const fixed_outline& in) {
    fixed_outline result;
    if (!parse(in)) return result;

    std::size_t points0 = 0, contours0 = 0, points1 = 0, contours1 = 0;
    if (!borders_[0].counts(points0, contours0) || !borders_[1].counts(points1, contours1)) return result;

    result.points.reserve(points0 + points1);
    result.tags.reserve(points0 + points1);
    result.contours.reserve(contours0 + contours1);

    for (const border& b : borders_) {
        const int base = int(result.points.size());
        for (std::size_t i = 0; i < b.points.size(); ++i) {
            result.points.push_back(b.points[i]);
            result.tags.push_back((b.tags[i] & tag_on) ? point_tag::on : point_tag::cubic);
            if (b.tags[i] & tag_end) result.contours.push_back(base + int(i));
        }
    }
    result.open.assign(result.contours.size(), 0);
    return result;
}

}  // namespace

fixed_outline stroke_outline(const fixed_outline& in, fixed radius, line_cap cap, line_join join, fixed miter_limit) {
    stroker s(radius, cap, join, miter_limit);
    return s.run(in);
}

}  // namespace stb::detail
