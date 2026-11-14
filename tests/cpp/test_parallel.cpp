// stb/parallel.hpp on its own: the public threads_for, run_parallel and parallel_for, used the way a program of
// its own would (this file includes no internal header).
#include "stb/parallel.hpp"

#include <atomic>
#include <cstddef>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "test_common.hpp"

UTEST(libstb_parallel_tests, test_small_work_stays_on_one_thread_and_big_work_may_not) {
    stb::thread_scope scope(8);
    ASSERT_EQ(stb::utils::threads_for(0.0), std::size_t(1));
    ASSERT_EQ(stb::utils::threads_for(-5.0), std::size_t(1));
    ASSERT_EQ(stb::utils::threads_for(1000.0), std::size_t(1));  // a microsecond is not worth a thread
    const std::size_t big = stb::utils::threads_for(1e9);        // a second of work: all the threads allowed
    ASSERT_TRUE(big >= 1 && big <= 8);
    stb::thread_scope one(1);
    ASSERT_EQ(stb::utils::threads_for(1e9), std::size_t(1));
}

UTEST(libstb_parallel_tests, test_parallel_for_visits_every_index_once_in_contiguous_pieces) {
    for (int threads : {1, 3, 8}) {
        stb::thread_scope scope(threads);
        const std::size_t n = 1000;
        std::vector<int> hits(n, 0);
        std::atomic<int> pieces{0};
        std::atomic<bool> bad_piece{false};
        // 1e7 ns per item: ten milliseconds each, so the work is big enough to be shared
        stb::utils::parallel_for(n, 1e7, [&](std::size_t begin, std::size_t end) {
            if (!(begin < end && end <= n)) bad_piece = true;  // a piece is never empty
            for (std::size_t i = begin; i < end; ++i) ++hits[i];
            ++pieces;
        });
        ASSERT_FALSE(bad_piece.load());
        for (int h : hits) ASSERT_EQ(h, 1);
        ASSERT_TRUE(pieces.load() >= 1 && pieces.load() <= threads);
    }
    int calls = 0;
    stb::utils::parallel_for(0, 1e9, [&](std::size_t, std::size_t) { ++calls; });
    ASSERT_EQ(calls, 0);  // nothing to do: the function is not called
}

UTEST(libstb_parallel_tests, test_a_sum_does_not_depend_on_the_number_of_threads) {
    std::vector<long> v(100000);
    std::iota(v.begin(), v.end(), 1L);
    long expected = 0;
    for (long x : v) expected += x;
    for (int threads : {1, 2, 5}) {
        stb::thread_scope scope(threads);
        std::vector<long> partial(16, 0);
        std::atomic<std::size_t> next{0};
        stb::utils::parallel_for(v.size(), 1e5, [&](std::size_t begin, std::size_t end) {
            long s = 0;
            for (std::size_t i = begin; i < end; ++i) s += v[i];
            partial[next.fetch_add(1)] = s;
        });
        long total = 0;
        for (long p : partial) total += p;
        ASSERT_EQ(total, expected);
    }
}

UTEST(libstb_parallel_tests, test_run_parallel_runs_each_piece_once_and_the_first_exception_wins) {
    stb::thread_scope scope(4);
    std::vector<std::atomic<int>> runs(6);
    for (auto& r : runs) r = 0;
    stb::utils::run_parallel(6, [&](std::size_t i) { runs[i].fetch_add(1); });
    for (auto& r : runs) ASSERT_EQ(r.load(), 1);

    std::atomic<int> none{0};
    stb::utils::run_parallel(0, [&](std::size_t) { ++none; });
    ASSERT_EQ(none.load(), 0);  // k == 0 runs nothing
    std::thread::id where;
    stb::utils::run_parallel(1, [&](std::size_t) { where = std::this_thread::get_id(); });
    ASSERT_TRUE(where == std::this_thread::get_id());  // one piece: this thread, none is started

    std::atomic<int> finished{0};
    bool caught = false;
    try {
        stb::utils::run_parallel(4, [&](std::size_t i) {
            if (i == 2) throw std::runtime_error("piece 2");
            if (i == 3) throw std::logic_error("piece 3");
            ++finished;
        });
    } catch (const std::runtime_error& e) {  // the first in piece order, after all the pieces ended
        caught = std::string(e.what()) == "piece 2";
    }
    ASSERT_TRUE(caught);
    ASSERT_EQ(finished.load(), 2);
}
