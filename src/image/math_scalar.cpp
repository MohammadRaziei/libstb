#include "image/math_kernels.hpp"

// The numeric kernels compiled with the compiler's auto-vectorisation switched off (CMake gives this
// file -fno-tree-vectorize / -fno-vectorize, like kernels_scalar.cpp): the plain one-value-at-a-time
// reference, and the "scalar" backend. The tests compare every other backend with it.

namespace stb::detail {
namespace {

void u8_to_f32_scalar(const std::uint8_t* s, float* d, std::size_t n) { u8_to_f32_row(s, d, n); }
void f32_to_u8_scalar(const float* s, std::uint8_t* d, std::size_t n) { f32_to_u8_row(s, d, n); }
void fir_h_scalar(const float* p, float* d, std::size_t n, const float* t, int nt, std::size_t stride) {
    fir_h_row(p, d, n, t, nt, stride);
}
void fir_v_scalar(const float* const* rows, float* d, std::size_t n, const float* t, int nt) {
    fir_v_row(rows, d, n, t, nt);
}
void add_scalar(const float* a, float* acc, std::size_t n) { add_row(a, acc, n); }
void magnitude_scalar(const float* dx, const float* dy, float* out, std::size_t n) { magnitude_row(dx, dy, out, n); }

}  // namespace

const math_kernels& math_scalar_kernels() noexcept {
    static const math_kernels k = {&u8_to_f32_scalar, &f32_to_u8_scalar, &fir_h_scalar,
                                   &fir_v_scalar,     &add_scalar,       &magnitude_scalar};
    return k;
}

}  // namespace stb::detail
