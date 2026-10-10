#include "image_kernels.hpp"

// Hand-written SSE2 versions of the kernels the compiler does not vectorise well (the
// per-pixel byte shuffling of 3-channel data defeats it): RGB / RGBA -> gray, and RGBA over
// RGB. SSE2 is part of the x86-64 baseline, so these need no run-time check; they are what
// the "sse2" backend runs, with every other pair left to the compiler-vectorised loops.
// They compute the same integers as the plain loops (checked against the scalar backend).
// Elsewhere (arm64, MSVC for x86) this file only defines pickers that return nullptr.

#if defined(__SSE2__)
#include <emmintrin.h>

namespace stb {
namespace detail {
namespace {

// Four 4-channel pixels: one dword each.
inline __m128i load4_rgba(const std::uint8_t* s) { return _mm_loadu_si128(reinterpret_cast<const __m128i*>(s)); }

// Four 3-channel pixels, one dword each (the top byte is the next pixel's: ignore it). Reads 16
// bytes from s, so the caller keeps 6 pixels in range.
inline __m128i load4_rgb(const std::uint8_t* s) {
    const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
    const __m128i t01 = _mm_unpacklo_epi32(v, _mm_srli_si128(v, 3));
    const __m128i t23 = _mm_unpacklo_epi32(_mm_srli_si128(v, 6), _mm_srli_si128(v, 9));
    return _mm_unpacklo_epi64(t01, t23);
}

// (R*19595 + G*38470 + B*7471 + 0x8000) >> 16 for four pixels. 38470 does not fit the signed
// 16-bit multiplier of pmaddwd, so G is multiplied by half of it and doubled.
inline __m128i luma4(__m128i p) {
    const __m128i rb = _mm_and_si128(p, _mm_set1_epi32(0x00FF00FF));
    const __m128i g = _mm_and_si128(_mm_srli_epi32(p, 8), _mm_set1_epi32(0xFF));
    const __m128i a = _mm_madd_epi16(rb, _mm_set1_epi32((7471 << 16) | 19595));
    const __m128i b = _mm_slli_epi32(_mm_madd_epi16(g, _mm_set1_epi32(19235)), 1);
    return _mm_srli_epi32(_mm_add_epi32(_mm_add_epi32(a, b), _mm_set1_epi32(0x8000)), 16);
}

template <int SC>
void to_gray_sse2(const std::uint8_t* s, std::uint8_t* d, std::size_t n) {
    constexpr std::size_t margin = SC == 4 ? 4 : 6;  // pixels that must remain for the 16-byte load
    std::size_t i = 0;
    for (; i + margin <= n; i += 4) {
        __m128i y = luma4(SC == 4 ? load4_rgba(s + 4 * i) : load4_rgb(s + 3 * i));
        y = _mm_packus_epi16(_mm_packs_epi32(y, y), y);
        const int four = _mm_cvtsi128_si32(y);
        std::memcpy(d + i, &four, 4);
    }
    convert_row<SC, 1>(s + SC * i, d + i, n - i);
}

// d' = (s*a + d*(255-a) + 127) / 255 on two pixels held as 16-bit lanes (r g b a, r g b a);
// the division is exact: floor(v/255) == (v + 1 + (v >> 8)) >> 8 for v < 65535 (every v here
// is at most 65152), and the sums stay below 2^16, so 16-bit lanes are enough.
inline __m128i blend2(__m128i s, __m128i d) {
    const __m128i a = _mm_shufflehi_epi16(_mm_shufflelo_epi16(s, _MM_SHUFFLE(3, 3, 3, 3)), _MM_SHUFFLE(3, 3, 3, 3));
    const __m128i v = _mm_add_epi16(_mm_add_epi16(_mm_mullo_epi16(s, a), _mm_mullo_epi16(d, _mm_sub_epi16(_mm_set1_epi16(255), a))),
                                    _mm_set1_epi16(127));
    return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(v, _mm_set1_epi16(1)), _mm_srli_epi16(v, 8)), 8);
}

void composite_rgba_over_rgb_sse2(const std::uint8_t* s, std::uint8_t* d, std::size_t n) {
    const __m128i zero = _mm_setzero_si128();
    const __m128i keep24 = _mm_set1_epi32(0x00FFFFFF);
    const __m128i m0 = _mm_set_epi32(0, 0x00FFFFFF, 0, 0x00FFFFFF);          // pixel 0 of each 64-bit half
    const __m128i m1 = _mm_set_epi32(0x0000FFFF, int(0xFF000000), 0x0000FFFF, int(0xFF000000));  // pixel 1, shifted down a byte
    std::size_t i = 0;
    for (; i + 6 <= n; i += 4) {
        const __m128i sp = load4_rgba(s + 4 * i), dp = load4_rgb(d + 3 * i);
        const __m128i lo = blend2(_mm_unpacklo_epi8(sp, zero), _mm_unpacklo_epi8(dp, zero));
        const __m128i hi = blend2(_mm_unpackhi_epi8(sp, zero), _mm_unpackhi_epi8(dp, zero));
        const __m128i q = _mm_and_si128(_mm_packus_epi16(lo, hi), keep24);  // four dwords: r g b 0
        // Squeeze the four 3-byte pixels together into 12 bytes, so nothing is written past them.
        const __m128i w = _mm_or_si128(_mm_and_si128(q, m0), _mm_and_si128(_mm_srli_epi64(q, 8), m1));
        const __m128i low6 = _mm_srli_si128(_mm_slli_si128(w, 10), 10);
        const __m128i high6 = _mm_slli_si128(_mm_srli_si128(w, 8), 6);
        const __m128i full = _mm_or_si128(low6, high6);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(d + 3 * i), full);
        const int tail = _mm_cvtsi128_si32(_mm_srli_si128(full, 8));
        std::memcpy(d + 3 * i + 8, &tail, 4);
    }
    composite_row<4, 3>(s + 4 * i, d + 3 * i, n - i);
}

}  // namespace

row_fn pick_convert_sse2(int sc, int dc) {
    if (dc == 1 && sc == 3) return &to_gray_sse2<3>;
    if (dc == 1 && sc == 4) return &to_gray_sse2<4>;
    return nullptr;
}

row_fn pick_composite_sse2(int sc, int dc) { return sc == 4 && dc == 3 ? &composite_rgba_over_rgb_sse2 : nullptr; }

}  // namespace detail
}  // namespace stb

#else

namespace stb {
namespace detail {
row_fn pick_convert_sse2(int, int) { return nullptr; }
row_fn pick_composite_sse2(int, int) { return nullptr; }
}  // namespace detail
}  // namespace stb

#endif
