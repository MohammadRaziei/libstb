#include <cstddef>

#include "image/math_kernels.hpp"
#include "stb/image.hpp"
#include "util/parallel.hpp"

// The instruction-set builds of the numeric kernels (math_kernels.hpp), the choice between them, and
// the conversions between 8 bits and floats. Built with -ffp-contract=off (see CMakeLists.txt).

namespace stb {
namespace detail {
namespace {

// ---- the platform baseline: the compiler vectorises the loops for SSE2 / NEON
void u8_to_f32_vector(const std::uint8_t* s, float* d, std::size_t n) { u8_to_f32_row(s, d, n); }
void f32_to_u8_vector(const float* s, std::uint8_t* d, std::size_t n) { f32_to_u8_row(s, d, n); }
void fir_h_vector(const float* p, float* d, std::size_t n, const float* t, int nt, std::size_t stride) {
    fir_h_row(p, d, n, t, nt, stride);
}
void fir_v_vector(const float* const* rows, float* d, std::size_t n, const float* t, int nt) {
    fir_v_row(rows, d, n, t, nt);
}
void add_vector(const float* a, float* acc, std::size_t n) { add_row(a, acc, n); }
void magnitude_vector(const float* dx, const float* dy, float* out, std::size_t n) { magnitude_row(dx, dy, out, n); }

const math_kernels& vector_kernels() noexcept {
    static const math_kernels k = {&u8_to_f32_vector, &f32_to_u8_vector, &fir_h_vector,
                                   &fir_v_vector,     &add_vector,       &magnitude_vector};
    return k;
}

#ifdef STB_HAVE_AVX2_DISPATCH
// ---- the same loops with the AVX2 target attribute (chosen at run time if the CPU has it)
STB_TARGET_AVX2 void u8_to_f32_avx2(const std::uint8_t* s, float* d, std::size_t n) { u8_to_f32_row(s, d, n); }
STB_TARGET_AVX2 void f32_to_u8_avx2(const float* s, std::uint8_t* d, std::size_t n) { f32_to_u8_row(s, d, n); }
STB_TARGET_AVX2 void fir_h_avx2(const float* p, float* d, std::size_t n, const float* t, int nt, std::size_t stride) {
    fir_h_row(p, d, n, t, nt, stride);
}
STB_TARGET_AVX2 void fir_v_avx2(const float* const* rows, float* d, std::size_t n, const float* t, int nt) {
    fir_v_row(rows, d, n, t, nt);
}
STB_TARGET_AVX2 void add_avx2(const float* a, float* acc, std::size_t n) { add_row(a, acc, n); }
STB_TARGET_AVX2 void magnitude_avx2(const float* dx, const float* dy, float* out, std::size_t n) {
    magnitude_row(dx, dy, out, n);
}

const math_kernels& avx2_kernels() noexcept {
    static const math_kernels k = {&u8_to_f32_avx2, &f32_to_u8_avx2, &fir_h_avx2,
                                   &fir_v_avx2,     &add_avx2,       &magnitude_avx2};
    return k;
}
#endif

// Floors of what one core does per value (ns), so that a doubtful case stays on one thread, see util/parallel.hpp.
constexpr double convert_ns = 0.15;

}  // namespace

const math_kernels& math() noexcept {
    const backend be = current_backend();
    if (be == backend::scalar) return math_scalar_kernels();
#ifdef STB_HAVE_AVX2_DISPATCH
    if (be == backend::avx2) return avx2_kernels();
#endif
    return vector_kernels();
}

}  // namespace detail

mat32f to_f32(const mat8u& src) {
    if (src.empty()) return mat32f();
    mat32f out(src.width(), src.height(), src.channels());
    const std::uint8_t* s = src.data();
    float* d = out.data();
    const detail::math_kernels& k = detail::math();  // the calling thread's backend, for the workers too
    detail::parallel_for(src.size(), detail::convert_ns,
                         [&](std::size_t b, std::size_t e) { k.u8_to_f32(s + b, d + b, e - b); });
    return out;
}

image8u to_8u(const mat32f& src) {
    if (src.empty()) return image8u();
    image8u out(src.width(), src.height(), pixel_format_from_channels(src.channels()));
    const float* s = src.data();
    std::uint8_t* d = out.data();
    const detail::math_kernels& k = detail::math();
    detail::parallel_for(src.size(), detail::convert_ns,
                         [&](std::size_t b, std::size_t e) { k.f32_to_u8(s + b, d + b, e - b); });
    return out;
}

}  // namespace stb
