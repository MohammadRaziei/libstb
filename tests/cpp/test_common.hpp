#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libstb.h"
#include "utest/utest.h"

// utest.h has no exception macro: check the exact (base-or-derived) type.
#define ASSERT_THROWS(expr, ex)   \
    do {                          \
        bool caught_ = false;     \
        try {                     \
            (void)(expr);         \
        } catch (const ex&) {     \
            caught_ = true;       \
        }                         \
        ASSERT_TRUE(caught_);     \
    } while (0)

namespace testutil {

// Binary PPM (P6): the simplest format stb_image reads, so decode tests need
// no encoder (and no binary fixtures).
inline std::string ppm(int w, int h, const std::vector<std::uint8_t>& rgb) {
    std::string s = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    s.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    return s;
}

// 1x2 image: top row red, bottom row green.
inline std::string red_over_green() { return ppm(1, 2, {255, 0, 0, 0, 255, 0}); }

// Deterministic, non-flat pixels (defeats trivially-passing round trips).
inline libstb::image gradient(int w, int h, int c) {
    libstb::image img(w, h, c);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < c; ++k)
                img(x, y, k) = static_cast<std::uint8_t>((x * 31 + y * 17 + k * 53) & 0xFF);
    return img;
}

inline libstb::image solid(int w, int h, int c, std::uint8_t v) {
    return libstb::image(w, h, c, std::vector<std::uint8_t>(std::size_t(w) * h * c, v));
}

inline bool same(const libstb::image& a, const libstb::image& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           a.pixels() == b.pixels();
}

}  // namespace testutil
