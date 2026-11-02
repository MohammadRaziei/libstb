#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1  // sched_getaffinity and CPU_COUNT (glibc)
#endif

#include "stb/threads.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#include "stb/util.hpp"
#include "util/parallel.hpp"

#if defined(__linux__)
#include <sched.h>
#endif

namespace stb {
namespace detail {
namespace {

#if defined(__linux__)
// The CPU limit of the container this process runs in (`docker run --cpus=2`, a Kubernetes CPU
// limit), rounded up to whole CPUs, or 0 when there is none or it cannot be read. cgroup v2 keeps
// it in cpu.max ("max 100000", or "<quota> <period>"), v1 in two files.
int cgroup_cpu_limit() noexcept {
    try {
        long long quota = 0, period = 0;
        std::ifstream v2("/sys/fs/cgroup/cpu.max");
        std::string first;
        if (v2 >> first) {
            if (first == "max" || !(v2 >> period)) return 0;
            quota = std::stoll(first);
        } else {
            std::ifstream q1("/sys/fs/cgroup/cpu/cpu.cfs_quota_us"), p1("/sys/fs/cgroup/cpu/cpu.cfs_period_us");
            if (!(q1 >> quota) || !(p1 >> period)) return 0;
        }
        if (quota <= 0 || period <= 0) return 0;  // v1 writes -1 for "no limit"
        return int(std::min<long long>((quota + period - 1) / period, 1 << 16));
    } catch (...) {
        return 0;
    }
}
#endif

// 0 means automatic.
int initial_setting() noexcept {
    try {
        const std::string text = stb::env("LIBSTB_THREADS");
        if (text.empty()) return 0;
        int n = 0;
        if (parse_threads_setting(text, n)) return n;
        std::fprintf(stderr, "libstb: LIBSTB_THREADS=%s is not auto, off or a number of threads; using auto\n", text.c_str());
    } catch (...) {
    }
    return 0;
}

std::atomic<int>& setting() {
    static std::atomic<int> s{initial_setting()};
    return s;
}

// Per-thread override (thread_scope): -1 means "follow the global setting".
thread_local int tl_override = -1;

std::atomic<double> g_grain_ns{250000.0};
std::atomic<unsigned long long> g_runs{0};

}  // namespace

bool parse_threads_setting(std::string_view text, int& out) noexcept {
    if (text == "auto") {
        out = 0;
        return true;
    }
    if (text == "off") {
        out = 1;
        return true;
    }
    if (text.empty() || text.size() > 6) return false;
    int n = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    out = n;
    return true;
}

int available_cpus() noexcept {
    static const int cpus = [] {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        return 1;  // a build without thread support
#else
        int n = int(std::thread::hardware_concurrency());  // 0 when unknown
#if defined(__linux__)
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof set, &set) == 0 && CPU_COUNT(&set) > 0) n = CPU_COUNT(&set);
        if (const int limit = cgroup_cpu_limit(); limit > 0) n = n > 0 ? std::min(n, limit) : limit;
#endif
        return std::max(n, 1);
#endif
    }();
    return cpus;
}

double parallel_grain_ns() noexcept { return g_grain_ns.load(std::memory_order_relaxed); }

void set_parallel_grain_ns(double ns) noexcept { g_grain_ns.store(ns > 0 ? ns : 250000.0, std::memory_order_relaxed); }

unsigned long long parallel_runs() noexcept { return g_runs.load(std::memory_order_relaxed); }

void note_parallel_run() noexcept { g_runs.fetch_add(1, std::memory_order_relaxed); }

}  // namespace detail

int thread_count() noexcept {
    const int requested =
        detail::tl_override >= 0 ? detail::tl_override : detail::setting().load(std::memory_order_relaxed);
    return requested == 0 ? detail::available_cpus() : requested;
}

bool set_threads(int n) noexcept {
    if (n < 0) return false;
    detail::setting().store(n, std::memory_order_relaxed);
    return true;
}

thread_scope::thread_scope(int n) {
    if (n < 0) throw std::invalid_argument("the number of threads must be >= 0 (0 means automatic)");
    previous_ = detail::tl_override;
    detail::tl_override = n;
}

thread_scope::~thread_scope() { detail::tl_override = previous_; }

}  // namespace stb
