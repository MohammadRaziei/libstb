#include <algorithm>
#include <atomic>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "test_common.hpp"
#include "util/parallel.hpp"

// stb::thread_count / set_threads / thread_scope, the internal helpers that decide whether a call
// is worth threads (util/parallel.hpp), and the one operation that uses them so far: resize.
// The tests include that internal header, like test_gsub.cpp does with the GSUB engine.

namespace {

// Puts the process-wide setting and the grain back to their defaults when a test ends.
struct restore_defaults {
    ~restore_defaults() {
        stb::set_threads(0);
        stb::detail::set_parallel_grain_ns(0);
    }
};

}  // namespace

// ------------------------------------------------------------------- the setting

UTEST(libstb_threads_tests, test_the_default_is_automatic_and_never_below_one) {
    restore_defaults restore;
    stb::set_threads(0);
    ASSERT_TRUE(stb::detail::available_cpus() >= 1);
    ASSERT_EQ(stb::detail::available_cpus(), stb::thread_count());
}

UTEST(libstb_threads_tests, test_set_threads_is_a_ceiling_and_rejects_negative_numbers) {
    restore_defaults restore;
    ASSERT_TRUE(stb::set_threads(1));
    ASSERT_EQ(1, stb::thread_count());
    ASSERT_TRUE(stb::set_threads(6));
    ASSERT_EQ(6, stb::thread_count());
    ASSERT_FALSE(stb::set_threads(-1));
    ASSERT_EQ(6, stb::thread_count());  // a refused value changes nothing
    ASSERT_TRUE(stb::set_threads(0));
    ASSERT_EQ(stb::detail::available_cpus(), stb::thread_count());
}

UTEST(libstb_threads_tests, test_scopes_nest_restore_and_reject_negative_numbers) {
    restore_defaults restore;
    stb::set_threads(5);
    {
        stb::thread_scope outer(2);
        ASSERT_EQ(2, stb::thread_count());
        {
            stb::thread_scope inner(1);
            ASSERT_EQ(1, stb::thread_count());
            stb::set_threads(9);  // the scope still wins
            ASSERT_EQ(1, stb::thread_count());
        }
        ASSERT_EQ(2, stb::thread_count());
        stb::thread_scope automatic(0);
        ASSERT_EQ(stb::detail::available_cpus(), stb::thread_count());
    }
    ASSERT_EQ(9, stb::thread_count());
    ASSERT_THROWS(stb::thread_scope(-1), std::invalid_argument);
    ASSERT_EQ(9, stb::thread_count());
}

UTEST(libstb_threads_tests, test_a_scope_changes_the_calling_thread_only) {
    restore_defaults restore;
    stb::set_threads(4);
    stb::thread_scope mine(2);
    int seen_elsewhere = 0, seen_with_own_scope = 0;
    std::thread other([&] {
        seen_elsewhere = stb::thread_count();  // follows the global setting
        stb::thread_scope own(3);
        seen_with_own_scope = stb::thread_count();
    });
    other.join();
    ASSERT_EQ(4, seen_elsewhere);
    ASSERT_EQ(3, seen_with_own_scope);
    ASSERT_EQ(2, stb::thread_count());
}

UTEST(libstb_threads_tests, test_the_environment_values_are_auto_off_or_a_number) {
    int n = -7;
    ASSERT_TRUE(stb::detail::parse_threads_setting("auto", n));
    ASSERT_EQ(0, n);
    ASSERT_TRUE(stb::detail::parse_threads_setting("off", n));
    ASSERT_EQ(1, n);
    ASSERT_TRUE(stb::detail::parse_threads_setting("12", n));
    ASSERT_EQ(12, n);
    ASSERT_TRUE(stb::detail::parse_threads_setting("0", n));
    ASSERT_EQ(0, n);
    for (const char* bad : {"", "AUTO", "-1", "+2", "2.5", "two", "1e3", " 4", "4 ", "1234567"}) {
        n = -7;
        ASSERT_FALSE(stb::detail::parse_threads_setting(bad, n));
        ASSERT_EQ(-7, n);  // untouched
    }
}

// ----------------------------------------------------------- is the work worth threads

UTEST(libstb_threads_tests, test_small_work_gets_one_thread_and_big_work_as_many_as_it_can_keep_busy) {
    restore_defaults restore;
    stb::set_threads(8);
    const double grain = stb::detail::parallel_grain_ns();
    ASSERT_TRUE(grain > 0);
    using stb::detail::threads_for;
    ASSERT_EQ(std::size_t(1), threads_for(0));
    ASSERT_EQ(std::size_t(1), threads_for(-5));
    ASSERT_EQ(std::size_t(1), threads_for(grain));            // one thread's worth
    ASSERT_EQ(std::size_t(1), threads_for(1.9 * grain));      // not enough for a second thread
    ASSERT_EQ(std::size_t(2), threads_for(2.0 * grain));
    ASSERT_EQ(std::size_t(2), threads_for(2.9 * grain));
    ASSERT_EQ(std::size_t(5), threads_for(5.5 * grain));
    ASSERT_EQ(std::size_t(8), threads_for(8.0 * grain));
    ASSERT_EQ(std::size_t(8), threads_for(1e12));              // never above the ceiling
    stb::set_threads(1);
    ASSERT_EQ(std::size_t(1), threads_for(1e12));              // and with a ceiling of 1, never threads
    stb::set_threads(8);
    stb::thread_scope two(2);
    ASSERT_EQ(std::size_t(2), threads_for(1e12));
}

