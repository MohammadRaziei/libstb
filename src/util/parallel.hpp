#pragma once

// Internal: running one piece of work on several threads, only when that pays.
//
//   threads_for(work_ns)           how many threads `work_ns` nanoseconds of single-threaded work
//                                  deserves: never more than stb::thread_count(), and every
//                                  thread must get at least parallel_grain_ns() of work, so
//                                  small work answers 1 and no thread is created for it.
//   run_parallel(k, fn)            fn(0) .. fn(k - 1), concurrently: the calling thread runs one of
//                                  them and k - 1 short-lived threads run the others.
//   parallel_for(n, ns_each, fn)   fn(begin, end) on contiguous pieces of [0, n), as many pieces
//                                  as threads_for(n * ns_each) allows.
//
// Starting and joining a thread costs a few tens of microseconds, and a core is only worth
// waking when it will be busy for a good deal longer than that: the grain is 250 microseconds
// of work per thread (the spawn is then a few percent of the time). Callers estimate their work
// from a floor of what one core was measured to do, so that a doubtful case runs on one thread.
//
// There is no pool: threads exist for the duration of the call only. An exception thrown by any
// piece is rethrown on the calling thread after all the pieces have finished, the first one in
// piece order if several threw. If a thread cannot be started (std::system_error), its piece
// runs on the calling thread instead, so a call never fails because of the thread limit.

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string_view>
#include <thread>
#include <vector>

#include "stb/threads.hpp"

namespace stb {
namespace detail {

// The work, in nanoseconds, that one thread must have to be worth starting. 250 000 unless a test
// changes it (so that small inputs exercise the threaded path).
double parallel_grain_ns() noexcept;
void set_parallel_grain_ns(double ns) noexcept;  // ns <= 0 restores the default

// How many run_parallel calls ran on more than one thread, since the process started (tests).
unsigned long long parallel_runs() noexcept;
void note_parallel_run() noexcept;

// The number of CPUs this process may use (affinity and, on Linux, a cgroup CPU limit), at
// least 1. Computed once.
int available_cpus() noexcept;

// "auto" -> 0, "off" -> 1, decimal digits -> that number. False for anything else.
bool parse_threads_setting(std::string_view text, int& out) noexcept;

inline std::size_t threads_for(double work_ns) noexcept {
    const std::size_t cap = std::size_t(thread_count());
    if (cap < 2 || !(work_ns > 0)) return 1;
    const double by_work = work_ns / parallel_grain_ns();
    if (by_work < 2) return 1;
    return by_work >= double(cap) ? cap : std::size_t(by_work);
}

template <class Fn>
void run_parallel(std::size_t k, Fn&& fn) {
    if (k == 0) return;
    if (k == 1) {
        fn(std::size_t(0));
        return;
    }
    std::vector<std::exception_ptr> errors(k);
    std::vector<std::thread> workers;
    std::size_t spawned = 1;  // pieces [1, spawned) have a thread; the rest run below, here
    try {
        workers.reserve(k - 1);
        for (; spawned < k; ++spawned)
            workers.emplace_back([&fn, &errors, spawned] {
                try {
                    fn(spawned);
                } catch (...) {
                    errors[spawned] = std::current_exception();
                }
            });
    } catch (...) {
        // no more threads: the pieces without one are run on this thread, below
    }
    note_parallel_run();
    const auto run_here = [&](std::size_t i) {
        try {
            fn(i);
        } catch (...) {
            errors[i] = std::current_exception();
        }
    };
    run_here(0);
    for (std::size_t i = spawned; i < k; ++i) run_here(i);  // the pieces that got no thread
    for (std::thread& t : workers) t.join();
    for (const std::exception_ptr& e : errors)
        if (e) std::rethrow_exception(e);
}

template <class Fn>
void parallel_for(std::size_t n, double ns_each, Fn&& fn) {
    if (n == 0) return;
    const std::size_t k = (std::min)(n, threads_for(double(n) * ns_each));
    if (k <= 1) {
        fn(std::size_t(0), n);
        return;
    }
    const std::size_t each = n / k, extra = n % k;  // the first `extra` pieces get one more
    run_parallel(k, [&](std::size_t i) {
        const std::size_t begin = i * each + (std::min)(i, extra);
        fn(begin, begin + each + (i < extra ? 1 : 0));
    });
}

}  // namespace detail
}  // namespace stb
