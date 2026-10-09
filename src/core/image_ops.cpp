#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "simd.hpp"
#include "stb/image.hpp"

// Geometry, channel conversion and alpha compositing for stb::image. No stb header
// is needed here: these are plain loops over the pixel buffer.

namespace stb {
namespace {

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

inline std::uint8_t luma(int r, int g, int b) {
    // 0.299 R + 0.587 G + 0.114 B in 16-bit fixed point (19595 + 38470 + 7471 == 65536),
    // rounded: the same integer formula as Pillow's convert("L").
    return std::uint8_t((r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16);
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
// alpha blending) are written once, as always-inline templates, and instantiated twice:
// as plain code, and with the AVX2 target attribute so the compiler vectorises them with
// 256-bit registers. The AVX2 copy is picked at run time (see simd.hpp). They are
// branchless on purpose: a data-dependent branch is what stops the vectoriser.
// The two copies compute the same integers, so the results are bit-identical.

#if defined(__GNUC__) || defined(__clang__)
#define STB_INLINE inline __attribute__((always_inline))
#else
#define STB_INLINE inline
#endif

using row_fn = void (*)(const std::uint8_t* src, std::uint8_t* dst, std::size_t pixels);

template <int SC, int DC>
STB_INLINE void convert_row(const std::uint8_t* s, std::uint8_t* d, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i, s += SC, d += DC) {
        if constexpr (DC <= 2) {
            d[0] = SC <= 2 ? s[0] : luma(s[0], s[1], s[2]);
            if constexpr (DC == 2) d[1] = (SC == 2 || SC == 4) ? s[SC - 1] : std::uint8_t(255);
        } else {
            if constexpr (SC <= 2) {
                d[0] = d[1] = d[2] = s[0];
            } else {
                d[0] = s[0], d[1] = s[1], d[2] = s[2];
            }
            if constexpr (DC == 4) d[3] = (SC == 2 || SC == 4) ? s[SC - 1] : std::uint8_t(255);
        }
    }
}

// Source over destination, straight (non-premultiplied) alpha, for a source WITH alpha
// (SC 2 or 4). The overlay's colour is taken in the destination's colour model (gray
// destination: luma of an RGB overlay; RGB destination: gray replicated).
//
// Destination without alpha:   d' = (s*sa + d*(255-sa) + 127) / 255
// Destination with alpha, with ws = 255*sa, wd = da*(255-sa), den = ws + wd:
//     d' = round-down((s*ws + d*wd + den/2) / den) = d + floor(((s-d)*ws + den/2) / den)
//     a' = (den + 127) / 255
// The second form keeps the numerator below 2^24, so it is exact in a float and the
// division can be a (correctly rounded) float division: floor(q) = trunc(q) minus one
// when truncation went up. Checked against the plain integer division for every
// (sa, da, colour pair) in the tests. den == 0 only when both alphas are 0, and then
// the formula returns the destination unchanged.
// RGBA over RGBA, the common case: a pixel is one 32-bit word, so the vectoriser works on
// whole registers of pixels instead of shuffling bytes in and out.
STB_INLINE void composite_rgba_row(const std::uint8_t* s, std::uint8_t* d, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t sv, dv;
        std::memcpy(&sv, s + 4 * i, 4);
        std::memcpy(&dv, d + 4 * i, 4);
        const int sa = int(sv >> 24), da = int(dv >> 24);
        const int ws = sa * 255, wd = da * (255 - sa), den = ws + wd, half = den / 2;
        const float fden = float(den + int(den == 0));
        std::uint32_t out = std::uint32_t((den + 127) / 255) << 24;
        for (int k = 0; k < 3; ++k) {
            const int dk = int((dv >> (8 * k)) & 255), diff = int((sv >> (8 * k)) & 255) - dk;
            const float q = float(diff * ws + half) / fden;
            int t = int(q);
            t -= int(float(t) > q);
            out |= std::uint32_t(dk + t) << (8 * k);
        }
        std::memcpy(d + 4 * i, &out, 4);
    }
}

template <int SC, int DC>
STB_INLINE void composite_row(const std::uint8_t* s, std::uint8_t* d, std::size_t n) {
    if constexpr (SC == 4 && DC == 4) {
        composite_rgba_row(s, d, n);
        return;
    }
    constexpr bool dst_alpha = DC == 2 || DC == 4;
    constexpr int NC = DC <= 2 ? 1 : 3;  // colour channels of the destination
    for (std::size_t i = 0; i < n; ++i, s += SC, d += DC) {
        const int sa = s[SC - 1];
        int sc[NC];
        if constexpr (NC == 1) {
            sc[0] = SC == 2 ? s[0] : luma(s[0], s[1], s[2]);
        } else {
            for (int k = 0; k < 3; ++k) sc[k] = SC == 2 ? s[0] : s[k];
        }
        if constexpr (!dst_alpha) {
            for (int k = 0; k < NC; ++k) d[k] = std::uint8_t((sc[k] * sa + int(d[k]) * (255 - sa) + 127) / 255);
        } else {
            const int da = d[DC - 1];
            const int ws = sa * 255, wd = da * (255 - sa), den = ws + wd, half = den / 2;
            const float fden = float(den + int(den == 0));
            for (int k = 0; k < NC; ++k) {
                const int dk = d[k];
                const float q = float((sc[k] - dk) * ws + half) / fden;
                int t = int(q);  // truncates toward zero
                t -= int(float(t) > q);
                d[k] = std::uint8_t(dk + t);
            }
            d[DC - 1] = std::uint8_t((den + 127) / 255);
        }
    }
}

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

row_fn pick_convert(int sc, int dc, bool avx) {
    (void)avx;
    switch (sc * 10 + dc) {
        case 12: return STB_PICK(convert, 1, 2);
        case 13: return STB_PICK(convert, 1, 3);
        case 14: return STB_PICK(convert, 1, 4);
        case 21: return STB_PICK(convert, 2, 1);
        case 23: return STB_PICK(convert, 2, 3);
        case 24: return STB_PICK(convert, 2, 4);
        case 31: return STB_PICK(convert, 3, 1);
        case 32: return STB_PICK(convert, 3, 2);
        case 34: return STB_PICK(convert, 3, 4);
        case 41: return STB_PICK(convert, 4, 1);
        case 42: return STB_PICK(convert, 4, 2);
        case 43: return STB_PICK(convert, 4, 3);
        default: return nullptr;  // same channel count
    }
}

row_fn pick_composite(int sc, int dc, bool avx) {  // sc is 2 or 4
    (void)avx;
    switch (sc * 10 + dc) {
        case 21: return STB_PICK(composite, 2, 1);
        case 22: return STB_PICK(composite, 2, 2);
        case 23: return STB_PICK(composite, 2, 3);
        case 24: return STB_PICK(composite, 2, 4);
        case 41: return STB_PICK(composite, 4, 1);
        case 42: return STB_PICK(composite, 4, 2);
        case 43: return STB_PICK(composite, 4, 3);
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
    pick_convert(channels_, channels, detail::use_avx2())(data(), out.data(),
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
    const row_fn fn = blend ? pick_composite(sc, dc, detail::use_avx2())
                            : (sc == dc ? nullptr : pick_convert(sc, dc, detail::use_avx2()));
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
