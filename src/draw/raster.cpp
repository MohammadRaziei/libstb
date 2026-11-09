#include "stb/raster.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "draw/raster/dash.hpp"
#include "draw/raster/gray_raster.hpp"
#include "draw/raster/stroker.hpp"
#include "util/log.hpp"

namespace stb {

namespace {

using detail::fixed;
using detail::fvec;
using detail::fixed_outline;

// A coordinate in pixels as 26.6 fixed point (1/64 pixel), rounded to the nearest.
fixed to_fixed(float v) noexcept { return fixed(std::round(v * 64.f)); }

fvec to_fixed(point p) noexcept { return {to_fixed(p.x), to_fixed(p.y)}; }

// The device-space outline of cubic-only `commands` moved through `m`. Empty when `m` produces a
// coordinate that is NaN or infinite.
fixed_outline make_outline(const std::vector<outline_command>& commands, const matrix& m) {
    constexpr float limit = 1000000.f;
    fixed_outline o;
    o.points.reserve(commands.size() * 2);
    o.tags.reserve(commands.size() * 2);

    bool ok = true, clamped = false;
    auto map = [&](point p) {
        p = m.map(p);
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            ok = false;
            return fvec{};
        }
        if (std::fabs(p.x) > limit || std::fabs(p.y) > limit) clamped = true;
        p.x = std::max(-limit, std::min(limit, p.x));
        p.y = std::max(-limit, std::min(limit, p.y));
        return to_fixed(p);
    };

    for (const outline_command& c : commands) {
        switch (c.op) {
            case outline_op::move_to: o.move_to(map(c.p[0])); break;
            case outline_op::line_to: o.line_to(map(c.p[0])); break;
            case outline_op::cubic_to: {
                const fvec a = map(c.p[0]);
                const fvec b = map(c.p[1]);
                o.cubic_to(a, b, map(c.p[2]));
                break;
            }
            case outline_op::close: o.close(); break;
            case outline_op::quad_to: break;  // converted before
        }
    }
    o.end();
    if (!ok) {
        detail::loggers::raster().warning("the transformation gives a NaN or infinite coordinate: nothing is drawn");
        return {};
    }
    if (clamped) detail::loggers::raster().warning("the transformation moves a coordinate beyond +-%g pixels: it is moved to that limit", double(limit));
    return o;
}

}  // namespace

void rasterize_fill(const outline& p, fill_rule rule, const matrix& m, const clip_rect& clip, span_sink sink) {
    if (p.empty()) return;
    const fixed_outline o = make_outline(detail::to_cubic_commands(p), m);
    detail::rasterize_outline(o, rule == fill_rule::even_odd, clip, sink);
}

void rasterize_stroke(const outline& p, const stroke_style& style, const matrix& m, const clip_rect& clip, span_sink sink) {
    if (p.empty() || !(style.width > 0.f) || !std::isfinite(style.width)) {
        if (!p.empty() && !std::isfinite(style.width)) detail::loggers::raster().warning("the stroke width is NaN or infinite: nothing is drawn");
        return;
    }

    std::vector<outline_command> cubics = detail::to_cubic_commands(p);

    // Dashes are cut in the outline's own space, before the matrix.
    if (detail::valid_dashes(style.dashes.data(), style.dashes.size()) && std::isfinite(style.dash_offset)) {
        cubics = detail::dash_commands(cubics, style.dashes.data(), style.dashes.size(), style.dash_offset);
    } else if (!style.dashes.empty()) {
        detail::loggers::raster().warning("the dash pattern is not usable (a negative or non-finite length, a zero sum, or a non-finite offset): the line is solid");
    }

    const fixed_outline center = make_outline(cubics, m);
    if (center.empty()) return;

    // The line's width is given in outline units; in pixels it is scaled by the mean scale of the matrix.
    const float sx2 = m.a * m.a + m.b * m.b;
    const float sy2 = m.c * m.c + m.d * m.d;
    const double scale_x = std::sqrt(double(sx2));
    const double scale_y = std::sqrt(double(sy2));
    const double scale = std::hypot(scale_x, scale_y) / double(1.41421356237309504880f);
    const double width = double(style.width) * scale;
    const fixed radius = fixed(width * 0.5 * 64.0);
    if (radius <= 0) return;

    if (!std::isfinite(style.miter_limit)) detail::loggers::raster().warning("the miter limit is NaN or infinite: 1 is used");
    const float miter = std::isfinite(style.miter_limit) ? style.miter_limit : 1.f;
    const fixed miter_limit = fixed(std::min(miter, 1.0e6f) * 65536.f);

    const fixed_outline stroked = detail::stroke_outline(center, radius, style.cap, style.join, miter_limit);
    detail::rasterize_outline(stroked, false, clip, sink);
}

