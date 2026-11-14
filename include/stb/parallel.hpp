#pragma once

// Running one piece of work on several threads, only when that pays. For your own loops as much as for libstb's:
// this is what resize, the filters, tone and the superpixels use.
//
//   stb::utils::threads_for(work_ns)           how many threads `work_ns` nanoseconds of single-threaded work
//                                              deserves: never more than stb::thread_count() (set_threads,
//                                              thread_scope), and every thread must get at least 250 microseconds
//                                              of work, so small work answers 1 and no thread is created for it.
//   stb::utils::run_parallel(k, fn)            fn(0) .. fn(k - 1), concurrently: the calling thread runs one of
//                                              them and k - 1 short-lived threads run the others.
//   stb::utils::parallel_for(n, ns_each, fn)   fn(begin, end) on contiguous pieces of [0, n), as many pieces as
//                                              threads_for(n * ns_each) allows (a piece is never empty).
//
//   std::vector<float> v(10'000'000);
//   stb::utils::parallel_for(v.size(), 2.0 /* ns per item, a floor */, [&](std::size_t begin, std::size_t end) {
//       for (std::size_t i = begin; i < end; ++i) v[i] = std::sqrt(float(i));
//   });
//
// Starting and joining a thread costs a few tens of microseconds, and a core is only worth waking when it will be
// busy for a good deal longer than that. Say what one item costs as a floor of what one core was measured to do, so
// that a doubtful case runs on one thread. The pieces must not write to the same memory.
//
// There is no pool: threads exist for the duration of the call only. An exception thrown by any piece is rethrown
// on the calling thread after all the pieces have finished, the first one in piece order if several threw. If a
// thread cannot be started (std::system_error), its piece runs on the calling thread instead, so a call never
// fails because of the thread limit. The functions are templates: the lambda is not copied and may capture by
// reference.

#include <cstddef>
#include <exception>
#include <thread>
#include <vector>

#include "stb/export.hpp"
#include "stb/threads.hpp"

namespace stb {

namespace detail {
// Hooks that the templates below call into the library for (defined in src/util/threads.cpp).
STB_API double parallel_grain_ns() noexcept;                              // work per thread worth a spawn, ns
STB_API void note_parallel_run() noexcept;                                // counted for the tests
STB_API void warn_threads_unavailable(std::size_t missing, std::size_t total);  // logs, from the caller's thread
}  // namespace detail

namespace utils {

inline std::size_t threads_for(double work_ns) noexcept {
    const std::size_t cap = std::size_t(thread_count());
    if (cap < 2 || !(work_ns > 0)) return 1;
    const double by_work = work_ns / detail::parallel_grain_ns();
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
        // no more threads: the pieces without one are run on this thread, below (and said once, from here:
        // never from a worker)
        detail::warn_threads_unavailable(k - spawned, k - 1);
    }
    detail::note_parallel_run();
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
    const std::size_t wanted = threads_for(double(n) * ns_each);  // (STB_MIN would call it twice)
    const std::size_t k = STB_MIN(n, wanted);
    if (k <= 1) {
        fn(std::size_t(0), n);
        return;
    }
    const std::size_t each = n / k, extra = n % k;  // the first `extra` pieces get one more
    run_parallel(k, [&](std::size_t i) {
        const std::size_t begin = i * each + STB_MIN(i, extra);
        fn(begin, begin + each + (i < extra ? 1 : 0));
    });
}

}  // namespace utils
}  // namespace stb
