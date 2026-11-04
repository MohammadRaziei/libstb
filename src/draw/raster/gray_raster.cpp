#include "draw/raster/gray_raster.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "stb/export.hpp"

namespace stb::detail {

namespace {

using coord = std::int64_t;  // positions, areas, counts: always 64 bits, so no overflow in the products

constexpr int pixel_bits = 8;  // the rasterizer works in 24.8 fixed point: 256 units per pixel
constexpr coord one_pixel = coord(1) << pixel_bits;

constexpr coord to_pixel(coord x) noexcept { return x >> pixel_bits; }
constexpr coord subpixel(coord x) noexcept { return x & (one_pixel - 1); }
// 26.6 -> 24.8
constexpr coord upscale(coord x) noexcept { return x * (one_pixel >> 6); }

constexpr coord max_span_coord = (coord(1) << 23) - 1;
constexpr std::size_t span_batch = 256;  // spans collected before they are handed to the sink

// Floor division and the matching (non-negative) remainder.
void div_mod(coord dividend, coord divisor, coord& quotient, coord& remainder) noexcept {
    quotient = dividend / divisor;
    remainder = dividend % divisor;
    if (remainder < 0) {
        --quotient;
        remainder += divisor;
    }
}

// One pixel's accumulated edge contributions. Cells of a row form a list sorted by x.
struct cell {
    int x;
    int cover;
    coord area;
    int next;  // index of the next cell in the row, -1 at the end
};

struct box {
    coord x0, y0, x1, y1;
};

class worker {
public:
    worker(const fixed_outline& o, bool even_odd, span_sink sink) : o_(o), even_odd_(even_odd), sink_(sink) {
        batch_.reserve(span_batch);
    }

    void run(const clip_rect& clip);

private:
    // --- cells ---------------------------------------------------------------------------------
    int find_cell();
    void record_cell();
    void set_cell(coord ex, coord ey);
    void start_cell(coord ex, coord ey);

    // --- edges ---------------------------------------------------------------------------------
    void render_scanline(coord ey, coord x1, coord y1, coord x2, coord y2);
    void render_line(coord from_x, coord from_y, coord to_x, coord to_y);
    int clip_flags(coord x, coord y) const noexcept;
    int clip_vflags(coord y) const noexcept;
    void vline(coord x1, coord y1, coord x2, coord y2, int f1, int f2);
    void line_to(coord x2, coord y2);
    void render_cubic(fvec control1, fvec control2, fvec to);
    void move_to(fvec to);
    bool decompose();

    // --- output --------------------------------------------------------------------------------
    void hline(coord x, coord y, coord area, int count);
    void sweep();
    void flush();

    const fixed_outline& o_;
    const bool even_odd_;
    span_sink sink_;

    // The current cell and the bounds, in whole pixels (the "ex" and "ey" of a pixel column and row).
    coord ex_ = 0, ey_ = 0;
    coord min_ex_ = 0, max_ex_ = 0, min_ey_ = 0, max_ey_ = 0;
    coord count_ex_ = 0, count_ey_ = 0;
    coord area_ = 0;
    coord cover_ = 0;
    bool invalid_ = true;

    std::vector<cell> cells_;
    std::vector<int> rows_;  // per row: index of the first cell, -1 for none

    coord x_ = 0, y_ = 0;  // the current point, 24.8

    box clip_{};  // in 24.8, one pixel wider than the clip on every side
    int clip_flags_ = 0;
    bool clipping_ = false;

