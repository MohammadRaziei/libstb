// Benchmark of stb::composite_row / composite_solid_row (not a test: the tests/cpp/test_*.cpp glob does not pick it
// up). Built and run by the stb_bench_composite_row CMake target:
//
//   cmake --build <build dir> --target stb_bench_composite_row
//
// A rasterizer calls these once per span, so the row lengths that matter are short: 8, 32, 128 and 2048 pixels
// (a whole scanline). For each length and a few operators it times every SIMD backend this machine can run
// (scalar is the plain integer loop) over the same data and reports ns per pixel, best of several runs.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "stb/image.hpp"

namespace {

using u32 = std::uint32_t;

double best_ns_per_pixel(const std::string& backend, bool solid, stb::composite_op op, std::size_t len, int coverage) {
    // 64 KiB of rows, so the data stays in cache and the loop itself is measured
    const std::size_t rows = std::max<std::size_t>(1, 16384 / len);
    std::vector<u32> dest(rows * len), src(rows * len);
    std::uint32_t state = 7;
    for (std::size_t i = 0; i < dest.size(); ++i) {
        state = state * 1664525u + 1013904223u;
        const unsigned a = (state >> 24) | 1u;  // never 0, so not every pixel is skipped
        const auto make = [&](std::uint32_t x) { return u32(a) << 24 | ((x >> 3) % (a + 1)) << 16 | ((x >> 9) % (a + 1)) << 8 | ((x >> 15) % (a + 1)); };
        dest[i] = make(state);
        src[i] = make(state * 2654435761u);
    }
    const stb::simd_scope scope(backend);
    double best = 1e30;
    for (int run = 0; run < 7; ++run) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int rep = 0; rep < 64; ++rep)
            for (std::size_t r = 0; r < rows; ++r) {
                if (solid) stb::composite_solid_row(&dest[r * len], 0x80402010u, len, op, stb::blend_mode::normal, std::uint8_t(coverage));
                else stb::composite_row(&dest[r * len], &src[r * len], len, op, stb::blend_mode::normal, std::uint8_t(coverage));
            }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        best = std::min(best, ns / (64.0 * double(rows * len)));
    }
    return best;
}

}  // namespace

int main() {
    const std::vector<std::string> backends = stb::simd_backends();
    struct item {
        const char* name;
        stb::composite_op op;
        bool solid;
        int coverage;
    };
    const item items[] = {
        {"source_over, a row, full coverage", stb::composite_op::source_over, false, 255},
        {"source_over, a row, coverage 128", stb::composite_op::source_over, false, 128},
        {"source_over, one colour, coverage 128", stb::composite_op::source_over, true, 128},
        {"destination_in, a row (clip / mask)", stb::composite_op::destination_in, false, 255},
        {"source_atop, a row, coverage 128", stb::composite_op::source_atop, false, 128},
    };
    for (const item& it : items) {
        std::printf("%s\n  length", it.name);
        for (const std::string& b : backends) std::printf("%10s", b.c_str());
        std::printf("   (ns per pixel)\n");
        for (const std::size_t len : {std::size_t(8), std::size_t(32), std::size_t(128), std::size_t(2048)}) {
            std::printf("  %6zu", len);
            for (const std::string& b : backends) std::printf("%10.3f", best_ns_per_pixel(b, it.solid, it.op, len, it.coverage));
            std::printf("\n");
        }
    }
    return 0;
}
