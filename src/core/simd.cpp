#include "simd.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
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

// LIBSTB_SIMD=<backend name> | auto | off (off, 0 and false mean "scalar"). An unknown or
// unavailable name falls back to auto, with one line on stderr so a typo does not go unnoticed.
std::size_t initial() {
    const char* e = std::getenv("LIBSTB_SIMD");
    if (!e || !*e) return 0;
    const std::string_view want = e;
    if (want == "auto") return 0;
    if (want == "off" || want == "0" || want == "false") return entries().size() - 1;
    for (std::size_t i = 0; i < entries().size(); ++i)
        if (want == entries()[i].name) return i;
    std::string have;
    for (const entry& en : entries()) have += (have.empty() ? "" : ", ") + std::string(en.name);
    std::fprintf(stderr, "libstb: LIBSTB_SIMD=%s is not an available SIMD backend (available: %s); using %s\n", e,
                 have.c_str(), entries().front().name);
    return 0;
}

std::atomic<std::size_t>& current() {
    static std::atomic<std::size_t> c{initial()};
    return c;
}

// Per-thread override (simd_scope): an index into entries(), or npos for "follow the global".
constexpr std::size_t npos = std::size_t(-1);
thread_local std::size_t tl_override = npos;

// The index in use on the calling thread.
std::size_t effective() noexcept {
    return tl_override != npos ? tl_override : current().load(std::memory_order_relaxed);
}

// Position of `name` in entries() ("auto" is 0, the best); npos if there is no such backend.
std::size_t find(std::string_view name) {
    if (name == "auto") return 0;
    for (std::size_t i = 0; i < entries().size(); ++i)
        if (name == entries()[i].name) return i;
    return npos;
}

}  // namespace

namespace detail {
backend current_backend() noexcept { return entries()[effective()].be; }
}  // namespace detail

std::vector<std::string> simd_backends() {
    std::vector<std::string> out;
    for (const entry& e : entries()) out.emplace_back(e.name);
    return out;
}

const char* simd_name() noexcept { return entries()[effective()].name; }

bool set_simd(std::string_view name) noexcept {
    const std::size_t i = find(name);
    if (i == npos) return false;
    current().store(i, std::memory_order_relaxed);
    return true;
}

simd_scope::simd_scope(std::string_view name) {
    const std::size_t i = find(name);
    if (i == npos) {
        std::string have;
        for (const entry& en : entries()) have += (have.empty() ? "'" : ", '") + std::string(en.name) + "'";
        throw std::invalid_argument("unknown or unavailable SIMD backend '" + std::string(name) +
                                    "' (available: " + have + ", or 'auto')");
    }
    previous_ = tl_override;
    tl_override = i;
}

simd_scope::~simd_scope() { tl_override = previous_; }

}  // namespace stb
