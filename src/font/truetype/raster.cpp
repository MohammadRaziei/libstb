// Outline -> coverage bitmap. Curves are flattened into line segments, and every pixel gets the
// exact area the outline covers inside it (anti-aliased by area, scanline by scanline), so a glyph
// renders the same everywhere. Written from the algorithm of stb_truetype.h v1.26 (public domain /
// MIT, Sean Barrett and contributors); the float arithmetic is kept in the same order, so the
// bitmaps are identical.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "font/truetype/truetype.hpp"

namespace stb::truetype {
namespace {

struct point {
    float x, y;
};

// A non-horizontal line segment of a contour, oriented with y0 < y1.
struct edge {
    float x0, y0, x1, y1;
    bool invert;
};

// An edge that crosses the current scanline.
struct active_edge {
    active_edge* next = nullptr;
    float fx = 0;         // x where it crosses the current scanline's top
    float fdx = 0;        // dx / dy
    float fdy = 0;        // dy / dx (0 for a vertical edge)
    float direction = 0;  // +1 or -1: which way the winding changes
    float sy = 0, ey = 0;
};

// ---- flattening

constexpr int max_subdivision = 16;  // 65536 segments on one curve is enough

void flatten_quadratic(std::vector<point>& out, float x0, float y0, float x1, float y1, float x2, float y2,
                       float flatness_squared, int n) {
    const float mx = (x0 + 2 * x1 + x2) / 4;
    const float my = (y0 + 2 * y1 + y2) / 4;
    const float dx = (x0 + x2) / 2 - mx;
    const float dy = (y0 + y2) / 2 - my;
    if (n > max_subdivision) return;
    if (dx * dx + dy * dy > flatness_squared) {
        flatten_quadratic(out, x0, y0, (x0 + x1) / 2.0f, (y0 + y1) / 2.0f, mx, my, flatness_squared, n + 1);
        flatten_quadratic(out, mx, my, (x1 + x2) / 2.0f, (y1 + y2) / 2.0f, x2, y2, flatness_squared, n + 1);
    } else {
        out.push_back({x2, y2});
    }
}

void flatten_cubic(std::vector<point>& out, float x0, float y0, float x1, float y1, float x2, float y2, float x3,
                   float y3, float flatness_squared, int n) {
    const float dx0 = x1 - x0, dy0 = y1 - y0;
    const float dx1 = x2 - x1, dy1 = y2 - y1;
    const float dx2 = x3 - x2, dy2 = y3 - y2;
    const float dx = x3 - x0, dy = y3 - y0;
    const float long_len = static_cast<float>(std::sqrt(dx0 * dx0 + dy0 * dy0) + std::sqrt(dx1 * dx1 + dy1 * dy1) +
                                              std::sqrt(dx2 * dx2 + dy2 * dy2));
    const float short_len = static_cast<float>(std::sqrt(dx * dx + dy * dy));
    const float flatness = long_len * long_len - short_len * short_len;
    if (n > max_subdivision) return;
    if (flatness > flatness_squared) {
        const float x01 = (x0 + x1) / 2, y01 = (y0 + y1) / 2;
        const float x12 = (x1 + x2) / 2, y12 = (y1 + y2) / 2;
        const float x23 = (x2 + x3) / 2, y23 = (y2 + y3) / 2;
        const float xa = (x01 + x12) / 2, ya = (y01 + y12) / 2;
        const float xb = (x12 + x23) / 2, yb = (y12 + y23) / 2;
        const float mx = (xa + xb) / 2, my = (ya + yb) / 2;
        flatten_cubic(out, x0, y0, x01, y01, xa, ya, mx, my, flatness_squared, n + 1);
        flatten_cubic(out, mx, my, xb, yb, x23, y23, x3, y3, flatness_squared, n + 1);
    } else {
        out.push_back({x3, y3});
    }
}

// The outline as closed polygons: the points of all contours, and how many belong to each.
struct polygons {
    std::vector<point> points;
    std::vector<int> contour_lengths;
};

polygons flatten(const std::vector<vertex>& vertices, float flatness) {
    polygons p;
    const float flatness_squared = flatness * flatness;
    float x = 0, y = 0;
    std::size_t start = 0;
    for (const vertex& v : vertices) {
        switch (v.type) {
            case vertex::move:
                if (!p.points.empty() || !p.contour_lengths.empty()) p.contour_lengths.push_back(int(p.points.size() - start));
                start = p.points.size();
                x = v.x, y = v.y;
                p.points.push_back({x, y});
                break;
            case vertex::line:
                x = v.x, y = v.y;
                p.points.push_back({x, y});
                break;
            case vertex::curve:
                flatten_quadratic(p.points, x, y, v.cx, v.cy, v.x, v.y, flatness_squared, 0);
                x = v.x, y = v.y;
                break;
            case vertex::cubic:
                flatten_cubic(p.points, x, y, v.cx, v.cy, v.cx1, v.cy1, v.x, v.y, flatness_squared, 0);
                x = v.x, y = v.y;
                break;
        }
    }
    if (!p.points.empty()) p.contour_lengths.push_back(int(p.points.size() - start));
    return p;
}

// ---- edge sorting: the quicksort and insertion sort of stb_truetype, step for step, so edges with
// equal y0 end up in the same order (which decides the float summation order below)

bool edge_above(const edge& a, const edge& b) { return a.y0 < b.y0; }

void insertion_sort(edge* p, int n) {
    for (int i = 1; i < n; ++i) {
        const edge t = p[i];
        int j = i;
        while (j > 0) {
            if (!edge_above(t, p[j - 1])) break;
            p[j] = p[j - 1];
            --j;
        }
        if (i != j) p[j] = t;
    }
}

void quicksort(edge* p, int n) {
    while (n > 12) {  // small ranges are left for the insertion sort
        const int m = n >> 1;  // median of three
        const bool c01 = edge_above(p[0], p[m]);
        const bool c12 = edge_above(p[m], p[n - 1]);
        if (c01 != c12) {
            const bool c = edge_above(p[0], p[n - 1]);
            std::swap(p[(c == c12) ? 0 : n - 1], p[m]);
        }
        std::swap(p[0], p[m]);  // the pivot goes first so it does not move around
        int i = 1, j = n - 1;
        for (;;) {
            for (;; ++i)
                if (!edge_above(p[i], p[0])) break;
            for (;; --j)
                if (!edge_above(p[0], p[j])) break;
            if (i >= j) break;
            std::swap(p[i], p[j]);
            ++i;
            --j;
        }
        if (j < n - i) {  // recurse on the smaller side, loop on the larger
            quicksort(p, j);
            p += i;
            n -= i;
        } else {
            quicksort(p + i, n - i);
            n = j;
        }
    }
}

void sort_edges(edge* p, int n) {
    quicksort(p, n);
    insertion_sort(p, n);
}

// ---- scanline coverage

// What a clipped part of an edge adds to one pixel of the scanline: the signed height covered,
// weighted by how far right of the pixel's left side the edge is.
void add_clipped_edge(float* scanline, int x, const active_edge& e, float x0, float y0, float x1, float y1) {
    if (y0 == y1) return;
    if (y0 > e.ey) return;
    if (y1 < e.sy) return;
    if (y0 < e.sy) {
        x0 += (x1 - x0) * (e.sy - y0) / (y1 - y0);
        y0 = e.sy;
    }
    if (y1 > e.ey) {
        x1 += (x1 - x0) * (e.ey - y1) / (y1 - y0);
        y1 = e.ey;
    }
    if (x0 <= x && x1 <= x) {
        scanline[x] += e.direction * (y1 - y0);
    } else if (x0 >= x + 1 && x1 >= x + 1) {
        // right of the pixel: nothing
    } else {
        scanline[x] += e.direction * (y1 - y0) * (1 - ((x0 - x) + (x1 - x)) / 2);  // coverage = 1 - average x position
    }
}

float trapezoid_area(float height, float top_width, float bottom_width) { return (top_width + bottom_width) / 2.0f * height; }
float trapezoid_area(float height, float tx0, float tx1, float bx0, float bx1) {
    return trapezoid_area(height, tx1 - tx0, bx1 - bx0);
}
float triangle_area(float height, float width) { return height * width / 2; }

// Adds every active edge's coverage for the scanline starting at y_top. `scanline` gets the
// partial coverage of the pixels an edge passes through; `fill` the winding change that applies to
// everything to the right of them.
void fill_active_edges(float* scanline, float* fill, int len, const active_edge* e, float y_top) {
    const float y_bottom = y_top + 1;
    for (; e; e = e->next) {
        if (e->fdx == 0) {  // a vertical edge
            const float x0 = e->fx;
            if (x0 < len) {
                if (x0 >= 0) {
                    add_clipped_edge(scanline, int(x0), *e, x0, y_top, x0, y_bottom);
                    add_clipped_edge(fill - 1, int(x0) + 1, *e, x0, y_top, x0, y_bottom);
                } else {
                    add_clipped_edge(fill - 1, 0, *e, x0, y_top, x0, y_bottom);
                }
            }
            continue;
        }

        float x0 = e->fx;
        float dx = e->fdx;
        float xb = x0 + dx;
        float dy = e->fdy;
        float x_top, x_bottom, sy0, sy1;
        // the part of the edge inside this scanline
        if (e->sy > y_top) {
            x_top = x0 + dx * (e->sy - y_top);
            sy0 = e->sy;
        } else {
            x_top = x0;
            sy0 = y_top;
        }
        if (e->ey < y_bottom) {
            x_bottom = x0 + dx * (e->ey - y_top);
            sy1 = e->ey;
        } else {
            x_bottom = xb;
            sy1 = y_bottom;
        }

        if (x_top >= 0 && x_bottom >= 0 && x_top < len && x_bottom < len) {
            if (int(x_top) == int(x_bottom)) {  // within one pixel: a trapezoid
                const int x = int(x_top);
                const float height = (sy1 - sy0) * e->direction;
                scanline[x] += trapezoid_area(height, x_top, x + 1.0f, x_bottom, x + 1.0f);
                fill[x] += height;  // everything to the right is filled
            } else {                // crosses several pixels: a triangle, trapezoids, and a final piece
                if (x_top > x_bottom) {  // flip so it goes down and to the right
                    sy0 = y_bottom - (sy0 - y_top);
                    sy1 = y_bottom - (sy1 - y_top);
                    std::swap(sy0, sy1);
                    std::swap(x_bottom, x_top);
                    dx = -dx;
                    dy = -dy;
                    std::swap(x0, xb);
                }
                const int x1 = int(x_top), x2 = int(x_bottom);
                float y_crossing = y_top + dy * (x1 + 1 - x0);  // where it leaves the first pixel
                float y_final = y_top + dy * (x2 - x0);         // where it enters the last
                if (y_crossing > y_bottom) y_crossing = y_bottom;
                const float sign = e->direction;
                float area = sign * (y_crossing - sy0);
                scanline[x1] += triangle_area(area, x1 + 1 - x_top);
                if (y_final > y_bottom) {
                    y_final = y_bottom;
                    dy = (y_final - y_crossing) / (x2 - (x1 + 1));  // if the divisor is 0, y_final == y_crossing
                }
                const float step = sign * dy * 1;  // dy here is dy/dx: the change in y for one pixel of x
                for (int x = x1 + 1; x < x2; ++x) {
                    scanline[x] += area + step / 2;  // the area of a trapezoid one pixel wide
                    area += step;
                }
                scanline[x2] += area + sign * trapezoid_area(sy1 - y_final, float(x2), x2 + 1.0f, x_bottom, x2 + 1.0f);
                fill[x2] += sign * (sy1 - sy0);
            }
        } else {
            // partly off the bitmap: clip the edge against every pixel column (slow, but rare)
            for (int x = 0; x < len; ++x) {
                const float y0 = y_top;
                const float x1 = float(x), x2 = float(x + 1), x3 = xb, y3 = y_bottom;
                const float y1 = (x - x0) / dx + y_top;
                const float y2 = (x + 1 - x0) / dx + y_top;
                if (x0 < x1 && x3 > x2) {  // three segments, descending down-right
                    add_clipped_edge(scanline, x, *e, x0, y0, x1, y1);
                    add_clipped_edge(scanline, x, *e, x1, y1, x2, y2);
                    add_clipped_edge(scanline, x, *e, x2, y2, x3, y3);
                } else if (x3 < x1 && x0 > x2) {  // three segments, descending down-left
                    add_clipped_edge(scanline, x, *e, x0, y0, x2, y2);
                    add_clipped_edge(scanline, x, *e, x2, y2, x1, y1);
                    add_clipped_edge(scanline, x, *e, x1, y1, x3, y3);
                } else if (x0 < x1 && x3 > x1) {  // two segments across x, down-right
                    add_clipped_edge(scanline, x, *e, x0, y0, x1, y1);
                    add_clipped_edge(scanline, x, *e, x1, y1, x3, y3);
                } else if (x3 < x1 && x0 > x1) {  // two segments across x, down-left
                    add_clipped_edge(scanline, x, *e, x0, y0, x1, y1);
                    add_clipped_edge(scanline, x, *e, x1, y1, x3, y3);
                } else if (x0 < x2 && x3 > x2) {  // two segments across x + 1, down-right
                    add_clipped_edge(scanline, x, *e, x0, y0, x2, y2);
                    add_clipped_edge(scanline, x, *e, x2, y2, x3, y3);
                } else if (x3 < x2 && x0 > x2) {  // two segments across x + 1, down-left
                    add_clipped_edge(scanline, x, *e, x0, y0, x2, y2);
                    add_clipped_edge(scanline, x, *e, x2, y2, x3, y3);
                } else {  // one segment
                    add_clipped_edge(scanline, x, *e, x0, y0, x3, y3);
                }
            }
        }
    }
}

// Sweeps the sorted edges down the bitmap, one scanline per row.
class scanline_rasterizer {
public:
    scanline_rasterizer(std::uint8_t* pixels, int w, int h, int stride, std::vector<edge> edges)
        : pixels_(pixels), w_(w), h_(h), stride_(stride), edges_(std::move(edges)) {}

