#pragma once

// Internal: the per-row pixel kernels (channel conversion, alpha blending). Written once as
// always-inline templates; image_ops.cpp instantiates them as plain code and with the AVX2
// target attribute, image_kernels_scalar.cpp with auto-vectorisation off. They are
// branchless on purpose: a data-dependent branch is what stops the vectoriser. Every
// instantiation computes the same integers, so the results are bit-identical.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace stb {
namespace detail {

inline std::uint8_t luma(int r, int g, int b) {
    // 0.299 R + 0.587 G + 0.114 B in 16-bit fixed point (19595 + 38470 + 7471 == 65536),
    // rounded: the same integer formula as Pillow's convert("L").
    return std::uint8_t((r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16);
}

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


// The (source channels, destination channels) pairs the pickers dispatch on, as case labels.
// PICK(FN, SC, DC) names the instantiation to return for FN in {convert, composite}.
#define STB_CONVERT_CASES(PICK) \
    case 12: return PICK(convert, 1, 2); \
    case 13: return PICK(convert, 1, 3); \
    case 14: return PICK(convert, 1, 4); \
    case 21: return PICK(convert, 2, 1); \
    case 23: return PICK(convert, 2, 3); \
    case 24: return PICK(convert, 2, 4); \
    case 31: return PICK(convert, 3, 1); \
    case 32: return PICK(convert, 3, 2); \
    case 34: return PICK(convert, 3, 4); \
    case 41: return PICK(convert, 4, 1); \
    case 42: return PICK(convert, 4, 2); \
    case 43: return PICK(convert, 4, 3);

#define STB_COMPOSITE_CASES(PICK) \
    case 21: return PICK(composite, 2, 1); \
    case 22: return PICK(composite, 2, 2); \
    case 23: return PICK(composite, 2, 3); \
    case 24: return PICK(composite, 2, 4); \
    case 41: return PICK(composite, 4, 1); \
    case 42: return PICK(composite, 4, 2); \
    case 43: return PICK(composite, 4, 3);

// The same kernels compiled with auto-vectorisation off (image_kernels_scalar.cpp).
row_fn pick_convert_scalar(int sc, int dc);
row_fn pick_composite_scalar(int sc, int dc);  // sc is 2 or 4

}  // namespace detail
}  // namespace stb