UTEST(libstb_threads_tests, test_the_grain_can_be_lowered_and_restored) {
    restore_defaults restore;
    const double normal = stb::detail::parallel_grain_ns();
    stb::detail::set_parallel_grain_ns(10.0);
    ASSERT_TRUE(stb::detail::parallel_grain_ns() == 10.0);
    stb::detail::set_parallel_grain_ns(0);
    ASSERT_TRUE(stb::detail::parallel_grain_ns() == normal);
}

// -------------------------------------------------------------------- run_parallel

UTEST(libstb_threads_tests, test_run_parallel_runs_every_piece_once) {
    for (std::size_t k : {std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(5), std::size_t(17)}) {
        std::vector<std::atomic<int>> runs(k + 1);
        stb::detail::run_parallel(k, [&](std::size_t i) { runs[i].fetch_add(1); });
        for (std::size_t i = 0; i < k; ++i) ASSERT_EQ(1, runs[i].load());
        ASSERT_EQ(0, runs[k].load());  // nothing past the end
    }
}

UTEST(libstb_threads_tests, test_a_single_piece_runs_on_the_calling_thread_without_starting_anything) {
    const unsigned long long before = stb::detail::parallel_runs();
    std::thread::id where;
    stb::detail::run_parallel(1, [&](std::size_t) { where = std::this_thread::get_id(); });
    ASSERT_TRUE(where == std::this_thread::get_id());
    ASSERT_EQ(before, stb::detail::parallel_runs());
}

UTEST(libstb_threads_tests, test_several_pieces_run_on_more_than_one_thread) {
    const unsigned long long before = stb::detail::parallel_runs();
    std::set<std::thread::id> ids;
    std::atomic<int> arrived{0};
    std::vector<std::thread::id> by_piece(4);
    stb::detail::run_parallel(4, [&](std::size_t i) {
        by_piece[i] = std::this_thread::get_id();
        arrived.fetch_add(1);
    });
    for (const auto& id : by_piece) ids.insert(id);
    ASSERT_EQ(4, arrived.load());
    ASSERT_EQ(std::size_t(4), ids.size());  // three extra threads and the caller
    ASSERT_TRUE(by_piece[0] == std::this_thread::get_id());
    ASSERT_EQ(before + 1, stb::detail::parallel_runs());
}

UTEST(libstb_threads_tests, test_an_exception_is_rethrown_after_every_piece_finished_first_piece_first) {
    std::atomic<int> finished{0};
    bool caught = false;
    try {
        stb::detail::run_parallel(6, [&](std::size_t i) {
            if (i == 1 || i == 4) throw std::runtime_error("piece " + std::to_string(i));
            finished.fetch_add(1);
        });
    } catch (const std::runtime_error& e) {
        caught = true;
        ASSERT_STREQ("piece 1", e.what());
    }
    ASSERT_TRUE(caught);
    ASSERT_EQ(4, finished.load());  // the pieces that did not throw all ran to the end

    // an exception from the calling thread's own piece is handled the same way
    caught = false;
    try {
        stb::detail::run_parallel(3, [&](std::size_t i) {
            if (i == 0) throw std::logic_error("zero");
        });
    } catch (const std::logic_error&) {
        caught = true;
    }
    ASSERT_TRUE(caught);
}

// -------------------------------------------------------------------- parallel_for

UTEST(libstb_threads_tests, test_parallel_for_covers_the_range_once_in_contiguous_pieces) {
    restore_defaults restore;
    stb::set_threads(4);
    stb::detail::set_parallel_grain_ns(1.0);
    for (std::size_t n : {std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(4), std::size_t(5), std::size_t(97), std::size_t(1000)}) {
        std::vector<std::atomic<int>> seen(n);
        std::atomic<int> pieces{0};
        stb::detail::parallel_for(n, 1000.0, [&](std::size_t begin, std::size_t end) {
            pieces.fetch_add(1);
            for (std::size_t i = begin; i < end; ++i) seen[i].fetch_add(1);
        });
        for (std::size_t i = 0; i < n; ++i) ASSERT_EQ(1, seen[i].load());
        ASSERT_EQ(int(std::min<std::size_t>(n, 4)), pieces.load());  // as many pieces as threads, none empty
    }
    bool called = false;
    stb::detail::parallel_for(0, 1000.0, [&](std::size_t, std::size_t) { called = true; });
    ASSERT_FALSE(called);
}

