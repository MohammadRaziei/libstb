#include "stb/raster.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "draw/raster/dash.hpp"
#include "draw/raster/gray_raster.hpp"
#include "draw/raster/stroker.hpp"

namespace stb {

namespace {

using detail::fixed;
using detail::fvec;
using detail::outline;

// A coordinate in pixels as 26.6 fixed point (1/64 pixel), rounded to the nearest.
fixed to_fixed(float v) noexcept { return fixed(std::round(v * 64.f)); }

fvec to_fixed(point p) noexcept { return {to_fixed(p.x), to_fixed(p.y)}; }

// The device-space outline of cubic-only `commands` moved through `m`. Empty when `m` produces a
// coordinate that is NaN or infinite.
outline make_outline(const std::vector<path_command>& commands, const matrix& m) {
    constexpr float limit = 1000000.f;
    outline o;
    o.points.reserve(commands.size() * 2);
    o.tags.reserve(commands.size() * 2);

    bool ok = true;
    auto map = [&](point p) {
        p = m.map(p);
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            ok = false;
            return fvec{};
        }
        p.x = std::max(-limit, std::min(limit, p.x));
        p.y = std::max(-limit, std::min(limit, p.y));
        return to_fixed(p);
    };

    for (const path_command& c : commands) {
        switch (c.op) {
            case path_op::move_to: o.move_to(map(c.p[0])); break;
            case path_op::line_to: o.line_to(map(c.p[0])); break;
            case path_op::cubic_to: {
                const fvec a = map(c.p[0]);
                const fvec b = map(c.p[1]);
                o.cubic_to(a, b, map(c.p[2]));
                break;
            }
            case path_op::close: o.close(); break;
            case path_op::quad_to: break;  // converted before
        }
    }
    o.end();
    if (!ok) return {};
    return o;
}

}  // namespace

void rasterize_fill(const path& p, fill_rule rule, const matrix& m, const clip_rect& clip, span_sink sink) {
    if (p.empty()) return;
    const outline o = make_outline(detail::to_cubic_commands(p), m);
    detail::rasterize_outline(o, rule == fill_rule::even_odd, clip, sink);
}

void rasterize_stroke(const path& p, const stroke_style& style, const matrix& m, const clip_rect& clip, span_sink sink) {
    if (p.empty() || !(style.width > 0.f) || !std::isfinite(style.width)) return;

    std::vector<path_command> cubics = detail::to_cubic_commands(p);

    // Dashes are cut in the path's own space, before the matrix.
    if (detail::valid_dashes(style.dashes.data(), style.dashes.size()) && std::isfinite(style.dash_offset)) {
        cubics = detail::dash_commands(cubics, style.dashes.data(), style.dashes.size(), style.dash_offset);
    }

    const outline center = make_outline(cubics, m);
    if (center.empty()) return;

    // The line's width is given in path units; in pixels it is scaled by the mean scale of the matrix.
    const float sx2 = m.a * m.a + m.b * m.b;
    const float sy2 = m.c * m.c + m.d * m.d;
    const double scale_x = std::sqrt(double(sx2));
    const double scale_y = std::sqrt(double(sy2));
    const double scale = std::hypot(scale_x, scale_y) / double(1.41421356237309504880f);
    const double width = double(style.width) * scale;
    const fixed radius = fixed(width * 0.5 * 64.0);
    if (radius <= 0) return;

    const float miter = std::isfinite(style.miter_limit) ? style.miter_limit : 1.f;
    const fixed miter_limit = fixed(std::min(miter, 1.0e6f) * 65536.f);

    const outline stroked = detail::stroke_outline(center, radius, style.cap, style.join, miter_limit);
    detail::rasterize_outline(stroked, false, clip, sink);
}

namespace {

void check_options(const image& img, const draw_options& options) {
    if (img.empty()) throw std::invalid_argument("stb::fill/stroke: the image is empty");
    if (!(options.opacity >= 0.f && options.opacity <= 1.f)) {
        throw std::invalid_argument("stb::fill/stroke: opacity must be between 0 and 1");
    }
}

// Paints the spans that `produce` reports as `c` onto `img`: a layer the size of their bounding box
// holds the colour with the coverage as alpha, and image::blend puts it on the image (so the blend
// mode, the opacity and the pixel formats work exactly as for any other overlay).
template <class Produce>
void paint(image& img, const color& c, const draw_options& options, Produce&& produce) {
    std::vector<span> spans;
    int x0 = img.width(), y0 = img.height(), x1 = 0, y1 = 0;

    produce(clip_rect{0, 0, img.width(), img.height()}, [&](span_view v) {
        for (const span& s : v) {
            spans.push_back(s);
            x0 = std::min(x0, s.x);
            x1 = std::max(x1, s.x + s.len);
            y0 = std::min(y0, s.y);
            y1 = std::max(y1, s.y + 1);
        }
    });
    if (spans.empty() || c.a() == 0 || options.opacity == 0.f) return;

    image layer(x1 - x0, y1 - y0, pixel_format::rgba);
    layer.fill(color(c.r(), c.g(), c.b(), 0));
    for (const span& s : spans) {
        std::uint8_t* d = layer.data() + std::size_t(s.y - y0) * layer.stride() + std::size_t(s.x - x0) * 4 + 3;
        const std::uint8_t a = std::uint8_t((unsigned(c.a()) * s.coverage + 127u) / 255u);
        for (int i = 0; i < s.len; ++i, d += 4) *d = a;
    }
    img.blend(layer, x0, y0, options.blend, options.opacity);
}

}  // namespace

void fill(image& img, const path& p, const color& c, fill_rule rule, const draw_options& options) {
    check_options(img, options);
    if (p.empty()) return;
    paint(img, c, options, [&](const clip_rect& clip, span_sink sink) {
        rasterize_fill(p, rule, options.transform, clip, sink);
    });
}

void stroke(image& img, const path& p, const color& c, const stroke_style& style, const draw_options& options) {
    check_options(img, options);
    if (p.empty()) return;
    paint(img, c, options, [&](const clip_rect& clip, span_sink sink) {
        rasterize_stroke(p, style, options.transform, clip, sink);
    });
}

}  // namespace stb
