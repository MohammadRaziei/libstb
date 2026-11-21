#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "stb/filter.hpp"
#include "stb/parallel.hpp"

namespace stb {
namespace {

using utils::parallel_for;

constexpr double thin_ns = 6.0;  // the direction test and up to two neighbours for one value, a floor of what one core does (stb/parallel.hpp)

// The picture as one channel of floats (the 0 to 1 range): the luma of 3 or 4 channels, the channel itself for 1.
template <class T>
mat32f unit_gray(const mat<T>& src) {
    const int c = src.channels();
    if (c != 1 && c != 3 && c != 4) throw std::invalid_argument("canny needs a picture of 1, 3 or 4 channels");
    mat32f f;
    if constexpr (std::is_same_v<T, std::uint8_t>) f = to_f32(src);
    else f = src.copy();
    if (c == 1) return f;
    mat32f gray(src.width(), src.height(), 1);
    parallel_for(std::size_t(src.height()), double(src.width()) * 2.0, [&](std::size_t b, std::size_t e) {
        for (std::size_t y = b; y < e; ++y) {
            const float* s = f.row(int(y));
            float* d = gray.row(int(y));
            for (int x = 0; x < src.width(); ++x) {
                const float* p = s + std::size_t(x) * std::size_t(c);
                d[x] = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
            }
        }
    });
    return gray;
}

}  // namespace

namespace detail {

template <class T>
mat8u canny_mat(const mat<T>& src, float low, float high, float sigma) {
    if (!(low >= 0.0f) || !(high >= low) || !std::isfinite(high)) throw std::invalid_argument("canny needs 0 <= low <= high");
    if (!(sigma >= 0.0f) || !std::isfinite(sigma)) throw std::invalid_argument("sigma must be a finite number >= 0");
    if (src.empty()) return mat8u();
    mat32f gray = unit_gray(src);
    if (sigma > 0.0f) gray = gaussian_blur(gray, sigma);
    const gradients g = sobel(gray);
    const mat32f mag = magnitude(g);
    const int w = src.width(), h = src.height();
    // the limits are fractions of a step across the picture, which Sobel makes 4
    const float lo = low * 4.0f, hi = high * 4.0f;

    // state: 0 nothing, 1 a ridge point of at least `low`, 2 one of at least `high`
    std::vector<std::uint8_t> state(std::size_t(w) * std::size_t(h), 0);
    constexpr float tan22 = 0.41421356f, tan67 = 2.41421356f;
    parallel_for(std::size_t(h), double(w) * thin_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t yy = b; yy < e; ++yy) {
            const int y = int(yy);
            const float* m = mag.row(y);
            const float* dx = g.dx.row(y);
            const float* dy = g.dy.row(y);
            for (int x = 0; x < w; ++x) {
                const float v = m[x];
                if (!(v >= lo) || v <= 0.0f) continue;
                const auto at = [&](int xx, int yy2) { return (xx < 0 || yy2 < 0 || xx >= w || yy2 >= h) ? 0.0f : mag.row(yy2)[xx]; };
                const float ax = std::fabs(dx[x]), ay = std::fabs(dy[x]);
                float before, after;  // the neighbours along the gradient; the first must be smaller, the second not larger
                if (ay <= tan22 * ax) {
                    before = at(x - 1, y);
                    after = at(x + 1, y);
                } else if (ay >= tan67 * ax) {
                    before = at(x, y - 1);
                    after = at(x, y + 1);
                } else if ((dx[x] > 0.0f) == (dy[x] > 0.0f)) {
                    before = at(x - 1, y - 1);
                    after = at(x + 1, y + 1);
                } else {
                    before = at(x - 1, y + 1);
                    after = at(x + 1, y - 1);
                }
                if (v > before && v >= after) state[std::size_t(y) * std::size_t(w) + std::size_t(x)] = std::uint8_t(v >= hi ? 2 : 1);
            }
        }
    });

    // hysteresis: everything connected (8 neighbours) to a strong point through points of at least `low`
    mat8u out(w, h, 1);
    std::memset(out.data(), 0, out.size());
    std::vector<std::size_t> stack;
    const std::size_t W = std::size_t(w);
    for (std::size_t s = 0; s < state.size(); ++s) {
        if (state[s] != 2 || out.data()[s] != 0) continue;
        out.data()[s] = 255;
        stack.push_back(s);
        while (!stack.empty()) {
            const std::size_t p = stack.back();
            stack.pop_back();
            const int px = int(p % W), py = int(p / W);
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const int qx = px + ox, qy = py + oy;
                    if ((ox == 0 && oy == 0) || qx < 0 || qy < 0 || qx >= w || qy >= h) continue;
                    const std::size_t q = std::size_t(qy) * W + std::size_t(qx);
                    if (state[q] != 0 && out.data()[q] == 0) {
                        out.data()[q] = 255;
                        stack.push_back(q);
                    }
                }
        }
    }
    return out;
}

template STB_API mat8u canny_mat<std::uint8_t>(const mat<std::uint8_t>&, float, float, float);
template STB_API mat8u canny_mat<float>(const mat<float>&, float, float, float);

}  // namespace detail
}  // namespace stb
