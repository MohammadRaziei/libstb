#include "stb/draw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stb {
namespace {

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Fraction of the pixel whose centre is at signed distance d from the edge (negative = inside)
// that the shape covers: a one-pixel wide ramp around the edge.
double coverage(double d) { return clamp01(0.5 - d); }

// Signed distance from (px, py) to the outline: negative inside, positive outside.
struct distance_field {
    const shape& s;

    double operator()(double px, double py) const {
        switch (s.type) {
            case shape::kind::rect: {
                const double hx = (s.bx - s.ax) / 2, hy = (s.by - s.ay) / 2;
                const double r = std::min(s.radius, std::min(hx, hy));
                const double qx = std::fabs(px - (s.ax + hx)) - (hx - r);
                const double qy = std::fabs(py - (s.ay + hy)) - (hy - r);
                const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
                return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - r;
            }
            case shape::kind::ellipse: {
                // First-order distance estimate: exact for a circle, within a fraction of a pixel
                // near the edge of an ellipse (where the coverage ramp lives).
                const double a = (s.bx - s.ax) / 2, b = (s.by - s.ay) / 2;
                const double qx = px - (s.ax + a), qy = py - (s.ay + b);
                const double k0 = std::sqrt((qx / a) * (qx / a) + (qy / b) * (qy / b));
                const double k1 = std::sqrt((qx / (a * a)) * (qx / (a * a)) + (qy / (b * b)) * (qy / (b * b)));
                return k1 > 0 ? k0 * (k0 - 1) / k1 : -std::min(a, b);
            }
            case shape::kind::line: {
                const double dx = s.bx - s.ax, dy = s.by - s.ay;
                const double len2 = dx * dx + dy * dy;
                const double t = len2 > 0 ? clamp01(((px - s.ax) * dx + (py - s.ay) * dy) / len2) : 0.0;
                const double ex = px - (s.ax + t * dx), ey = py - (s.ay + t * dy);
                return std::sqrt(ex * ex + ey * ey) - s.stroke_width / 2;
            }
        }
        return 1;
    }
};

struct bounds {
    int x0, y0, x1, y1;  // [x0, x1) x [y0, y1), already clipped to the image
};

// The shape's extent grown by one pixel for the soft edge, clipped to a w x h image.
bounds clipped_bounds(const shape& s, int w, int h) {
    double lo_x = std::min(s.ax, s.bx), hi_x = std::max(s.ax, s.bx);
    double lo_y = std::min(s.ay, s.by), hi_y = std::max(s.ay, s.by);
    if (s.type == shape::kind::line) {
        const double r = s.stroke_width / 2;
        lo_x -= r;
        hi_x += r;
        lo_y -= r;
        hi_y += r;
    }
    auto lo = [](double v, int limit) { return int(std::max(0.0, std::min(std::floor(v) - 1, double(limit)))); };
    auto hi = [](double v, int limit) { return int(std::max(0.0, std::min(std::ceil(v) + 1, double(limit)))); };
    return {lo(lo_x, w), lo(lo_y, h), hi(hi_x, w), hi(hi_y, h)};
}

// One solid-colour layer whose alpha is c.a() * cov(px, py), blended onto dst.
template <class Coverage>
void paint(image& dst, const bounds& b, const color& c, Coverage cov) {
    if (b.x1 <= b.x0 || b.y1 <= b.y0) return;  // entirely outside the image
    image layer(b.x1 - b.x0, b.y1 - b.y0, pixel_format::rgba);
    for (int row = 0; row < layer.height(); ++row) {
        std::uint8_t* d = layer.data() + std::size_t(row) * layer.stride();
        const double py = b.y0 + row + 0.5;
        for (int col = 0; col < layer.width(); ++col, d += 4) {
            const double a = double(c.a()) * cov(b.x0 + col + 0.5, py);
            d[0] = c.r();
            d[1] = c.g();
            d[2] = c.b();
            d[3] = static_cast<std::uint8_t>(a + 0.5);
        }
    }
    dst.composite_in_place(layer, b.x0, b.y0);
}

}  // namespace

void draw_shape(image& dst, const shape& s) {
    if (s.radius < 0) throw std::invalid_argument("shape: radius must be >= 0");
    if (s.stroke_color && !(s.stroke_width > 0)) throw std::invalid_argument("shape: stroke width must be > 0");
    if (s.type == shape::kind::line && !s.stroke_color) throw std::invalid_argument("shape: a line needs a colour");

    const bounds b = clipped_bounds(s, dst.width(), dst.height());
    const distance_field d{s};

    if (s.type == shape::kind::line) {
        paint(dst, b, *s.stroke_color, [&](double px, double py) { return coverage(d(px, py)); });
        return;
    }
    if (s.fill) {
        paint(dst, b, *s.fill, [&](double px, double py) { return coverage(d(px, py)); });
    }
    if (s.stroke_color) {
        // The border is the ring between the outline and the outline moved inwards by its width.
        const double w = s.stroke_width;
        paint(dst, b, *s.stroke_color, [&](double px, double py) {
            const double v = d(px, py);
            return clamp01(coverage(v) - coverage(v + w));
        });
    }
}

}  // namespace stb
