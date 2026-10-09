#include "simd.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "stb/image.hpp"

namespace stb {
namespace {

bool cpu_has_avx2() noexcept {
#ifdef STB_HAVE_AVX2_DISPATCH
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");  // also checks that the OS saves the AVX registers
#else
    return false;
#endif
}

bool initial() noexcept {
    const char* e = std::getenv("LIBSTB_SIMD");
    const bool off = e && (!std::strcmp(e, "off") || !std::strcmp(e, "0") || !std::strcmp(e, "false"));
    return cpu_has_avx2() && !off;
}

std::atomic<bool>& flag() noexcept {
    static std::atomic<bool> f{initial()};
    return f;
}

bool cpu_supported() noexcept {
    static const bool v = cpu_has_avx2();
    return v;
}

}  // namespace

namespace detail {
bool use_avx2() noexcept { return flag().load(std::memory_order_relaxed); }
}  // namespace detail

const char* simd_name() noexcept { return detail::use_avx2() ? "avx2" : "baseline"; }

void set_simd_enabled(bool enabled) noexcept {
    flag().store(enabled && cpu_supported(), std::memory_order_relaxed);
}

}  // namespace stb