namespace {

void check_options(const image& img, const draw_options& options) {
    if (img.empty()) throw std::invalid_argument("stb::fill/stroke: the image is empty");
    if (!(options.opacity >= 0.f && options.opacity <= 1.f)) {
        throw std::invalid_argument("stb::fill/stroke: opacity must be between 0 and 1");
    }
}

std::uint8_t unit_to_byte(float v) { return std::uint8_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); }

// One pixel of the image moved towards the pixel the operator made, by `k` (the coverage, 0..1). With alpha the
// mix is done on premultiplied colours (the right one for alpha), without it directly.
void mix_pixel(std::uint8_t* d, const std::uint8_t* r, int colours, bool alpha, float k) {
    if (!alpha) {
        for (int i = 0; i < colours; ++i) d[i] = unit_to_byte((float(d[i]) + (float(r[i]) - float(d[i])) * k) / 255.f);
        return;
    }
    const float da = float(d[colours]) / 255.f, ra = float(r[colours]) / 255.f;
    const float oa = da + (ra - da) * k;
    for (int i = 0; i < colours; ++i) {
        const float pd = float(d[i]) / 255.f * da, pr = float(r[i]) / 255.f * ra;
        d[i] = unit_to_byte(oa > 0.f ? (pd + (pr - pd) * k) / oa : 0.f);
    }
    d[colours] = unit_to_byte(oa);
}

// The Porter-Duff operators other than source_over. image::blend runs the operator on a copy of the part of the image
// under the shape's bounding box, with the colour at its full alpha (the coverage is NOT in it); then each covered
// pixel of the image moves towards that result by its coverage. Pixels the shape does not cover stay as they were.
void paint_composite(image& img, const color& c, const draw_options& options, const std::vector<span>& spans, int x0,
                     int y0, int x1, int y1) {
    const int w = x1 - x0, h = y1 - y0;
    image result = img.crop(x0, y0, w, h);
    image source(w, h, pixel_format::rgba);
    source.fill(c);
    result.blend(source, 0, 0, options.blend, options.opacity, options.composite);

    const int ch = img.channels();
    const bool alpha = ch == 2 || ch == 4;
    const int colours = alpha ? ch - 1 : ch;
    for (const span& s : spans) {
        std::uint8_t* d = img.data() + std::size_t(s.y) * img.stride() + std::size_t(s.x) * std::size_t(ch);
        const std::uint8_t* r = result.data() + std::size_t(s.y - y0) * result.stride() + std::size_t(s.x - x0) * std::size_t(ch);
        const float k = float(s.coverage) / 255.f;
        for (int i = 0; i < s.len; ++i, d += ch, r += ch) mix_pixel(d, r, colours, alpha, k);
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
    if (spans.empty()) return;
    if (options.composite != composite_op::source_over) {
        paint_composite(img, c, options, spans, x0, y0, x1, y1);
        return;
    }
    if (c.a() == 0 || options.opacity == 0.f) return;

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

void fill(image& img, const outline& p, const color& c, fill_rule rule, const draw_options& options) {
    check_options(img, options);
    if (p.empty()) return;
    paint(img, c, options, [&](const clip_rect& clip, span_sink sink) {
        rasterize_fill(p, rule, options.transform, clip, sink);
    });
}

void stroke(image& img, const outline& p, const color& c, const stroke_style& style, const draw_options& options) {
    check_options(img, options);
    if (p.empty()) return;
    paint(img, c, options, [&](const clip_rect& clip, span_sink sink) {
        rasterize_stroke(p, style, options.transform, clip, sink);
    });
}

}  // namespace stb
