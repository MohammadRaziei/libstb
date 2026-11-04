#pragma once

// Fixed-point arithmetic for the rasterizer and the stroker. The same arithmetic everywhere (64-bit
// integers, no floating point, no platform-dependent rounding), so a path gives the same pixels on
// every platform. The formats:
//   coordinates   26.6 (1 / 64 pixel) between the path and the stroker, 24.8 inside the rasterizer
//   lengths       26.6, or 16.16 when they are factors
//   angles        16.16 degrees: angle_pi is 180 degrees, a full turn is 360 << 16

#include <cstdint>

namespace stb::detail {

using fixed = std::int64_t;

struct fvec {
    fixed x = 0, y = 0;
};

constexpr fixed angle_pi = fixed(180) << 16;
constexpr fixed angle_2pi = angle_pi * 2;
constexpr fixed angle_pi2 = angle_pi / 2;
constexpr fixed angle_pi4 = angle_pi / 4;

constexpr fixed fx_abs(fixed v) noexcept { return v >= 0 ? v : -v; }

fixed mul_fix(fixed a, fixed b) noexcept;            // a * b / 65536, rounded
fixed mul_div(fixed a, fixed b, fixed c) noexcept;   // a * b / c, rounded
fixed div_fix(fixed a, fixed b) noexcept;            // a * 65536 / b, rounded

fixed atan2_fix(fixed dx, fixed dy) noexcept;  // the angle of the vector (dx, dy)
fixed cos_fix(fixed angle) noexcept;
fixed sin_fix(fixed angle) noexcept;
fixed tan_fix(fixed angle) noexcept;
fixed angle_diff(fixed from, fixed to) noexcept;  // to - from, in (-pi, pi]

fvec from_polar(fixed length, fixed angle) noexcept;  // length * (cos, sin)
fvec unit_vector(fixed angle) noexcept;               // (cos, sin) in 16.16
fixed length(fvec v) noexcept;

}  // namespace stb::detail
