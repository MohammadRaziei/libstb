#pragma once

// Internal: which kernel set to run. See stb::simd_backends() / set_simd() in image.hpp.

// Compile with -DSTB_NO_SIMD_DISPATCH to leave the AVX2 kernels out (they are only built for
// x86-64 with GCC or Clang anyway; arm64 and MSVC builds always use the portable ones).
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__) && !defined(STB_NO_SIMD_DISPATCH)
#define STB_HAVE_AVX2_DISPATCH 1
#define STB_TARGET_AVX2 __attribute__((target("avx2")))
#endif

namespace stb {
namespace detail {

enum class backend {
    avx2,    // the kernels built with the AVX2 target attribute (run-time dispatch)
    vector,  // the same loops, vectorised by the compiler for the platform baseline (SSE2 / NEON)
    scalar,  // the same loops with auto-vectorisation off
};

backend current_backend() noexcept;

}  // namespace detail
}  // namespace stb
