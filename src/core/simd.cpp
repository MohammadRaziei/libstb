#include "simd.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

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

// The instruction set the compiler targets for everything that is not an AVX2 kernel: what
// the portable loops are vectorised with. x86-64 always has SSE2 and arm64 always has NEON;
// "scalar" is any other CPU. (A build made with -march=native / -mavx2 reports what it targets.)
const char* portable_isa() noexcept {
#if defined(__AVX512F__)
    return "avx512";
#elif defined(__AVX2__)
    return "avx2";
#elif defined(__AVX__)
    return "avx";
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    return "sse2";
#elif defined(__ARM_NEON) || defined(__aarch64__) || defined(_M_ARM64)
    return "neon";
#else
    return "scalar";
#endif
}

struct entry {
    const char* name;
    detail::backend be;
};

// The backends this build can run on this CPU, best first. Built once.
const std::vector<entry>& entries() {
    static const std::vector<entry> e = [] {
        std::vector<entry> v;
        const std::string p = portable_isa();
        const bool wide = p == "avx2" || p == "avx512" || p == "avx";  // the whole build targets it
#ifdef STB_HAVE_AVX2_DISPATCH
        if (!wide && cpu_has_avx2()) v.push_back({"avx2", detail::backend::avx2});
#endif
        (void)wide;
        if (p != "scalar") v.push_back({portable_isa(), detail::backend::vector});
        v.push_back({"scalar", detail::backend::scalar});
        return v;
    }();
    return e;
}

// LIBSTB_SIMD=<backend name> | auto | off (off, 0 and false mean "scalar"); anything else: auto.
std::size_t initial() {
    const char* e = std::getenv("LIBSTB_SIMD");
    if (!e || !*e) return 0;
    const std::string_view want = e;
    if (want == "off" || want == "0" || want == "false") return entries().size() - 1;
    for (std::size_t i = 0; i < entries().size(); ++i)
        if (want == entries()[i].name) return i;
    return 0;
}

std::atomic<std::size_t>& current() {
    static std::atomic<std::size_t> c{initial()};
    return c;
}

}  // namespace

namespace detail {
backend current_backend() noexcept { return entries()[current().load(std::memory_order_relaxed)].be; }
}  // namespace detail

std::vector<std::string> simd_backends() {
    std::vector<std::string> out;
    for (const entry& e : entries()) out.emplace_back(e.name);
    return out;
}

const char* simd_name() noexcept { return entries()[current().load(std::memory_order_relaxed)].name; }

bool set_simd(std::string_view name) noexcept {
    if (name == "auto") {
        current().store(0, std::memory_order_relaxed);
        return true;
    }
    for (std::size_t i = 0; i < entries().size(); ++i)
        if (name == entries()[i].name) {
            current().store(i, std::memory_order_relaxed);
            return true;
        }
    return false;
}

}  // namespace stb
