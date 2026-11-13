#pragma once

// Internal: sRGB (8 bits) to CIELAB (D65). L is 0..100, a and b about -128..127.
// Not vectorised on purpose: it uses the C library's cube root, which is the same function on every backend.

#include <cmath>
#include <cstdint>

namespace stb::detail {

struct lab_table {
    float linear[256];  // sRGB value -> linear light
    lab_table() {
        for (int i = 0; i < 256; ++i) {
            const double c = double(i) / 255.0;
            linear[i] = float(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
    }
};

inline const lab_table& lab_lut() {
    static const lab_table t;
    return t;
}

inline float lab_f(float t) { return t > 0.008856f ? std::cbrt(t) : 7.787037f * t + 16.0f / 116.0f; }

inline void rgb_to_lab(const lab_table& t, std::uint8_t r8, std::uint8_t g8, std::uint8_t b8, float* out) {
    const float r = t.linear[r8], g = t.linear[g8], b = t.linear[b8];
    const float x = (0.4124564f * r + 0.3575761f * g + 0.1804375f * b) / 0.95047f;
    const float y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
    const float z = (0.0193339f * r + 0.1191920f * g + 0.9503041f * b) / 1.08883f;
    const float fx = lab_f(x), fy = lab_f(y), fz = lab_f(z);
    out[0] = 116.0f * fy - 16.0f;
    out[1] = 500.0f * (fx - fy);
    out[2] = 200.0f * (fy - fz);
}

// n pixels of `channels` (1 to 4) 8-bit channels to n * 3 floats. 1 and 2 channels are gray (alpha ignored).
inline void row_to_lab(const lab_table& t, const std::uint8_t* s, int channels, float* d, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i, d += 3) {
        if (channels < 3) {
            const std::uint8_t g = s[i * std::size_t(channels)];
            rgb_to_lab(t, g, g, g, d);
        } else {
            const std::uint8_t* p = s + i * std::size_t(channels);
            rgb_to_lab(t, p[0], p[1], p[2], d);
        }
    }
}

}  // namespace stb::detail