    void run(int off_x, int off_y) {
        // each edge becomes active once, so the pool never reallocates and the pointers stay valid
        pool_.reserve(edges_.size());
        std::vector<float> storage(std::size_t(w_) * 2 + 1);
        float* scanline = storage.data();
        float* fill = scanline + w_;
        const edge* e = edges_.data();
        edges_.back().y0 = float(off_y + h_) + 1;  // a sentinel after the last edge
        active_edge* active = nullptr;
        int y = off_y;
        for (int j = 0; j < h_; ++j, ++y) {
            const float y_top = y + 0.0f, y_bottom = y + 1.0f;
            std::memset(scanline, 0, std::size_t(w_) * sizeof(float));
            std::memset(fill, 0, std::size_t(w_ + 1) * sizeof(float));

            // drop the edges that ended above this scanline
            for (active_edge** step = &active; *step;) {
                if ((*step)->ey <= y_top) *step = (*step)->next;
                else step = &(*step)->next;
            }
            // start the edges that begin within it
            for (; e->y0 <= y_bottom; ++e) {
                if (e->y0 == e->y1) continue;
                active_edge* z = start(*e, off_x, y_top);
                if (j == 0 && off_y != 0 && z->ey < y_top) z->ey = y_top;  // a sliver of an edge can lie just outside
                z->next = active;
                active = z;
            }

            if (active) fill_active_edges(scanline, fill + 1, w_, active, y_top);

            float sum = 0;
            for (int i = 0; i < w_; ++i) {
                sum += fill[i];
                float k = scanline[i] + sum;
                k = std::fabs(k) * 255 + 0.5f;
                int m = int(k);
                if (m > 255) m = 255;
                pixels_[std::size_t(j) * std::size_t(stride_) + std::size_t(i)] = static_cast<std::uint8_t>(m);
            }

            for (active_edge* z = active; z; z = z->next) z->fx += z->fdx;  // to the next scanline
        }
    }

private:
    active_edge* start(const edge& e, int off_x, float start_point) {
        pool_.emplace_back();
        active_edge* z = &pool_.back();
        const float dxdy = (e.x1 - e.x0) / (e.y1 - e.y0);
        z->fdx = dxdy;
        z->fdy = dxdy != 0.0f ? (1.0f / dxdy) : 0.0f;
        z->fx = e.x0 + dxdy * (start_point - e.y0);
        z->fx -= off_x;
        z->direction = e.invert ? 1.0f : -1.0f;
        z->sy = e.y0;
        z->ey = e.y1;
        return z;
    }

