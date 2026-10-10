#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "image_kernels.hpp"
#include "simd.hpp"
#include "stb/image.hpp"

// Geometry, channel conversion and alpha compositing for stb::image. No stb header
// is needed here: these are plain loops over the pixel buffer.

namespace stb {
namespace {

using detail::backend;
using detail::composite_row;
using detail::convert_row;
using detail::luma;
using detail::pick_composite_scalar;
using detail::pick_convert_scalar;
using detail::row_fn;

void need_pixels(const image& img) {
    if (img.empty()) throw std::invalid_argument("image is empty");
}

void need_channels(int c) {
    if (c < 1 || c > 4) throw std::invalid_argument("channels must be in 1..4");
}

// Pixels the caller overwrites completely (no zero-fill pass). 2 GiB is the ceiling
// the rest of libstb works with (the encoders size everything with int).
image allocate(std::int64_t w, std::int64_t h, int c) {
    if (w < 1 || h < 1) throw std::invalid_argument("width and height must be >= 1");
    if (w > INT_MAX || h > INT_MAX || std::uint64_t(w) * std::uint64_t(h) * std::uint64_t(c) > std::uint64_t(INT_MAX))
        throw limit_error("result larger than 2 GiB");
    const std::size_t n = std::size_t(w) * std::size_t(h) * std::size_t(c);
    std::uint8_t* raw = new std::uint8_t[n];
    // If this constructor throws, it calls the deleter: no leak either way.
    std::shared_ptr<void> owner(raw, [](void* p) { delete[] static_cast<std::uint8_t*>(p); });
    return image::wrap(int(w), int(h), c, raw, std::move(owner));
}

// ------------------------------------------------------------ flips and turns
//
// Every flip / quarter turn / transpose is out(x', y') = in(sx, sy) with sx, sy
// affine in x', y'. In bytes: source offset = off0 + x' * step_x + y' * step_y.

enum class op { flip_h, flip_v, rot180, transpose, rot90, rot270, transverse };

template <int C>
void remap(const std::uint8_t* src, std::uint8_t* dst, int ow, int oh, std::int64_t off0, std::int64_t step_x,
           std::int64_t step_y, bool tiled) {
    if (step_x == C) {  // rows stay contiguous: one memcpy per row
        for (int y = 0; y < oh; ++y)
            std::memcpy(dst + std::size_t(y) * std::size_t(ow) * C, src + off0 + y * step_y,
                        std::size_t(ow) * C);
        return;
    }
    if (!tiled) {
        for (int y = 0; y < oh; ++y) {
            const std::uint8_t* p = src + off0 + y * step_y;
            for (int x = 0; x < ow; ++x, p += step_x, dst += C) std::memcpy(dst, p, C);
        }
        return;
    }
    // A turn or transpose reads the source down its columns: do it in small tiles so
    // both sides stay in cache.
    constexpr int T = 32;
    for (int y0 = 0; y0 < oh; y0 += T)
        for (int x0 = 0; x0 < ow; x0 += T) {
            const int y1 = std::min(y0 + T, oh), x1 = std::min(x0 + T, ow);
            for (int y = y0; y < y1; ++y) {
                const std::uint8_t* p = src + off0 + y * step_y + x0 * step_x;
                std::uint8_t* d = dst + (std::size_t(y) * std::size_t(ow) + std::size_t(x0)) * C;
                for (int x = x0; x < x1; ++x, p += step_x, d += C) std::memcpy(d, p, C);
            }
        }
}

image apply(const image& in, op o) {
    need_pixels(in);
    const std::int64_t W = in.width(), H = in.height(), C = in.channels(), S = std::int64_t(in.stride());
    // sx = a + bx*x' + by*y'     sy = c + dx*x' + dy*y'
    std::int64_t a = 0, bx = 1, by = 0, c = 0, dx = 0, dy = 1;
    std::int64_t ow = W, oh = H;
    switch (o) {
        case op::flip_h:    a = W - 1; bx = -1; break;
        case op::flip_v:    c = H - 1; dy = -1; break;
        case op::rot180:    a = W - 1; bx = -1; c = H - 1; dy = -1; break;
        case op::transpose: ow = H; oh = W; bx = 0; by = 1; dx = 1; dy = 0; break;
        case op::rot90:     ow = H; oh = W; bx = 0; by = 1; c = H - 1; dx = -1; dy = 0; break;
        case op::rot270:    ow = H; oh = W; a = W - 1; bx = 0; by = -1; dx = 1; dy = 0; break;
        case op::transverse:ow = H; oh = W; a = W - 1; bx = 0; by = -1; c = H - 1; dx = -1; dy = 0; break;
    }
    image out = allocate(ow, oh, int(C));
    const std::int64_t off0 = c * S + a * C, step_x = dx * S + bx * C, step_y = dy * S + by * C;
    const bool tiled = bx == 0;  // x' moves down the source's columns
    switch (C) {
        case 1: remap<1>(in.data(), out.data(), int(ow), int(oh), off0, step_x, step_y, tiled); break;
        case 2: remap<2>(in.data(), out.data(), int(ow), int(oh), off0, step_x, step_y, tiled); break;
        case 3: remap<3>(in.data(), out.data(), int(ow), int(oh), off0, step_x, step_y, tiled); break;
        default: remap<4>(in.data(), out.data(), int(ow), int(oh), off0, step_x, step_y, tiled); break;
    }
    return out;
}

// ------------------------------------------------------------------- kernels
//
// The loops over whole rows of pixels that dominate run time (channel conversion and
// alpha blending) live in image_kernels.hpp as always-inline templates and are
// instantiated three times: here as plain code (vectorised by the compiler with the
// platform's baseline SIMD: SSE2 / NEON), here again with the AVX2 target attribute, and in
// image_kernels_scalar.cpp compiled with auto-vectorisation off. Which one runs is the
// backend chosen in simd.cpp. All three compute the same integers: results are identical.

template <int SC, int DC>
void convert_base(const std::uint8_t* s, std::uint8_t* d, std::size_t n) { convert_row<SC, DC>(s, d, n); }
template <int SC, int DC>
void composite_base(const std::uint8_t* s, std::uint8_t* d, std::size_t n) { composite_row<SC, DC>(s, d, n); }

#ifdef STB_HAVE_AVX2_DISPATCH
template <int SC, int DC>
STB_TARGET_AVX2 void convert_avx2(const std::uint8_t* s, std::uint8_t* d, std::size_t n) { convert_row<SC, DC>(s, d, n); }
template <int SC, int DC>
STB_TARGET_AVX2 void composite_avx2(const std::uint8_t* s, std::uint8_t* d, std::size_t n) { composite_row<SC, DC>(s, d, n); }
#define STB_PICK(FN, SC, DC) (avx ? &FN##_avx2<SC, DC> : &FN##_base<SC, DC>)
#else
#define STB_PICK(FN, SC, DC) (&FN##_base<SC, DC>)
#endif

row_fn pick_convert(int sc, int dc, backend be) {
    if (be == backend::scalar) return pick_convert_scalar(sc, dc);
    const bool avx = be == backend::avx2;
    (void)avx;
    switch (sc * 10 + dc) {
        STB_CONVERT_CASES(STB_PICK)
        default: return nullptr;  // same channel count
    }
}

row_fn pick_composite(int sc, int dc, backend be) {  // sc is 2 or 4
    if (be == backend::scalar) return pick_composite_scalar(sc, dc);
    const bool avx = be == backend::avx2;
    (void)avx;
    switch (sc * 10 + dc) {
        STB_COMPOSITE_CASES(STB_PICK)
        default: return STB_PICK(composite, 4, 4);
    }
}

// The size that fits inside (mw, mh) with the aspect ratio of (w, h); one side is
// exactly the limit.
void fit_size(int w, int h, int mw, int mh, int& nw, int& nh) {
    const std::int64_t by_width = std::int64_t(mw) * h, by_height = std::int64_t(mh) * w;
    if (by_width <= by_height) {  // the width is the limiting side
        nw = mw;
        nh = int(std::max<std::int64_t>(1, (std::int64_t(h) * mw + w / 2) / w));
    } else {
        nh = mh;
        nw = int(std::max<std::int64_t>(1, (std::int64_t(w) * mh + h / 2) / h));
    }
}

void fill_run(std::uint8_t* d, std::size_t pixels, const std::uint8_t* px, int c) {
    if (c == 1) {
        std::memset(d, px[0], pixels);
        return;
    }
    for (std::size_t i = 0; i < pixels; ++i, d += c) std::memcpy(d, px, std::size_t(c));
}

}  // namespace

// ------------------------------------------------------------------ geometry

image image::crop(int x, int y, int width, int height) const {
    need_pixels(*this);
    if (width < 1 || height < 1) throw std::invalid_argument("crop width and height must be >= 1");
    if (x < 0 || y < 0 || std::int64_t(x) + width > width_ || std::int64_t(y) + height > height_)
        throw std::invalid_argument("crop rectangle is not inside the image");
    image out = allocate(width, height, channels_);
    const std::size_t row = std::size_t(width) * std::size_t(channels_);
    for (int r = 0; r < height; ++r)
        std::memcpy(out.data() + std::size_t(r) * row,
                    data() + std::size_t(y + r) * stride() + std::size_t(x) * std::size_t(channels_), row);
    return out;
}

image image::flip_horizontal() const { return apply(*this, op::flip_h); }
image image::flip_vertical() const { return apply(*this, op::flip_v); }
image image::transpose() const { return apply(*this, op::transpose); }

image image::rotate90(int turns) const {
    need_pixels(*this);
    switch (((turns % 4) + 4) % 4) {
        case 1: return apply(*this, op::rot90);
        case 2: return apply(*this, op::rot180);
        case 3: return apply(*this, op::rot270);
        default: return copy();
    }
}

image image::orient(int orientation) const {
    need_pixels(*this);
    switch (orientation) {
        case 1: return copy();
        case 2: return apply(*this, op::flip_h);
        case 3: return apply(*this, op::rot180);
        case 4: return apply(*this, op::flip_v);
        case 5: return apply(*this, op::transpose);
        case 6: return apply(*this, op::rot90);
        case 7: return apply(*this, op::transverse);
        case 8: return apply(*this, op::rot270);
        default: throw std::invalid_argument("EXIF orientation must be in 1..8");
    }
}

image image::pad(int left, int top, int right, int bottom, std::array<std::uint8_t, 4> fill) const {
    need_pixels(*this);
    if (left < 0 || top < 0 || right < 0 || bottom < 0)
        throw std::invalid_argument("pad widths must be >= 0");
    if (left == 0 && top == 0 && right == 0 && bottom == 0) return copy();
    const std::int64_t ow = std::int64_t(width_) + left + right, oh = std::int64_t(height_) + top + bottom;
    image out = allocate(ow, oh, channels_);
    const std::size_t c = std::size_t(channels_);
    std::uint8_t* d = out.data();
    for (std::int64_t r = 0; r < oh; ++r, d += std::size_t(ow) * c) {
        if (r < top || r >= std::int64_t(top) + height_) {
            fill_run(d, std::size_t(ow), fill.data(), channels_);
            continue;
        }
        fill_run(d, std::size_t(left), fill.data(), channels_);
        std::memcpy(d + std::size_t(left) * c, data() + std::size_t(r - top) * stride(), stride());
        fill_run(d + (std::size_t(left) + std::size_t(width_)) * c, std::size_t(right), fill.data(), channels_);
    }
    return out;
}

image image::thumbnail(int max_width, int max_height, const resizer* r) const {
    need_pixels(*this);
    if (max_width < 1 || max_height < 1) throw std::invalid_argument("thumbnail size must be >= 1");
    if (width_ <= max_width && height_ <= max_height) return copy();
    int nw = 0, nh = 0;
    fit_size(width_, height_, max_width, max_height, nw, nh);
    return resize(nw, nh, r);
}

image image::thumbnail(int max_width, int max_height, resize_filter filter) const {
    need_pixels(*this);
    if (max_width < 1 || max_height < 1) throw std::invalid_argument("thumbnail size must be >= 1");
    if (width_ <= max_width && height_ <= max_height) return copy();
    int nw = 0, nh = 0;
    fit_size(width_, height_, max_width, max_height, nw, nh);
    return resize(nw, nh, filter);
}

image image::thumbnail(int max_width, int max_height, std::string_view filter_name) const {
    need_pixels(*this);
    if (max_width < 1 || max_height < 1) throw std::invalid_argument("thumbnail size must be >= 1");
    if (width_ <= max_width && height_ <= max_height) return copy();
    int nw = 0, nh = 0;
    fit_size(width_, height_, max_width, max_height, nw, nh);
    return resize(nw, nh, filter_name);
}

// ------------------------------------------------------------------ channels

image image::convert(int channels) const {
    need_pixels(*this);
    need_channels(channels);
    if (channels == channels_) return copy();
    image out = allocate(width_, height_, channels);
    pick_convert(channels_, channels, detail::current_backend())(data(), out.data(),
                                                         std::size_t(width_) * std::size_t(height_));
    return out;
}

std::vector<image> image::split() const {
    need_pixels(*this);
    const std::size_t n = std::size_t(width_) * std::size_t(height_), c = std::size_t(channels_);
    std::vector<image> out;
    out.reserve(c);
    for (std::size_t k = 0; k < c; ++k) {
        image plane = allocate(width_, height_, 1);
        const std::uint8_t* s = data() + k;
        std::uint8_t* d = plane.data();
        for (std::size_t i = 0; i < n; ++i, s += c) d[i] = *s;
        out.push_back(std::move(plane));
    }
    return out;
}

image image::merge(const std::vector<const image*>& channels) {
    const std::size_t c = channels.size();
    if (c < 1 || c > 4) throw std::invalid_argument("merge needs 1 to 4 channel images");
    for (const image* p : channels) {
        if (!p || p->empty()) throw std::invalid_argument("merge: empty image");
        if (p->channels() != 1) throw std::invalid_argument("merge needs single-channel images");
        if (p->width() != channels[0]->width() || p->height() != channels[0]->height())
            throw std::invalid_argument("merge needs images of the same size");
    }
    image out = allocate(channels[0]->width(), channels[0]->height(), int(c));
    const std::size_t n = std::size_t(out.width()) * std::size_t(out.height());
    for (std::size_t k = 0; k < c; ++k) {
        const std::uint8_t* s = channels[k]->data();
        std::uint8_t* d = out.data() + k;
        for (std::size_t i = 0; i < n; ++i, d += c) *d = s[i];
    }
    return out;
}

image image::merge(const std::vector<image>& channels) {
    std::vector<const image*> ptrs;
    ptrs.reserve(channels.size());
    for (const image& i : channels) ptrs.push_back(&i);
    return merge(ptrs);
}

// --------------------------------------------------------------------- alpha

image image::composite(const image& overlay, int x, int y) const {
    need_pixels(*this);
    need_pixels(overlay);
    image out = copy();
    // The part of the overlay that lands inside this image.
    const std::int64_t x0 = std::max<std::int64_t>(0, x), y0 = std::max<std::int64_t>(0, y);
    const std::int64_t x1 = std::min<std::int64_t>(width_, std::int64_t(x) + overlay.width());
    const std::int64_t y1 = std::min<std::int64_t>(height_, std::int64_t(y) + overlay.height());
    if (x0 >= x1 || y0 >= y1) return out;

    const int dc = channels_, sc = overlay.channels();
    const std::size_t n = std::size_t(x1 - x0);
    // An overlay without alpha is opaque: its rows replace the destination's (converted to
    // this image's channel count); one with alpha is blended.
    const bool blend = sc == 2 || sc == 4;
    const row_fn fn = blend ? pick_composite(sc, dc, detail::current_backend())
                            : (sc == dc ? nullptr : pick_convert(sc, dc, detail::current_backend()));
    for (std::int64_t py = y0; py < y1; ++py) {
        std::uint8_t* d = out.data() + std::size_t(py) * stride() + std::size_t(x0) * std::size_t(dc);
        const std::uint8_t* s = overlay.data() + std::size_t(py - y) * overlay.stride() +
                                std::size_t(x0 - x) * std::size_t(sc);
        if (fn)
            fn(s, d, n);
        else
            std::memcpy(d, s, n * std::size_t(dc));
    }
    return out;
}

image image::flatten(std::array<std::uint8_t, 3> background) const {
    need_pixels(*this);
    if (channels_ == 1 || channels_ == 3) return copy();
    const int dc = channels_ - 1;
    image base = allocate(width_, height_, dc);
    const std::uint8_t px[3] = {dc == 1 ? luma(background[0], background[1], background[2]) : background[0],
                                background[1], background[2]};
    fill_run(base.data(), std::size_t(width_) * std::size_t(height_), px, dc);
    return base.composite(*this);
}

}  // namespace stb
