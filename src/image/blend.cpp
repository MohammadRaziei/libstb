#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "image/pixels.hpp"
#include "stb/image.hpp"

// Blend modes, fill, alpha masks and stacking for stb::image. The plain "over" operator
// (blend_mode::normal at full opacity) is in ops.cpp with the integer kernels; this file holds
// the general floating-point blend and the rest of the "put pixels into an image" operations.

namespace stb {
namespace {

using detail::allocate;
using detail::fill_run;
using detail::need_pixels;
using detail::pixel_of;

// The colour of mode m for one channel: b is the pixel below, s the overlay's, both in 0..1.
float mix(blend_mode m, float b, float s) {
    switch (m) {
        case blend_mode::normal: return s;
        case blend_mode::multiply: return b * s;
        case blend_mode::screen: return b + s - b * s;
        case blend_mode::overlay: return b <= 0.5f ? 2.0f * b * s : 1.0f - 2.0f * (1.0f - b) * (1.0f - s);
        case blend_mode::darken: return std::min(b, s);
        case blend_mode::lighten: return std::max(b, s);
        case blend_mode::add: return std::min(1.0f, b + s);
        case blend_mode::difference: return std::fabs(b - s);
    }
    return s;
}

std::uint8_t to_byte(float v) { return std::uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

}  // namespace

std::string_view blend_mode_name(blend_mode m) noexcept {
    switch (m) {
        case blend_mode::normal: return "normal";
        case blend_mode::multiply: return "multiply";
        case blend_mode::screen: return "screen";
        case blend_mode::overlay: return "overlay";
        case blend_mode::darken: return "darken";
        case blend_mode::lighten: return "lighten";
        case blend_mode::add: return "add";
        case blend_mode::difference: return "difference";
    }
    return {};
}

blend_mode blend_mode_from_name(std::string_view name) {
    for (const blend_mode m : {blend_mode::normal, blend_mode::multiply, blend_mode::screen, blend_mode::overlay,
                               blend_mode::darken, blend_mode::lighten, blend_mode::add, blend_mode::difference}) {
        if (blend_mode_name(m) == name) return m;
    }
    throw std::invalid_argument("unknown blend mode '" + std::string(name) +
                                "' (normal, multiply, screen, overlay, darken, lighten, add, difference)");
}

namespace detail {

// W3C compositing with straight alpha: sa is the overlay's alpha (times opacity), ab the alpha
// below, and the colour is  ((1 - sa) ab cb + sa (1 - ab) cs + sa ab B(cb, cs)) / alpha_out.
// Below an opaque pixel this is (1 - sa) cb + sa B(cb, cs); for blend_mode::normal it is "over".
void blend_row(const std::uint8_t* src, int sc, std::uint8_t* dst, int dc, std::size_t n, blend_mode mode,
               float opacity) {
    const int colours = dc <= 2 ? 1 : 3;     // colour channels of the destination
    const bool dst_alpha = dc == 2 || dc == 4;
    for (std::size_t i = 0; i < n; ++i, src += sc, dst += dc) {
        float rgb[3];
        float sa = 1.0f;
        if (sc <= 2) {
            rgb[0] = rgb[1] = rgb[2] = float(src[0]) / 255.0f;
            if (sc == 2) sa = float(src[1]) / 255.0f;
        } else {
            for (int k = 0; k < 3; ++k) rgb[k] = float(src[k]) / 255.0f;
            if (sc == 4) sa = float(src[3]) / 255.0f;
        }
        sa *= opacity;
        if (colours == 1) rgb[0] = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];  // overlay colour -> gray
        const float ab = dst_alpha ? float(dst[dc - 1]) / 255.0f : 1.0f;
        const float out_a = sa + ab * (1.0f - sa);
        for (int k = 0; k < colours; ++k) {
            const float cb = float(dst[k]) / 255.0f, cs = rgb[k];
            const float b = mix(mode, cb, cs);
            float out;
            if (!dst_alpha) out = (1.0f - sa) * cb + sa * b;
            else if (out_a > 0.0f) out = ((1.0f - sa) * ab * cb + sa * (1.0f - ab) * cs + sa * ab * b) / out_a;
            else out = 0.0f;
            dst[k] = to_byte(out);
        }
        if (dst_alpha) dst[dc - 1] = to_byte(out_a);
    }
}

}  // namespace detail

// ---------------------------------------------------------------------- fill

image& image::fill(const color& c, int x, int y, int width, int height) & {
    need_pixels(*this);
    if (width < 0 || height < 0) throw std::invalid_argument("width and height must be >= 0");
    const std::int64_t x0 = std::max<std::int64_t>(0, x), y0 = std::max<std::int64_t>(0, y);
    const std::int64_t x1 = std::min<std::int64_t>(width_, std::int64_t(x) + width);
    const std::int64_t y1 = std::min<std::int64_t>(height_, std::int64_t(y) + height);
    if (x0 >= x1 || y0 >= y1) return *this;
    std::uint8_t px[4];
    pixel_of(c, format(), px);
    for (std::int64_t py = y0; py < y1; ++py)
        fill_run(data() + std::size_t(py) * stride() + std::size_t(x0) * std::size_t(channels_), std::size_t(x1 - x0),
                 px, channels_);
    return *this;
}

image&& image::fill(const color& c, int x, int y, int width, int height) && {
    fill(c, x, y, width, height);
    return std::move(*this);
}

image& image::fill(const color& c) & {
    need_pixels(*this);
    return fill(c, 0, 0, width_, height_);
}

image&& image::fill(const color& c) && {
    fill(c);
    return std::move(*this);
}

// ---------------------------------------------------------------------- alpha

image image::alpha() const {
    need_pixels(*this);
    if (!has_alpha(format())) throw std::invalid_argument("image has no alpha channel");
    image out = allocate(width_, height_, 1);
    const std::size_t n = std::size_t(width_) * std::size_t(height_), c = std::size_t(channels_);
    const std::uint8_t* s = data() + (c - 1);
    std::uint8_t* d = out.data();
    for (std::size_t i = 0; i < n; ++i, s += c) d[i] = *s;
    return out;
}

image image::with_alpha(const image& mask) const {
    need_pixels(*this);
    need_pixels(mask);
    if (mask.channels() != 1) throw std::invalid_argument("mask must be a gray image");
    if (mask.width() != width_ || mask.height() != height_)
        throw std::invalid_argument("mask must have the size of the image");
    const int colours = has_alpha(format()) ? channels_ - 1 : channels_;
    image out = allocate(width_, height_, colours + 1);
    const std::size_t n = std::size_t(width_) * std::size_t(height_), sc = std::size_t(channels_),
                      cc = std::size_t(colours);
    const std::uint8_t* s = data();
    const std::uint8_t* m = mask.data();
    std::uint8_t* d = out.data();
    for (std::size_t i = 0; i < n; ++i, s += sc, d += cc + 1) {
        std::memcpy(d, s, cc);
        d[cc] = m[i];
    }
    return out;
}

// --------------------------------------------------------------------- stacks

namespace {

image stack(const std::vector<const image*>& images, int gap, const color& background, align where,
            bool horizontal) {
    if (images.empty()) throw std::invalid_argument("nothing to stack");
    if (gap < 0) throw std::invalid_argument("gap must be >= 0");
    bool colour = false, alpha = false;
    for (const image* i : images) {
        if (!i) throw std::invalid_argument("null image");
        need_pixels(*i);
        colour = colour || i->channels() >= 3;
        alpha = alpha || has_alpha(i->format());
    }
    const pixel_format fmt = pixel_format_from_channels((colour ? 3 : 1) + (alpha ? 1 : 0));
    const int c = channel_count(fmt);

    // Every input in the result's format (the ones that already are stay where they are).
    std::vector<image> converted;
    converted.reserve(images.size());
    std::vector<const image*> in;
    in.reserve(images.size());
    for (const image* i : images) {
        if (i->format() == fmt) {
            in.push_back(i);
        } else {
            converted.push_back(i->convert(fmt));
            in.push_back(&converted.back());
        }
    }

    std::int64_t main_size = std::int64_t(gap) * std::int64_t(in.size() - 1), cross_size = 0;
    for (const image* i : in) {
        main_size += horizontal ? i->width() : i->height();
        cross_size = std::max<std::int64_t>(cross_size, horizontal ? i->height() : i->width());
    }
    image out = horizontal ? allocate(main_size, cross_size, c) : allocate(cross_size, main_size, c);
    std::uint8_t px[4];
    pixel_of(background, fmt, px);
    fill_run(out.data(), std::size_t(out.width()) * std::size_t(out.height()), px, c);

    std::int64_t at = 0;
    for (const image* i : in) {
        const std::int64_t extent = horizontal ? i->width() : i->height();
        const std::int64_t cross = horizontal ? i->height() : i->width();
        const std::int64_t shift = where == align::start ? 0 : where == align::center ? (cross_size - cross) / 2
                                                                                     : cross_size - cross;
        const std::int64_t ox = horizontal ? at : shift, oy = horizontal ? shift : at;
        for (int row = 0; row < i->height(); ++row)
            std::memcpy(out.data() + (std::size_t(oy) + std::size_t(row)) * out.stride() + std::size_t(ox) * std::size_t(c),
                        i->data() + std::size_t(row) * i->stride(), i->stride());
        at += extent + gap;
    }
    return out;
}

std::vector<const image*> pointers(const std::vector<image>& images) {
    std::vector<const image*> p;
    p.reserve(images.size());
    for (const image& i : images) p.push_back(&i);
    return p;
}

}  // namespace

image image::hstack(const std::vector<const image*>& images, int gap, const color& background, align where) {
    return stack(images, gap, background, where, true);
}
image image::hstack(const std::vector<image>& images, int gap, const color& background, align where) {
    return stack(pointers(images), gap, background, where, true);
}
image image::vstack(const std::vector<const image*>& images, int gap, const color& background, align where) {
    return stack(images, gap, background, where, false);
}
image image::vstack(const std::vector<image>& images, int gap, const color& background, align where) {
    return stack(pointers(images), gap, background, where, false);
}

}  // namespace stb
