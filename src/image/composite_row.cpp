#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "image/simd.hpp"
#include "stb/image.hpp"

// SSE2 is the baseline of every x86-64 (MSVC does not define __SSE2__ there)
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#define STB_COMPOSITE_SSE2 1
#endif

// composite_row / composite_solid_row: Porter-Duff operators and blend modes on rows of premultiplied
// 0xAARRGGBB pixels, 8 bits per channel, all four channels at once in two 32-bit lanes. Premultiplied
// pixels are what rasterizers draw into, so this is the part of compositing that needs no conversion;
// image::blend does the same on straight alpha in floating point.

namespace stb {
namespace {

using u32 = std::uint32_t;

constexpr u32 alpha_of(u32 p) { return p >> 24; }

// x * a / 255 on every byte of x, rounded (a in 0..255; exact for a == 255 and a == 0).
inline u32 byte_mul(u32 x, u32 a) {
    u32 t = (x & 0xff00ffu) * a;
    t = (t + ((t >> 8) & 0xff00ffu) + 0x800080u) >> 8;
    t &= 0xff00ffu;
    x = ((x >> 8) & 0xff00ffu) * a;
    x = x + ((x >> 8) & 0xff00ffu) + 0x800080u;
    x &= 0xff00ff00u;
    return x | t;
}

// (x * a + y * b) / 255 on every byte, rounded, for a + b <= 255 (lane sums stay below 2^16 for valid pixels).
inline u32 mix255(u32 x, u32 a, u32 y, u32 b) {
    u32 t = (x & 0xff00ffu) * a + (y & 0xff00ffu) * b;
    t = (t + ((t >> 8) & 0xff00ffu) + 0x800080u) >> 8;
    t &= 0xff00ffu;
    x = ((x >> 8) & 0xff00ffu) * a + ((y >> 8) & 0xff00ffu) * b;
    x = x + ((x >> 8) & 0xff00ffu) + 0x800080u;
    x &= 0xff00ff00u;
    return x | t;
}

constexpr int mul255(int x, int y) { return (x * y + 127) / 255; }

// ---------------------------------------------------------------- Porter-Duff operators

// The new destination pixel: operator Op of s over d, ca the coverage (0..255): d + ca * (result - d). The
// operators that are linear in the source (over, over, atop, xor) get the same number by scaling the source
// by ca first, which is cheaper; the others take ca as the share of the result that replaces d. With
// ca == 255 every line reduces to the plain operator.
template <composite_op Op, bool Solid = false>
inline u32 apply(u32 d, u32 s, u32 ca) {
    [[maybe_unused]] const u32 ia = 255 - ca;
    if constexpr (Op == composite_op::clear) {
        return byte_mul(d, ia);
    } else if constexpr (Op == composite_op::source) {
        // one colour (Solid): scaled once and the destination apart from it, as rasterizers always did; a row
        // takes the single rounding of mix255. The two can differ by one level.
        if constexpr (Solid) return ca == 255 ? s : byte_mul(s, ca) + byte_mul(d, ia);
        else return mix255(s, ca, d, ia);
    } else if constexpr (Op == composite_op::destination) {
        return d;
    } else if constexpr (Op == composite_op::source_over) {
        if (ca == 255) {
            if (s >= 0xff000000u) return s;  // opaque: nothing of d shows
        } else {
            s = byte_mul(s, ca);
        }
        return s + byte_mul(d, 255 - alpha_of(s));
    } else if constexpr (Op == composite_op::destination_over) {
        s = byte_mul(s, ca);
        return d + byte_mul(s, 255 - alpha_of(d));
    } else if constexpr (Op == composite_op::source_in) {
        return mix255(byte_mul(s, ca), alpha_of(d), d, ia);
    } else if constexpr (Op == composite_op::destination_in) {
        return byte_mul(d, byte_mul(alpha_of(s), ca) + ia);
    } else if constexpr (Op == composite_op::source_out) {
        return mix255(byte_mul(s, ca), 255 - alpha_of(d), d, ia);
    } else if constexpr (Op == composite_op::destination_out) {
        return byte_mul(d, byte_mul(255 - alpha_of(s), ca) + ia);
    } else if constexpr (Op == composite_op::source_atop) {
        s = byte_mul(s, ca);
        return mix255(s, alpha_of(d), d, 255 - alpha_of(s));
    } else if constexpr (Op == composite_op::destination_atop) {
        s = byte_mul(s, ca);
        return mix255(d, alpha_of(s) + ia, s, 255 - alpha_of(d));
    } else if constexpr (Op == composite_op::exclusive_or) {
        s = byte_mul(s, ca);
        return mix255(s, 255 - alpha_of(d), d, 255 - alpha_of(s));
    } else {  // plus_lighter: every channel (alpha too) is added and stops at 255; ca is the share of that shown
        u32 r = 0;
        for (int shift = 0; shift < 32; shift += 8)
            r |= STB_MIN(u32(255), ((s >> shift) & 255) + ((d >> shift) & 255)) << shift;
        return ca == 255 ? r : mix255(r, ca, d, ia);
    }
}

u32 apply_any(composite_op op, u32 d, u32 s, u32 ca) {
    switch (op) {
        case composite_op::clear: return apply<composite_op::clear>(d, s, ca);
        case composite_op::source: return apply<composite_op::source>(d, s, ca);
        case composite_op::destination: return d;
        case composite_op::source_over: return apply<composite_op::source_over>(d, s, ca);
        case composite_op::destination_over: return apply<composite_op::destination_over>(d, s, ca);
        case composite_op::source_in: return apply<composite_op::source_in>(d, s, ca);
        case composite_op::destination_in: return apply<composite_op::destination_in>(d, s, ca);
        case composite_op::source_out: return apply<composite_op::source_out>(d, s, ca);
        case composite_op::destination_out: return apply<composite_op::destination_out>(d, s, ca);
        case composite_op::source_atop: return apply<composite_op::source_atop>(d, s, ca);
        case composite_op::destination_atop: return apply<composite_op::destination_atop>(d, s, ca);
        case composite_op::exclusive_or: return apply<composite_op::exclusive_or>(d, s, ca);
        case composite_op::plus_lighter: return apply<composite_op::plus_lighter>(d, s, ca);
    }
    return d;
}

// ---------------------------------------------------------------- blend modes

// The W3C formula on premultiplied channels (cs the source's, cb the backdrop's; as, ab the alphas):
//   Cs (1 - ab) + Cb (1 - as) + as ab B(Cb / ab, Cs / as)
// which for each separable mode B has the integer form below (the same numbers the unpremultiplied
// float path of image::blend gives, to within a level).
template <blend_mode M>
inline int mode_value(int cs, int cb, int as, int ab) {
    if constexpr (M == blend_mode::multiply) {
        return mul255(cs, 255 - ab) + mul255(cb, 255 - as) + mul255(cs, cb);
    } else if constexpr (M == blend_mode::screen) {
        return cs + cb - mul255(cs, cb);
    } else if constexpr (M == blend_mode::darken) {
        return cs + cb - STB_MAX(mul255(cs, ab), mul255(cb, as));
    } else if constexpr (M == blend_mode::lighten) {
        return cs + cb - STB_MIN(mul255(cs, ab), mul255(cb, as));
    } else if constexpr (M == blend_mode::difference) {
        return cs + cb - 2 * STB_MIN(mul255(cs, ab), mul255(cb, as));
    } else if constexpr (M == blend_mode::exclusion) {
        return cs + cb - 2 * mul255(cs, cb);
    } else if constexpr (M == blend_mode::add) {
        return mul255(cs, 255 - ab) + mul255(cb, 255 - as) + STB_MIN(mul255(as, ab), mul255(cb, as) + mul255(cs, ab));
    } else {  // overlay: multiply where the backdrop is dark, screen where it is light
        const int base = mul255(cs, 255 - ab) + mul255(cb, 255 - as);
        if (2 * cb <= ab) return base + 2 * mul255(cs, cb);
        const int k = 2 * cb - ab;
        return base + mul255(cs, ab) + mul255(as, k) - mul255(cs, k);
    }
}

// S over D with mode M (what the destination keeps is included): the result of operator source_over.
template <blend_mode M>
inline u32 mode_over(u32 d, u32 s) {
    if (s == 0) return d;
    const int as = int(alpha_of(s)), ab = int(alpha_of(d));
    const int ar = as + ab - mul255(as, ab);
    u32 r = u32(ar) << 24;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const int c = mode_value<M>(int((s >> shift) & 255), int((d >> shift) & 255), as, ab);
        r |= u32(std::clamp(c, 0, ar)) << shift;  // a premultiplied channel never exceeds the alpha
    }
    return r;
}

// The source with the backdrop mixed into it (what the other operators take in place of S): the part of
// mode_over's result that is not the backdrop's own Cb (1 - as).
template <blend_mode M>
inline u32 mode_source(u32 d, u32 s) {
    if (s == 0) return 0;
    const int as = int(alpha_of(s)), ab = int(alpha_of(d));
    u32 r = u32(as) << 24;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const int cb = int((d >> shift) & 255);
        const int c = mode_value<M>(int((s >> shift) & 255), cb, as, ab) - mul255(cb, 255 - as);
        r |= u32(std::clamp(c, 0, as)) << shift;
    }
    return r;
}

