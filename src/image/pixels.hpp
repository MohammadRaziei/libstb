#pragma once

// Internal: small helpers shared by the pixel code in ops.cpp and blend.cpp.

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>

#include "image/kernels.hpp"
#include "stb/color.hpp"
#include "stb/image.hpp"

namespace stb {
namespace detail {

inline void need_pixels(const image& img) {
    if (img.empty()) throw std::invalid_argument("image is empty");
}

// Pixels the caller overwrites completely (no zero-fill pass). 2 GiB is the ceiling
// the rest of libstb works with (the encoders size everything with int).
inline image allocate(std::int64_t w, std::int64_t h, int c) {
    if (w < 1 || h < 1) throw std::invalid_argument("width and height must be >= 1");
    if (w > INT_MAX || h > INT_MAX || std::uint64_t(w) * std::uint64_t(h) * std::uint64_t(c) > std::uint64_t(INT_MAX))
        throw limit_error("result larger than 2 GiB");
    const std::size_t n = std::size_t(w) * std::size_t(h) * std::size_t(c);
    std::uint8_t* raw = new std::uint8_t[n];
    // If this constructor throws, it calls the deleter: no leak either way.
    std::shared_ptr<void> owner(raw, [](void* p) { delete[] static_cast<std::uint8_t*>(p); });
    return image::wrap(int(w), int(h), pixel_format_from_channels(c), raw, std::move(owner));
}

// `pixels` copies of the c-byte pixel px.
inline void fill_run(std::uint8_t* d, std::size_t pixels, const std::uint8_t* px, int c) {
    if (c == 1) {
        std::memset(d, px[0], pixels);
        return;
    }
    for (std::size_t i = 0; i < pixels; ++i, d += c) std::memcpy(d, px, std::size_t(c));
}

// A colour as one pixel of the given format: gray takes the luma, alpha is dropped where the
// format has none. Writes channel_count(f) bytes.
inline void pixel_of(const color& c, pixel_format f, std::uint8_t* px) {
    switch (f) {
        case pixel_format::gray: px[0] = luma(c.r(), c.g(), c.b()); break;
        case pixel_format::gray_alpha:
            px[0] = luma(c.r(), c.g(), c.b());
            px[1] = c.a();
            break;
        case pixel_format::rgb:
            px[0] = c.r();
            px[1] = c.g();
            px[2] = c.b();
            break;
        case pixel_format::rgba:
            px[0] = c.r();
            px[1] = c.g();
            px[2] = c.b();
            px[3] = c.a();
            break;
    }
}

// n pixels of `src` (sc channels) blended onto n pixels of `dst` (dc channels) in place: any
// mode and opacity (0..1), in floating point. blend_mode::normal with opacity 1 has the integer
// kernels instead (ops.cpp); this is the general case.
void blend_row(const std::uint8_t* src, int sc, std::uint8_t* dst, int dc, std::size_t n, blend_mode mode,
               float opacity);

}  // namespace detail
}  // namespace stb