    std::vector<span> batch_;
};

// --- cells ------------------------------------------------------------------------------------------

int worker::find_cell() {
    coord x = ex_;
    if (x > count_ex_) x = count_ex_;
    const int xi = int(x);
    int prev = -1;
    int cur = rows_[std::size_t(ey_)];
    while (cur != -1 && cells_[std::size_t(cur)].x <= xi) {
        if (cells_[std::size_t(cur)].x == xi) return cur;
        prev = cur;
        cur = cells_[std::size_t(cur)].next;
    }
    const int index = int(cells_.size());
    cells_.push_back(cell{xi, 0, 0, cur});
    if (prev < 0)
        rows_[std::size_t(ey_)] = index;
    else
        cells_[std::size_t(prev)].next = index;
    return index;
}

void worker::record_cell() {
    if (area_ | cover_) {
        cell& c = cells_[std::size_t(find_cell())];
        c.area += area_;
        c.cover += int(cover_);
    }
}

void worker::set_cell(coord ex, coord ey) {
    ey -= min_ey_;
    if (ex > max_ex_) ex = max_ex_;
    ex -= min_ex_;
    if (ex < 0) ex = -1;

    if (ex != ex_ || ey != ey_) {
        if (!invalid_) record_cell();
        area_ = 0;
        cover_ = 0;
        ex_ = ex;
        ey_ = ey;
    }
    invalid_ = std::uint64_t(ey) >= std::uint64_t(count_ey_) || ex >= count_ex_;
}

void worker::start_cell(coord ex, coord ey) {
    if (ex > max_ex_) ex = max_ex_;
    if (ex < min_ex_) ex = min_ex_ - 1;
    area_ = 0;
    cover_ = 0;
    ex_ = ex - min_ex_;
    ey_ = ey - min_ey_;
    invalid_ = false;
    set_cell(ex, ey);
}

// --- edges -------------------------------------------------------------------------------------------

// A line segment inside one pixel row: (x1, y1) to (x2, y2), y1 and y2 being the fractional heights.
void worker::render_scanline(coord ey, coord x1, coord y1, coord x2, coord y2) {
    coord ex1 = to_pixel(x1);
    const coord ex2 = to_pixel(x2);

    if (y1 == y2) {
        set_cell(ex2, ey);
        return;
    }

    coord fx1 = subpixel(x1);
    const coord fx2 = subpixel(x2);

    if (ex1 != ex2) {
        // The segment crosses pixel columns: split it at each boundary.
        coord dx = x2 - x1;
        coord dy = y2 - y1;
        coord p, first;
        int incr;
        if (dx > 0) {
            p = (one_pixel - fx1) * dy;
            first = one_pixel;
            incr = 1;
        } else {
            p = fx1 * dy;
            first = 0;
            incr = -1;
            dx = -dx;
        }

        coord delta, mod;
        div_mod(p, dx, delta, mod);
        area_ += (fx1 + first) * delta;
        cover_ += delta;
        y1 += delta;
        ex1 += incr;
        set_cell(ex1, ey);

        if (ex1 != ex2) {
            coord lift, rem;
            p = one_pixel * dy;
            div_mod(p, dx, lift, rem);
            do {
                delta = lift;
                mod += rem;
                if (mod >= dx) {
                    mod -= dx;
                    ++delta;
                }
                area_ += one_pixel * delta;
                cover_ += delta;
                y1 += delta;
                ex1 += incr;
                set_cell(ex1, ey);
            } while (ex1 != ex2);
        }
        fx1 = one_pixel - first;
    }

    const coord dy = y2 - y1;
    area_ += (fx1 + fx2) * dy;
    cover_ += dy;
}

void worker::render_line(coord from_x, coord from_y, coord to_x, coord to_y) {
    coord ey1 = to_pixel(from_y);
    const coord ey2 = to_pixel(to_y);

    // Entirely above or below the rows being computed.
    if ((ey1 >= max_ey_ && ey2 >= max_ey_) || (ey1 < min_ey_ && ey2 < min_ey_)) return;

    const coord fy1 = subpixel(from_y);
    const coord fy2 = subpixel(to_y);

    if (ey1 == ey2) {  // a single row
        render_scanline(ey1, from_x, fy1, to_x, fy2);
        return;
    }

    coord dx = to_x - from_x;
    coord dy = to_y - from_y;

    if (dx == 0) {  // a vertical line: the cheap case
        const coord ex = to_pixel(from_x);
        const coord two_fx = subpixel(from_x) << 1;
        const coord first = dy > 0 ? one_pixel : 0;

        coord delta = first - fy1;
        area_ += two_fx * delta;
        cover_ += delta;

        delta = first + first - one_pixel;
        const coord area = two_fx * delta;
        const coord max_ey1 = count_ey_ + min_ey_;

        if (dy < 0) {
            if (ey1 > max_ey1) {
                ey1 = max_ey1 > ey2 ? max_ey1 : ey2;
                set_cell(ex, ey1);
            } else {
                --ey1;
                set_cell(ex, ey1);
            }
            while (ey1 > ey2 && ey1 >= min_ey_) {
                area_ += area;
                cover_ += delta;
                --ey1;
                set_cell(ex, ey1);
            }
            if (ey1 != ey2) {
                ey1 = ey2;
                set_cell(ex, ey1);
            }
        } else {
            if (ey1 < min_ey_) {
                ey1 = min_ey_ < ey2 ? min_ey_ : ey2;
                set_cell(ex, ey1);
            } else {
                ++ey1;
                set_cell(ex, ey1);
            }
            while (ey1 < ey2 && ey1 < max_ey1) {
                area_ += area;
                cover_ += delta;
                ++ey1;
                set_cell(ex, ey1);
            }
            if (ey1 != ey2) {
                ey1 = ey2;
                set_cell(ex, ey1);
            }
        }

        delta = fy2 - one_pixel + first;
        area_ += two_fx * delta;
        cover_ += delta;
        return;
    }

    // A general line: one scanline segment per row it crosses.
    coord p, first;
    int incr;
    if (dy > 0) {
        p = (one_pixel - fy1) * dx;
        first = one_pixel;
        incr = 1;
    } else {
        p = fy1 * dx;
        first = 0;
        incr = -1;
        dy = -dy;
    }

    coord delta, mod;
    div_mod(p, dy, delta, mod);
    coord x = from_x + delta;
    render_scanline(ey1, from_x, fy1, x, first);

    ey1 += incr;
    set_cell(to_pixel(x), ey1);

    if (ey1 != ey2) {
        coord lift, rem;
        p = one_pixel * dx;
        div_mod(p, dy, lift, rem);
        do {
            delta = lift;
            mod += rem;
            if (mod >= dy) {
                mod -= dy;
                ++delta;
            }
            const coord x2 = x + delta;
            render_scanline(ey1, x, one_pixel - first, x2, first);
            x = x2;
            ey1 += incr;
            set_cell(to_pixel(x), ey1);
        } while (ey1 != ey2);
    }
    render_scanline(ey1, x, one_pixel - first, to_x, fy2);
}

// The clipping flags of a point: bit 0 right of the clip, 1 below, 2 left, 3 above.
int worker::clip_flags(coord x, coord y) const noexcept {
    return (int(x > clip_.x1) << 0) | (int(y > clip_.y1) << 1) | (int(x < clip_.x0) << 2) | (int(y < clip_.y0) << 3);
}

int worker::clip_vflags(coord y) const noexcept { return (int(y > clip_.y1) << 1) | (int(y < clip_.y0) << 3); }

// A segment that is vertical-ish after being clipped on the left or right: only its y limits need to be
// cut to the clip, then it is drawn.
void worker::vline(coord x1, coord y1, coord x2, coord y2, int f1, int f2) {
    f1 &= 10;
    f2 &= 10;
    if ((f1 | f2) == 0) {
        render_line(x1, y1, x2, y2);
    } else if (f1 == f2) {
        return;  // both ends beyond the same horizontal limit
    } else {
        coord tx1 = x1, ty1 = y1, tx2 = x2, ty2 = y2;
        const coord clip_y1 = clip_.y0;
        const coord clip_y2 = clip_.y1;
        if (f1 & 8) {
            tx1 = x1 + mul_div(clip_y1 - y1, x2 - x1, y2 - y1);
            ty1 = clip_y1;
        }
        if (f1 & 2) {
            tx1 = x1 + mul_div(clip_y2 - y1, x2 - x1, y2 - y1);
            ty1 = clip_y2;
        }
        if (f2 & 8) {
            tx2 = x1 + mul_div(clip_y1 - y1, x2 - x1, y2 - y1);
            ty2 = clip_y1;
        }
        if (f2 & 2) {
            tx2 = x1 + mul_div(clip_y2 - y1, x2 - x1, y2 - y1);
            ty2 = clip_y2;
        }
        render_line(tx1, ty1, tx2, ty2);
    }
}

void worker::line_to(coord x2, coord y2) {
    if (!clipping_) {
        render_line(x_, y_, x2, y2);
    } else {
        const int f1 = clip_flags_;
        const int f2 = clip_flags(x2, y2);

        if ((f1 & 10) == (f2 & 10) && (f1 & 10) != 0) {
            // Both ends beyond the same top or bottom limit: nothing visible.
            clip_flags_ = f2;
        } else {
            const coord x1 = x_;
            const coord y1 = y_;
            const coord clip_x1 = clip_.x0;
            const coord clip_x2 = clip_.x1;
            coord y3, y4;
            int f3, f4;

            switch (((f1 & 5) << 1) | (f2 & 5)) {
                case 0:  // inside horizontally
                    vline(x1, y1, x2, y2, f1, f2);
                    break;
                case 1:  // exits through the right
                    y3 = y1 + mul_div(clip_x2 - x1, y2 - y1, x2 - x1);
                    f3 = clip_vflags(y3);
                    vline(x1, y1, clip_x2, y3, f1, f3);
                    vline(clip_x2, y3, clip_x2, y2, f3, f2);
                    break;
                case 2:  // enters from the right
                    y3 = y1 + mul_div(clip_x2 - x1, y2 - y1, x2 - x1);
                    f3 = clip_vflags(y3);
                    vline(clip_x2, y1, clip_x2, y3, f1, f3);
                    vline(clip_x2, y3, x2, y2, f3, f2);
                    break;
                case 3:  // both right
                    vline(clip_x2, y1, clip_x2, y2, f1, f2);
                    break;
                case 4:  // exits through the left
                    y3 = y1 + mul_div(clip_x1 - x1, y2 - y1, x2 - x1);
                    f3 = clip_vflags(y3);
                    vline(x1, y1, clip_x1, y3, f1, f3);
                    vline(clip_x1, y3, clip_x1, y2, f3, f2);
                    break;
                case 6:  // from the right to the left
                    y3 = y1 + mul_div(clip_x2 - x1, y2 - y1, x2 - x1);
                    y4 = y1 + mul_div(clip_x1 - x1, y2 - y1, x2 - x1);
                    f3 = clip_vflags(y3);
                    f4 = clip_vflags(y4);
                    vline(clip_x2, y1, clip_x2, y3, f1, f3);
                    vline(clip_x2, y3, clip_x1, y4, f3, f4);
                    vline(clip_x1, y4, clip_x1, y2, f4, f2);
                    break;
                case 8:  // enters from the left
                    y3 = y1 + mul_div(clip_x1 - x1, y2 - y1, x2 - x1);
                    f3 = clip_vflags(y3);
                    vline(clip_x1, y1, clip_x1, y3, f1, f3);
                    vline(clip_x1, y3, x2, y2, f3, f2);
                    break;
                case 9:  // from the left to the right
                    y3 = y1 + mul_div(clip_x1 - x1, y2 - y1, x2 - x1);
                    y4 = y1 + mul_div(clip_x2 - x1, y2 - y1, x2 - x1);
                    f3 = clip_vflags(y3);
                    f4 = clip_vflags(y4);
                    vline(clip_x1, y1, clip_x1, y3, f1, f3);
                    vline(clip_x1, y3, clip_x2, y4, f3, f4);
                    vline(clip_x2, y4, clip_x2, y2, f4, f2);
                    break;
                case 12:  // both left
                    vline(clip_x1, y1, clip_x1, y2, f1, f2);
                    break;
            }
            clip_flags_ = f2;
        }
    }
    x_ = x2;
    y_ = y2;
}

void split_cubic(fvec* base) noexcept {
    coord a, b, c, d;

    base[6].x = base[3].x;
    c = base[1].x;
    d = base[2].x;
    base[1].x = a = (base[0].x + c) / 2;
    base[5].x = b = (base[3].x + d) / 2;
    c = (c + d) / 2;
    base[2].x = a = (a + c) / 2;
    base[4].x = b = (b + c) / 2;
    base[3].x = (a + b) / 2;

    base[6].y = base[3].y;
    c = base[1].y;
    d = base[2].y;
    base[1].y = a = (base[0].y + c) / 2;
    base[5].y = b = (base[3].y + d) / 2;
    c = (c + d) / 2;
    base[2].y = a = (a + c) / 2;
    base[4].y = b = (b + c) / 2;
    base[3].y = (a + b) / 2;
}

// A cubic curve from the current point: flattened into line segments, splitting until each piece is
// flat enough for its error to stay below a sixth of a pixel.
void worker::render_cubic(fvec control1, fvec control2, fvec to) {
    fvec bez_stack[16 * 3 + 1];
    fvec* arc = bez_stack;
    fvec* const limit = bez_stack + 45;

    arc[0] = {upscale(to.x), upscale(to.y)};
    arc[1] = {upscale(control2.x), upscale(control2.y)};
    arc[2] = {upscale(control1.x), upscale(control1.y)};
    arc[3] = {x_, y_};

    // Entirely above or below the rows being computed: skip it, but keep the current point.
    if ((to_pixel(arc[0].y) >= max_ey_ && to_pixel(arc[1].y) >= max_ey_ && to_pixel(arc[2].y) >= max_ey_ &&
         to_pixel(arc[3].y) >= max_ey_) ||
        (to_pixel(arc[0].y) < min_ey_ && to_pixel(arc[1].y) < min_ey_ && to_pixel(arc[2].y) < min_ey_ &&
         to_pixel(arc[3].y) < min_ey_)) {
        if (clipping_) clip_flags_ = clip_flags(arc[0].x, arc[0].y);
        x_ = arc[0].x;
        y_ = arc[0].y;
        return;
    }

    for (;;) {
        // The chord and how far the control points are from it.
        coord dx = arc[3].x - arc[0].x;
        coord dy = arc[3].y - arc[0].y;
        coord ax = fx_abs(dx);
        coord ay = fx_abs(dy);
        const coord len = ax > ay ? ax + (3 * ay >> 3) : ay + (3 * ax >> 3);  // an approximate hypot

        bool split = len >= (coord(1) << 23);
        if (!split) {
            const coord s_limit = len * (one_pixel / 6);
            const coord dx1 = arc[1].x - arc[0].x;
            const coord dy1 = arc[1].y - arc[0].y;
            const coord dx2 = arc[2].x - arc[0].x;
            const coord dy2 = arc[2].y - arc[0].y;
            if (fx_abs(dy * dx1 - dx * dy1) > s_limit || fx_abs(dy * dx2 - dx * dy2) > s_limit ||
                dx1 * (dx1 - dx) + dy1 * (dy1 - dy) > 0 || dx2 * (dx2 - dx) + dy2 * (dy2 - dy) > 0)
                split = true;
        }

        if (!split) {
            line_to(arc[0].x, arc[0].y);
            if (arc == bez_stack) return;
            arc -= 3;
            continue;
        }

        if (arc == limit) return;  // too deep: give up on this curve (cannot happen for sane input)
        split_cubic(arc);
        arc += 3;
    }
}

void worker::move_to(fvec to) {
    if (!invalid_) record_cell();

    const coord x = upscale(to.x);
    const coord y = upscale(to.y);
    start_cell(to_pixel(x), to_pixel(y));
    if (clipping_) clip_flags_ = clip_flags(x, y);

    x_ = x;
    y_ = y;
}

// Walks the contours of the outline and draws their edges. Returns false for a malformed outline.
bool worker::decompose() {
    std::size_t first = 0;
    for (std::size_t n = 0; n < o_.contours.size(); ++n) {
        const int last_i = o_.contours[n];
        if (last_i < 0) return false;
        const std::size_t last = std::size_t(last_i);
        if (last >= o_.points.size()) return false;

        const fvec v_start = o_.points[first];
        if (o_.tags[first] != point_tag::on) return false;  // a contour starts on the curve

        move_to(v_start);

        bool closed_by_curve = false;
        std::size_t i = first;
        while (i < last) {
            ++i;
            if (o_.tags[i] == point_tag::on) {
                line_to(upscale(o_.points[i].x), upscale(o_.points[i].y));
                continue;
            }
            // A cubic: two control points then an end point.
            if (i + 1 > last || o_.tags[i + 1] != point_tag::cubic) return false;
            i += 2;
            const fvec c1 = o_.points[i - 2];
            const fvec c2 = o_.points[i - 1];
            if (i <= last) {
                render_cubic(c1, c2, o_.points[i]);
                continue;
            }
            render_cubic(c1, c2, v_start);  // the contour's last curve ends at its start
            closed_by_curve = true;
            break;
        }
        if (!closed_by_curve) line_to(upscale(v_start.x), upscale(v_start.y));  // close the contour
        first = last + 1;
    }
    return true;
}

// --- output ----------------------------------------------------------------------------------------

void worker::flush() {
    if (!batch_.empty()) {
        sink_(span_view{batch_.data(), batch_.size()});
        batch_.clear();
    }
}

// `area` is twice the covered area of the pixel run in 24.8 x 24.8 units; turn it into 0..255.
void worker::hline(coord x, coord y, coord area, int count) {
    int coverage = int(area >> (pixel_bits * 2 + 1 - 8));
    if (coverage < 0) coverage = -coverage;

    if (even_odd_) {
        coverage &= 511;
        if (coverage > 256)
            coverage = 512 - coverage;
        else if (coverage == 256)
            coverage = 255;
    } else if (coverage >= 256) {
        coverage = 255;
    }

    y += min_ey_;
    x += min_ex_;
    if (x >= (coord(1) << 23)) x = max_span_coord;
    if (y >= (coord(1) << 23)) y = max_span_coord;

    if (coverage == 0) return;

    // Extend the previous span when the run continues it with the same coverage.
    if (!batch_.empty()) {
        span& s = batch_.back();
        if (s.y == y && s.x + s.len == x && s.coverage == coverage) {
            s.len += count;
            return;
        }
    }
    if (batch_.size() >= span_batch) flush();
    batch_.push_back(span{int(y), int(x), count, std::uint8_t(coverage)});
}

void worker::sweep() {
    if (cells_.empty()) return;

    for (coord yindex = 0; yindex < count_ey_; ++yindex) {
        coord cover = 0;
        coord x = 0;

        for (int c = rows_[std::size_t(yindex)]; c != -1; c = cells_[std::size_t(c)].next) {
            const cell& cl = cells_[std::size_t(c)];

            // The run of pixels between the previous cell and this one has the accumulated cover.
            if (cl.x > x && cover != 0) hline(x, yindex, cover * (one_pixel * 2), cl.x - x);

            cover += cl.cover;
            const coord area = cover * (one_pixel * 2) - cl.area;
            if (area != 0 && cl.x >= 0) hline(cl.x, yindex, area, 1);

            x = cl.x + 1;
        }

        if (count_ex_ > x && cover != 0) hline(x, yindex, cover * (one_pixel * 2), int(count_ex_ - x));
    }
}

void worker::run(const clip_rect& clip) {
    if (o_.points.empty() || o_.contours.empty() || clip.w <= 0 || clip.h <= 0) return;
    if (o_.points.size() != std::size_t(o_.contours.back()) + 1) return;

    // The outline's box in whole pixels.
    coord min_x = o_.points[0].x, max_x = min_x, min_y = o_.points[0].y, max_y = min_y;
    for (const fvec& p : o_.points) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    min_ex_ = min_x >> 6;
    min_ey_ = min_y >> 6;
    max_ex_ = (max_x + 63) >> 6;
    max_ey_ = (max_y + 63) >> 6;

    const coord cx0 = clip.x, cy0 = clip.y;
    const coord cx1 = coord(clip.x) + clip.w, cy1 = coord(clip.y) + clip.h;

    // Nothing of the outline is inside the clip.
    if (max_ex_ <= cx0 || min_ex_ >= cx1 || max_ey_ <= cy0 || min_ey_ >= cy1) return;

    clip_flags_ = 0;
    clipping_ = false;
    if (min_ex_ < cx0) min_ex_ = cx0, clipping_ = true;
    if (min_ey_ < cy0) min_ey_ = cy0, clipping_ = true;
    if (max_ex_ > cx1) max_ex_ = cx1, clipping_ = true;
    if (max_ey_ > cy1) max_ey_ = cy1, clipping_ = true;

    clip_.x0 = (min_ex_ - 1) * one_pixel;
    clip_.y0 = (min_ey_ - 1) * one_pixel;
    clip_.x1 = (max_ex_ + 1) * one_pixel;
    clip_.y1 = (max_ey_ + 1) * one_pixel;

    count_ex_ = max_ex_ - min_ex_;
    count_ey_ = max_ey_ - min_ey_;

    rows_.assign(std::size_t(count_ey_), -1);
    cells_.clear();
    cells_.reserve(std::size_t(std::min<coord>(count_ey_ * 8 + 64, 1 << 20)));
    invalid_ = true;

    if (!decompose()) return;
    if (!invalid_) record_cell();

    sweep();
    flush();
}

}  // namespace

void rasterize_outline(const fixed_outline& o, bool even_odd, const clip_rect& clip, span_sink sink) {
    worker w(o, even_odd, sink);
    w.run(clip);
}

}  // namespace stb::detail
