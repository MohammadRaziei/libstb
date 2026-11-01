// Benchmark of stb::rgba_to_premultiplied_argb / premultiplied_argb_to_rgba (not a test: the
// tests/cpp/test_*.cpp glob does not pick it up). Built and run by the stb_bench_premultiply
// CMake target:
//
//   cmake --build <build dir> --target stb_bench_premultiply
//
// 4096 x 4096 pixels (the first argument changes the side, e.g. 512 for a cache-resident run), three
// alpha mixes:
//   opaque        every pixel has alpha 255
//   half clear    a random half of the pixels has alpha 0, the rest 255
//   translucent   every pixel has an alpha between 1 and 254
// For each mix it times every SIMD backend this machine can run, out of place and in place, and
// a plain branchy per-channel loop with integer divisions (the shape of the C code the
// conversion replaces) for comparison, and a memcpy of the same bytes: the speed limit of a
// loop that only reads and writes memory. The best of several runs is reported: ns per pixel,
// and GB/s counting 8 bytes moved per pixel (4 read, 4 written).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "stb/image.hpp"

namespace {

int size = 4096;  // the side of the square image; the first argument changes it
std::size_t pixels = 0;

using bytes = std::vector<std::uint8_t>;

// The reference loops from the issue: branches for alpha 0 and 255, one division per channel.
void reference_to_premultiplied(std::uint8_t* dst, const std::uint8_t* src, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i, src += 4, dst += 4) {
        unsigned r = src[0], g = src[1], b = src[2];
        const unsigned a = src[3];
        std::uint32_t out = 0;
        if (a != 0) {
            if (a != 255) {
                r = r * a / 255;
                g = g * a / 255;
                b = b * a / 255;
            }
            out = a << 24 | r << 16 | g << 8 | b;
        }
        std::memcpy(dst, &out, 4);
    }
}

void reference_to_rgba(std::uint8_t* dst, const std::uint8_t* src, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i, src += 4, dst += 4) {
        std::uint32_t in;
        std::memcpy(&in, src, 4);
        unsigned a = in >> 24, r = (in >> 16) & 255, g = (in >> 8) & 255, b = in & 255;
        std::uint8_t out[4] = {0, 0, 0, 0};
        if (a != 0) {
            if (a != 255) {
                r = r * 255 / a;
                g = g * 255 / a;
                b = b * 255 / a;
            }
            out[0] = std::uint8_t(r), out[1] = std::uint8_t(g), out[2] = std::uint8_t(b), out[3] = std::uint8_t(a);
        }
        std::memcpy(dst, out, 4);
    }
}

enum class mix { opaque, half_clear, translucent };

const char* mix_name(mix m) {
    switch (m) {
        case mix::opaque: return "opaque";
        case mix::half_clear: return "half clear";
        default: return "translucent";
    }
}

// Straight RGBA with the alpha distribution of the mix.
bytes make_rgba(mix m) {
    std::mt19937 rng(12345);
    bytes b(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint32_t r = rng();
        b[4 * i + 0] = std::uint8_t(r);
        b[4 * i + 1] = std::uint8_t(r >> 8);
        b[4 * i + 2] = std::uint8_t(r >> 16);
        const std::uint32_t q = rng();
        b[4 * i + 3] = m == mix::opaque ? std::uint8_t(255) : m == mix::half_clear ? std::uint8_t((q & 1) ? 255 : 0) : std::uint8_t(1 + q % 254);
    }
    return b;
}

template <class Fn>
double best_ns_per_pixel(Fn fn) {
    using clock = std::chrono::steady_clock;
    fn();  // warm-up: page the buffers in
    double best = 1e300;
    for (int run = 0; run < 7; ++run) {
        const auto t0 = clock::now();
        fn();
        const auto t1 = clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / double(pixels));
    }
    return best;
}

void row(const std::string& what, double ns, double scalar_ns) {
    std::printf("    %-24s %7.3f ns/px %7.2f GB/s   x%.1f\n", what.c_str(), ns, 8.0 / ns, scalar_ns / ns);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) size = std::max(1, std::atoi(argv[1]));
    pixels = std::size_t(size) * std::size_t(size);
    std::printf("stb::rgba_to_premultiplied_argb / premultiplied_argb_to_rgba, %d x %d (best of 7)\n", size, size);
    std::printf("backends:");
    for (const std::string& b : stb::simd_backends()) std::printf(" %s", b.c_str());
    std::printf("\n(x = speed-up over the branchy reference loop)\n");

    volatile std::uint8_t sink = 0;
    for (mix m : {mix::opaque, mix::half_clear, mix::translucent}) {
        const bytes straight = make_rgba(m);
        bytes premultiplied(straight.size());
        reference_to_premultiplied(premultiplied.data(), straight.data(), pixels);  // the input of the inverse
        bytes out(straight.size());
        const std::size_t stride = std::size_t(size) * 4;

        std::printf("\n%s\n", mix_name(m));
        for (int inverse = 0; inverse < 2; ++inverse) {
            const bytes& in = inverse ? premultiplied : straight;
            std::printf("  %s\n", inverse ? "premultiplied ARGB32 -> straight RGBA" : "straight RGBA -> premultiplied ARGB32");
            const double ref = best_ns_per_pixel([&] {
                (inverse ? reference_to_rgba : reference_to_premultiplied)(out.data(), in.data(), pixels);
                sink = out[pixels];
            });
            row("reference loop", ref, ref);
            row("memcpy (the limit)", best_ns_per_pixel([&] {
                    std::memcpy(out.data(), in.data(), in.size());
                    sink = out[pixels];
                }), ref);
            for (const std::string& backend : stb::simd_backends()) {
                stb::simd_scope scope(backend);
                const auto convert = inverse ? stb::premultiplied_argb_to_rgba : stb::rgba_to_premultiplied_argb;
                row(backend + " out of place", best_ns_per_pixel([&] {
                        convert(out.data(), stride, in.data(), stride, size, size);
                        sink = out[pixels];
                    }), ref);
                bytes work = in;  // in place converts the same data again and again: the cost is what counts
                row(backend + " in place", best_ns_per_pixel([&] {
                        convert(work.data(), stride, work.data(), stride, size, size);
                        sink = work[pixels];
                    }), ref);
            }
        }
    }
    (void)sink;
    return 0;
}
