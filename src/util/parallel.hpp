#pragma once

// Internal: what sits next to the public stb/parallel.hpp (threads_for, run_parallel, parallel_for, which
// the library's own operations use through it): the grain that tests change, the counter they read, and the
// CPU count and the LIBSTB_THREADS parser that threads.cpp needs.

#include <string_view>

#include "stb/parallel.hpp"

namespace stb {
namespace detail {

void set_parallel_grain_ns(double ns) noexcept;  // ns <= 0 restores the default (250 000)

// How many run_parallel calls ran on more than one thread, since the process started (tests).
unsigned long long parallel_runs() noexcept;

// The number of CPUs this process may use (affinity and, on Linux, a cgroup CPU limit), at
// least 1. Computed once.
int available_cpus() noexcept;

// "auto" -> 0, "off" -> 1, decimal digits -> that number. False for anything else.
bool parse_threads_setting(std::string_view text, int& out) noexcept;

}  // namespace detail
}  // namespace stb
