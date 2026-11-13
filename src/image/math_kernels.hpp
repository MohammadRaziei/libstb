#pragma once

// Internal: the numeric row kernels every filter is built from. A filter never loops over pixels
// itself: it asks for a row of floats, calls these, and stores the row. That keeps the SIMD in one
// place (here), the threading in another (util/parallel.hpp, used by the callers) and the algorithms
// in terms of both.
//
// Like kernels.hpp, each kernel is written once as an always-inline function; math_ops.cpp
// instantiates it as plain code (compiler-vectorised for the platform baseline) and with the AVX2
// target attribute, math_scalar.cpp with auto-vectorisation off. The three tables below are the three
// backends of stb::set_simd().
//
// Every kernel computes each output value from its inputs in one fixed order, with no horizontal
// reduction across lanes and no fused multiply-add (the files are built with -ffp-contract=off), so
// all backends return the same bits, and so does any split of the work between threads.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "image/kernels.hpp"  // STB_INLINE
#include "image/simd.hpp"

namespace stb {
namespace detail {

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#define STB_RESTRICT __restrict
#else
#define STB_RESTRICT
#endif

// Outputs per block of the FIR kernels: 32 floats are 4 AVX2 registers (or 8 SSE ones) of running
// sums that stay in registers while the taps stream past.
constexpr std::size_t fir_block = 32;

// value / 255. A real division (not a multiply by 1/255) so that 255 lands on exactly 1.0f.
STB_INLINE void u8_to_f32_row(const std::uint8_t* STB_RESTRICT s, float* STB_RESTRICT d, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) d[i] = float(s[i]) / 255.0f;
}

// round(clamp(v, 0, 1) * 255). The first comparison is false for NaN, which therefore becomes 0 (a cast of
// NaN to an integer is undefined).
STB_INLINE void f32_to_u8_row(const float* STB_RESTRICT s, std::uint8_t* STB_RESTRICT d, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        float v = s[i];
        v = v > 0.0f ? v : 0.0f;
        v = v < 1.0f ? v : 1.0f;
        d[i] = static_cast<std::uint8_t>(v * 255.0f + 0.5f);
    }
}

// d[i] = sum over k of taps[k] * p[i + k * stride], k ascending: a 1-D filter along a row of `stride`
// interleaved channels (stride = channel count, so the neighbour of a value in the same channel is
// `stride` values away). `p` is the row padded by (nt / 2) pixels on each side, so p has n + (nt - 1) *
// stride values. Value i of the output needs no knowledge of its neighbours: it vectorises over i.
STB_INLINE void fir_h_row(const float* STB_RESTRICT p, float* STB_RESTRICT d, std::size_t n,
                          const float* STB_RESTRICT taps, int nt, std::size_t stride) {
    std::size_t i = 0;
    for (; i + fir_block <= n; i += fir_block) {
        float acc[fir_block] = {};
        for (int k = 0; k < nt; ++k) {
            const float t = taps[k];
            const float* s = p + i + std::size_t(k) * stride;
            for (std::size_t j = 0; j < fir_block; ++j) acc[j] += t * s[j];
        }
        for (std::size_t j = 0; j < fir_block; ++j) d[i + j] = acc[j];
    }
    for (; i < n; ++i) {  // the tail runs the same additions in the same order
        float acc = 0.0f;
        for (int k = 0; k < nt; ++k) acc += taps[k] * p[i + std::size_t(k) * stride];
        d[i] = acc;
    }
}

// d[i] = sum over k of taps[k] * rows[k][i], k ascending: the same filter across nt rows.
STB_INLINE void fir_v_row(const float* const* STB_RESTRICT rows, float* STB_RESTRICT d, std::size_t n,
                          const float* STB_RESTRICT taps, int nt) {
    std::size_t i = 0;
    for (; i + fir_block <= n; i += fir_block) {
        float acc[fir_block] = {};
        for (int k = 0; k < nt; ++k) {
            const float t = taps[k];
            const float* s = rows[k] + i;
            for (std::size_t j = 0; j < fir_block; ++j) acc[j] += t * s[j];
        }
        for (std::size_t j = 0; j < fir_block; ++j) d[i + j] = acc[j];
    }
    for (; i < n; ++i) {
        float acc = 0.0f;
        for (int k = 0; k < nt; ++k) acc += taps[k] * rows[k][i];
        d[i] = acc;
    }
}

// acc[i] += a[i]
STB_INLINE void add_row(const float* STB_RESTRICT a, float* STB_RESTRICT acc, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) acc[i] += a[i];
}

// sqrt(dx * dx + dy * dy): the square root is correctly rounded in every backend.
STB_INLINE void magnitude_row(const float* STB_RESTRICT dx, const float* STB_RESTRICT dy, float* STB_RESTRICT out,
                              std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) out[i] = std::sqrt(dx[i] * dx[i] + dy[i] * dy[i]);
}

// One backend: the same kernels, built for one instruction set.
struct math_kernels {
    void (*u8_to_f32)(const std::uint8_t*, float*, std::size_t);
    void (*f32_to_u8)(const float*, std::uint8_t*, std::size_t);
    void (*fir_h)(const float*, float*, std::size_t, const float*, int, std::size_t);
    void (*fir_v)(const float* const*, float*, std::size_t, const float*, int);
    void (*add)(const float*, float*, std::size_t);
    void (*magnitude)(const float*, const float*, float*, std::size_t);
};

// The tables. math() is the one for the backend the calling thread uses (stb::simd_name()).
const math_kernels& math_scalar_kernels() noexcept;  // math_scalar.cpp
const math_kernels& math() noexcept;                 // math_ops.cpp

}  // namespace detail
}  // namespace stb