u32 mode_source_any(blend_mode m, u32 d, u32 s) {
    switch (m) {
        case blend_mode::normal: return s;
        case blend_mode::multiply: return mode_source<blend_mode::multiply>(d, s);
        case blend_mode::screen: return mode_source<blend_mode::screen>(d, s);
        case blend_mode::overlay: return mode_source<blend_mode::overlay>(d, s);
        case blend_mode::darken: return mode_source<blend_mode::darken>(d, s);
        case blend_mode::lighten: return mode_source<blend_mode::lighten>(d, s);
        case blend_mode::add: return mode_source<blend_mode::add>(d, s);
        case blend_mode::difference: return mode_source<blend_mode::difference>(d, s);
        case blend_mode::exclusion: return mode_source<blend_mode::exclusion>(d, s);
    }
    return s;
}

// ---------------------------------------------------------------- the loops

// f(d, s) is the new destination pixel; the source is `color` for every pixel or the row `src`.
template <bool Solid, class F>
inline void loop(u32* dest, const u32* src, u32 color, std::size_t n, F f) {
    for (std::size_t i = 0; i < n; ++i) dest[i] = f(dest[i], Solid ? color : src[i]);
}

#if defined(STB_COMPOSITE_SSE2)
namespace sse2 {
using vec = __m128i;
constexpr std::size_t lanes = 4;
#define STB_K_ATTR inline
STB_K_ATTR vec vzero() { return _mm_setzero_si128(); }
STB_K_ATTR vec vset16(int x) { return _mm_set1_epi16(std::int16_t(x)); }
STB_K_ATTR vec vadd(vec a, vec b) { return _mm_add_epi16(a, b); }
STB_K_ATTR vec vsub(vec a, vec b) { return _mm_sub_epi16(a, b); }
STB_K_ATTR vec vmullo(vec a, vec b) { return _mm_mullo_epi16(a, b); }
STB_K_ATTR vec vsrl8(vec a) { return _mm_srli_epi16(a, 8); }
STB_K_ATTR vec vmin(vec a, vec b) { return _mm_min_epi16(a, b); }
STB_K_ATTR vec vunpacklo(vec a, vec z) { return _mm_unpacklo_epi8(a, z); }
STB_K_ATTR vec vunpackhi(vec a, vec z) { return _mm_unpackhi_epi8(a, z); }
STB_K_ATTR vec vpack(vec lo, vec hi) { return _mm_packus_epi16(lo, hi); }
STB_K_ATTR vec valpha(vec x) { return _mm_shufflehi_epi16(_mm_shufflelo_epi16(x, 0xFF), 0xFF); }  // lane 3 of each pixel
#include "image/composite_kernels.inl"
#undef STB_K_ATTR

template <bool Solid, composite_op Op, bool Full>
void run(u32* dest, const u32* src, u32 color, std::size_t n, u32 ca) {
    const vec CA = vset16(int(ca)), IA = vset16(int(255 - ca)), one = _mm_set1_epi32(int(color));
    std::size_t i = 0;
    for (; i + lanes <= n; i += lanes) {
        const vec d = _mm_loadu_si128(reinterpret_cast<const vec*>(dest + i));
        const vec s = Solid ? one : _mm_loadu_si128(reinterpret_cast<const vec*>(src + i));
        _mm_storeu_si128(reinterpret_cast<vec*>(dest + i), compute<Op, Full, Solid>(d, s, CA, IA));
    }
    for (; i < n; ++i) dest[i] = apply<Op, Solid>(dest[i], Solid ? color : src[i], ca);
}
}  // namespace sse2
#endif