UTEST(libstb_threads_tests, test_parallel_for_with_little_work_or_one_thread_is_one_call_on_this_thread) {
    restore_defaults restore;
    const unsigned long long before = stb::detail::parallel_runs();
    stb::set_threads(8);
    int calls = 0;
    std::size_t b = 99, e = 99;
    stb::detail::parallel_for(1000, 1.0, [&](std::size_t begin, std::size_t end) {  // 1000 ns: far below the grain
        ++calls, b = begin, e = end;
    });
    ASSERT_EQ(1, calls);
    ASSERT_EQ(std::size_t(0), b);
    ASSERT_EQ(std::size_t(1000), e);

    stb::set_threads(1);
    calls = 0;
    stb::detail::parallel_for(1000, 1e9, [&](std::size_t, std::size_t) { ++calls; });  // plenty of work, but a ceiling of 1
    ASSERT_EQ(1, calls);
    ASSERT_EQ(before, stb::detail::parallel_runs());
}

// ------------------------------------------------------------------------ resize

UTEST(libstb_threads_tests, test_resize_gives_the_same_bytes_on_any_number_of_threads) {
    restore_defaults restore;
    stb::detail::set_parallel_grain_ns(1.0);  // so that these small images take the threaded path
    struct shape {
        int w, h, nw, nh;
    };
    const shape shapes[] = {{700, 500, 301, 211}, {300, 200, 900, 700}, {640, 480, 640, 100}, {64, 64, 7, 5}, {50, 400, 50, 399}};
    for (const char* filter : {"nearest", "bilinear", "auto", "bicubic", "mitchell", "box", "bspline"})
        for (int channels = 1; channels <= 4; ++channels)
            for (const shape& s : shapes) {
                const stb::image img = testutil::gradient(s.w, s.h, channels);
                stb::image one, several, many;
                {
                    stb::thread_scope scope(1);
                    const unsigned long long before = stb::detail::parallel_runs();
                    one = img.resize(s.nw, s.nh, filter);
                    ASSERT_EQ(before, stb::detail::parallel_runs());  // one thread: nothing was started
                }
                {
                    stb::thread_scope scope(4);
                    const unsigned long long before = stb::detail::parallel_runs();
                    several = img.resize(s.nw, s.nh, filter);
                    // the engine keeps at least four output rows per piece: a 5 row result stays on one thread
                    if (s.nh >= 16) ASSERT_TRUE(stb::detail::parallel_runs() > before);
                }
                {
                    stb::thread_scope scope(7);
                    many = img.resize(s.nw, s.nh, filter);
                }
                ASSERT_TRUE(testutil::same(one, several));
                ASSERT_TRUE(testutil::same(one, many));
            }
}

UTEST(libstb_threads_tests, test_resize_really_uses_threads_when_the_work_is_big_enough) {
    restore_defaults restore;
    // No change to the grain: a 1600 x 1200 RGBA photograph is well past it on any machine.
    const stb::image big = testutil::gradient(1600, 1200, 4);
    stb::thread_scope scope(4);
    const unsigned long long before = stb::detail::parallel_runs();
    const stb::image threaded = big.resize(800, 600);
    ASSERT_TRUE(stb::detail::parallel_runs() > before);

    // and a thumbnail does not
    const stb::image thumb_source = testutil::gradient(64, 64, 4);
    const unsigned long long mid = stb::detail::parallel_runs();
    (void)thumb_source.resize(32, 32);
    ASSERT_EQ(mid, stb::detail::parallel_runs());

    stb::thread_scope one(1);
    ASSERT_TRUE(testutil::same(threaded, big.resize(800, 600)));
}

UTEST(libstb_threads_tests, test_resize_options_and_edge_modes_are_unaffected_by_threads) {
    restore_defaults restore;
    stb::detail::set_parallel_grain_ns(1.0);
    const stb::image img = testutil::gradient(333, 222, 4);
    for (stb::resize_edge edge : {stb::resize_edge::clamp, stb::resize_edge::reflect, stb::resize_edge::wrap, stb::resize_edge::zero})
        for (bool srgb : {false, true}) {
            stb::resize_options o;
            o.edge = edge;
            o.srgb = srgb;
            const stb::resizer r(o);
            stb::image one, four;
            {
                stb::thread_scope scope(1);
                one = r.resize(img, 111, 150);
            }
            {
                stb::thread_scope scope(4);
                four = r.resize(img, 111, 150);
            }
            ASSERT_TRUE(testutil::same(one, four));
        }
}

UTEST(libstb_threads_tests, test_concurrent_resizes_on_several_caller_threads_are_independent) {
    restore_defaults restore;
    stb::detail::set_parallel_grain_ns(1.0);
    const stb::image img = testutil::gradient(400, 300, 3);
    stb::image reference;
    {
        stb::thread_scope scope(1);
        reference = img.resize(200, 150);
    }
    std::vector<stb::image> results(4);
    std::vector<std::thread> callers;
    for (std::size_t i = 0; i < results.size(); ++i)
        callers.emplace_back([&, i] {
            stb::thread_scope scope(3);
            results[i] = img.resize(200, 150);
        });
    for (std::thread& t : callers) t.join();
    for (const stb::image& r : results) ASSERT_TRUE(testutil::same(reference, r));
}