    std::uint8_t* pixels_;
    int w_, h_, stride_;
    std::vector<edge> edges_;
    std::vector<active_edge> pool_;
};

}  // namespace

void rasterize(std::uint8_t* pixels, int w, int h, int stride, float flatness_in_pixels,
               const std::vector<vertex>& outline, float scale_x, float scale_y, float shift_x, float shift_y,
               int x_off, int y_off, bool invert) {
    const float scale = scale_x > scale_y ? scale_y : scale_x;
    const polygons poly = flatten(outline, flatness_in_pixels / scale);
    if (poly.points.empty()) return;

    const float y_scale = invert ? -scale_y : scale_y;
    std::vector<edge> edges;
    edges.reserve(poly.points.size() + 1);
    std::size_t first = 0;
    for (int length : poly.contour_lengths) {
        const point* p = poly.points.data() + first;
        first += std::size_t(length);
        for (int k = 0, j = length - 1; k < length; j = k++) {
            if (p[j].y == p[k].y) continue;  // horizontal edges add nothing
            int a = k, b = j;
            edge e{};
            e.invert = false;
            if (invert ? p[j].y > p[k].y : p[j].y < p[k].y) {
                e.invert = true;
                a = j, b = k;
            }
            e.x0 = p[a].x * scale_x + shift_x;
            e.y0 = p[a].y * y_scale + shift_y;
            e.x1 = p[b].x * scale_x + shift_x;
            e.y1 = p[b].y * y_scale + shift_y;
            edges.push_back(e);
        }
    }
    const int n = int(edges.size());
    edges.push_back({});  // room for the sentinel
    sort_edges(edges.data(), n);
    scanline_rasterizer(pixels, w, h, stride, std::move(edges)).run(x_off, y_off);
}

// ---------------------------------------------------------------------------------- bitmaps

box face::bitmap_box(int glyph, float scale_x, float scale_y, float shift_x, float shift_y) const {
    box b;
    if (!glyph_box(glyph, b)) return {};  // e.g. a space
    // whole pixels, counting every pixel the glyph touches
    return box{static_cast<int>(std::floor(b.x0 * scale_x + shift_x)), static_cast<int>(std::floor(-b.y1 * scale_y + shift_y)),
               static_cast<int>(std::ceil(b.x1 * scale_x + shift_x)), static_cast<int>(std::ceil(-b.y0 * scale_y + shift_y))};
}

void face::render_into(int glyph, std::uint8_t* out, int w, int h, int stride, float scale_x, float scale_y,
                       float shift_x, float shift_y) const {
    const box b = bitmap_box(glyph, scale_x, scale_y, shift_x, shift_y);
    if (w <= 0 || h <= 0) return;
    rasterize(out, w, h, stride, 0.35f, outline(glyph), scale_x, scale_y, shift_x, shift_y, b.x0, b.y0, true);
}

bitmap face::render(int glyph, float scale_x, float scale_y, float shift_x, float shift_y) const {
    if (scale_x == 0) scale_x = scale_y;
    if (scale_y == 0) {
        if (scale_x == 0) return {};
        scale_y = scale_x;
    }
    const box b = bitmap_box(glyph, scale_x, scale_y, shift_x, shift_y);
    bitmap bm;
    bm.width = b.x1 - b.x0;
    bm.height = b.y1 - b.y0;
    bm.x_offset = b.x0;
    bm.y_offset = b.y0;
    if (bm.width > 0 && bm.height > 0) {
        bm.pixels.assign(std::size_t(bm.width) * std::size_t(bm.height), 0);
        rasterize(bm.pixels.data(), bm.width, bm.height, bm.width, 0.35f, outline(glyph), scale_x, scale_y, shift_x,
                  shift_y, b.x0, b.y0, true);
    }
    return bm;
}

}  // namespace stb::truetype