#if defined(STB_HAVE_AVX2_DISPATCH)
#include <immintrin.h>
namespace avx2 {
using vec = __m256i;
constexpr std::size_t lanes = 8;
#define STB_K_ATTR STB_TARGET_AVX2 inline
STB_K_ATTR vec vzero() { return _mm256_setzero_si256(); }
STB_K_ATTR vec vset16(int x) { return _mm256_set1_epi16(std::int16_t(x)); }
STB_K_ATTR vec vadd(vec a, vec b) { return _mm256_add_epi16(a, b); }
STB_K_ATTR vec vsub(vec a, vec b) { return _mm256_sub_epi16(a, b); }
STB_K_ATTR vec vmullo(vec a, vec b) { return _mm256_mullo_epi16(a, b); }
STB_K_ATTR vec vsrl8(vec a) { return _mm256_srli_epi16(a, 8); }
STB_K_ATTR vec vmin(vec a, vec b) { return _mm256_min_epi16(a, b); }
STB_K_ATTR vec vunpacklo(vec a, vec z) { return _mm256_unpacklo_epi8(a, z); }  // within each 128-bit half, undone by vpack
STB_K_ATTR vec vunpackhi(vec a, vec z) { return _mm256_unpackhi_epi8(a, z); }
STB_K_ATTR vec vpack(vec lo, vec hi) { return _mm256_packus_epi16(lo, hi); }
STB_K_ATTR vec valpha(vec x) { return _mm256_shufflehi_epi16(_mm256_shufflelo_epi16(x, 0xFF), 0xFF); }
#include "image/composite_kernels.inl"
#undef STB_K_ATTR

// Eight pixels at a time; the last 1..7 go through one masked load and store, so no scalar tail.
template <bool Solid, composite_op Op, bool Full>
STB_TARGET_AVX2 void run(u32* dest, const u32* src, u32 color, std::size_t n, u32 ca) {
    const vec CA = vset16(int(ca)), IA = vset16(int(255 - ca)), one = _mm256_set1_epi32(int(color));
    std::size_t i = 0;
    for (; i + lanes <= n; i += lanes) {
        const vec d = _mm256_loadu_si256(reinterpret_cast<const vec*>(dest + i));
        const vec s = Solid ? one : _mm256_loadu_si256(reinterpret_cast<const vec*>(src + i));
        _mm256_storeu_si256(reinterpret_cast<vec*>(dest + i), compute<Op, Full, Solid>(d, s, CA, IA));
    }
    if (i < n) {
        const vec mask = _mm256_cmpgt_epi32(_mm256_set1_epi32(int(n - i)), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
        const vec d = _mm256_maskload_epi32(reinterpret_cast<const int*>(dest + i), mask);
        const vec s = Solid ? one : _mm256_maskload_epi32(reinterpret_cast<const int*>(src + i), mask);
        _mm256_maskstore_epi32(reinterpret_cast<int*>(dest + i), mask, compute<Op, Full, Solid>(d, s, CA, IA));
    }
}
}  // namespace avx2
#endif

template <bool Solid, composite_op Op>
void loop_op(u32* dest, const u32* src, u32 color, std::size_t n, u32 ca) {
    if (ca == 255 && (Op == composite_op::source || Op == composite_op::clear)) {
        // the whole row is replaced: no arithmetic at all
        if (Op == composite_op::clear) std::memset(dest, 0, n * sizeof(u32));
        else if (Solid) std::fill_n(dest, n, color);
        else std::memmove(dest, src, n * sizeof(u32));
        return;
    }
#if defined(STB_COMPOSITE_SSE2)
    if (n >= 4) {
        const detail::backend be = detail::current_backend();
#if defined(STB_HAVE_AVX2_DISPATCH)
        if (be == detail::backend::avx2 && n >= 8) {
            if (ca == 255) avx2::run<Solid, Op, true>(dest, src, color, n, ca);
            else avx2::run<Solid, Op, false>(dest, src, color, n, ca);
            return;
        }
#endif
        if (be != detail::backend::scalar) {
            if (ca == 255) sse2::run<Solid, Op, true>(dest, src, color, n, ca);
            else sse2::run<Solid, Op, false>(dest, src, color, n, ca);
            return;
        }
    }
#endif
    loop<Solid>(dest, src, color, n, [ca](u32 d, u32 s) { return apply<Op, Solid>(d, s, ca); });
}

template <bool Solid, blend_mode M>
void loop_mode(u32* dest, const u32* src, u32 color, std::size_t n, u32 ca) {
    // source_over: scaling the source by the coverage is the same as taking that share of the result
    loop<Solid>(dest, src, color, n, [ca](u32 d, u32 s) { return mode_over<M>(d, ca == 255 ? s : byte_mul(s, ca)); });
}

template <bool Solid>
void run(u32* dest, const u32* src, u32 color, std::size_t n, composite_op op, blend_mode mode, u32 ca) {
    if (op == composite_op::destination) return;  // nothing changes, whatever the mode
    if (op == composite_op::clear) mode = blend_mode::normal;
    if (mode == blend_mode::normal) {
        switch (op) {
            case composite_op::clear: return loop_op<Solid, composite_op::clear>(dest, src, color, n, ca);
            case composite_op::source: return loop_op<Solid, composite_op::source>(dest, src, color, n, ca);
            case composite_op::destination: return;
            case composite_op::source_over: return loop_op<Solid, composite_op::source_over>(dest, src, color, n, ca);
            case composite_op::destination_over: return loop_op<Solid, composite_op::destination_over>(dest, src, color, n, ca);
            case composite_op::source_in: return loop_op<Solid, composite_op::source_in>(dest, src, color, n, ca);
            case composite_op::destination_in: return loop_op<Solid, composite_op::destination_in>(dest, src, color, n, ca);
            case composite_op::source_out: return loop_op<Solid, composite_op::source_out>(dest, src, color, n, ca);
            case composite_op::destination_out: return loop_op<Solid, composite_op::destination_out>(dest, src, color, n, ca);
            case composite_op::source_atop: return loop_op<Solid, composite_op::source_atop>(dest, src, color, n, ca);
            case composite_op::destination_atop: return loop_op<Solid, composite_op::destination_atop>(dest, src, color, n, ca);
            case composite_op::exclusive_or: return loop_op<Solid, composite_op::exclusive_or>(dest, src, color, n, ca);
            case composite_op::plus_lighter: return loop_op<Solid, composite_op::plus_lighter>(dest, src, color, n, ca);
        }
        return;
    }
    if (op == composite_op::source_over) {
        switch (mode) {
            case blend_mode::normal: return;
            case blend_mode::multiply: return loop_mode<Solid, blend_mode::multiply>(dest, src, color, n, ca);
            case blend_mode::screen: return loop_mode<Solid, blend_mode::screen>(dest, src, color, n, ca);
            case blend_mode::overlay: return loop_mode<Solid, blend_mode::overlay>(dest, src, color, n, ca);
            case blend_mode::darken: return loop_mode<Solid, blend_mode::darken>(dest, src, color, n, ca);
            case blend_mode::lighten: return loop_mode<Solid, blend_mode::lighten>(dest, src, color, n, ca);
            case blend_mode::add: return loop_mode<Solid, blend_mode::add>(dest, src, color, n, ca);
            case blend_mode::difference: return loop_mode<Solid, blend_mode::difference>(dest, src, color, n, ca);
            case blend_mode::exclusion: return loop_mode<Solid, blend_mode::exclusion>(dest, src, color, n, ca);
        }
        return;
    }
    // ponytail: any other operator with a blend mode is one loop that switches per pixel (the source takes
    // the backdrop's colours, then the operator runs at full strength and ca decides how much of that shows).
    // It is the rare combination; give it per-mode loops if a rasterizer ever draws with it in bulk.
    loop<Solid>(dest, src, color, n, [=](u32 d, u32 s) {
        const u32 r = apply_any(op, d, mode_source_any(mode, d, s), 255);
        return ca == 255 ? r : mix255(r, ca, d, 255 - ca);
    });
}

[[noreturn]] void fail(const char* what) { throw std::invalid_argument(what); }

// Cheap enough for a span of one pixel: three compares, the throwing code out of line.
inline void check(const void* dest, std::size_t count, composite_op op, blend_mode mode) {
    if (static_cast<unsigned>(op) > static_cast<unsigned>(composite_op::plus_lighter)) fail("composite_row: unknown composite_op");
    if (static_cast<unsigned>(mode) > static_cast<unsigned>(blend_mode::exclusion)) fail("composite_row: unknown blend_mode");
    if (count > 0 && !dest) fail("composite_row: null destination");
}

}  // namespace

void composite_row(std::uint32_t* dest, const std::uint32_t* src, std::size_t count, composite_op op,
                   blend_mode mode, std::uint8_t const_alpha) {
    check(dest, count, op, mode);
    if (count > 0 && !src) fail("composite_row: null source");
    if (count == 0) return;
    // plain source_over is nearly every fill: straight to its loop, two calls and a switch less (a span is often 1 or 2 pixels)
    if (op == composite_op::source_over && mode == blend_mode::normal)
        return loop_op<false, composite_op::source_over>(dest, src, 0, count, const_alpha);
    run<false>(dest, src, 0, count, op, mode, const_alpha);
}

void composite_solid_row(std::uint32_t* dest, std::uint32_t color, std::size_t count, composite_op op,
                         blend_mode mode, std::uint8_t const_alpha) {
    check(dest, count, op, mode);
    if (count == 0) return;
    if (op == composite_op::source_over && mode == blend_mode::normal)
        return loop_op<true, composite_op::source_over>(dest, nullptr, color, count, const_alpha);
    run<true>(dest, nullptr, color, count, op, mode, const_alpha);
}

}  // namespace stb
