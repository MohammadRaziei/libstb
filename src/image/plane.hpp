#pragma once

// Internal: what the filters, the median and the morphology share to look at the values around a position.

#include <algorithm>
#include <cstddef>
#include <vector>

#include "stb/mat.hpp"
#include "stb/parallel.hpp"

namespace stb {
namespace detail_plane {

// The index that stands for i in [0, n) under the edge mode, or -1 where the mode says "zero".
inline int resolve(int i, int n, edge_mode mode) noexcept {
    if (i >= 0 && i < n) return i;
    switch (mode) {
        case edge_mode::zero: return -1;
        case edge_mode::wrap: {
            const int r = i % n;
            return r < 0 ? r + n : r;
        }
        case edge_mode::reflect: {  // dcba|abcd|dcba
            const int period = 2 * n;
            int r = i % period;
            if (r < 0) r += period;
            return r < n ? r : period - 1 - r;
        }
        case edge_mode::mirror: {  // dcb|abcd|cba
            if (n == 1) return 0;
            const int period = 2 * (n - 1);
            int r = i % period;
            if (r < 0) r += period;
            return r < n ? r : period - r;
        }
        case edge_mode::clamp: break;
    }
    return i < 0 ? 0 : n - 1;
}

// One channel `k` of `src` with `rx` values added at the left and right and `ry` above and below, as the edge mode says:
// (w + 2 rx) x (h + 2 ry) values, row by row.
template <class T>
std::vector<T> padded_plane(const mat<T>& src, int k, int rx, int ry, edge_mode edge) {
    constexpr double copy_ns = 0.3;  // moving one value, a floor of what one core does (stb/parallel.hpp)
    const int w = src.width(), h = src.height(), c = src.channels();
    const std::size_t pw = std::size_t(w) + 2 * std::size_t(rx), ph = std::size_t(h) + 2 * std::size_t(ry);
    std::vector<int> cx(pw);
    for (std::size_t x = 0; x < pw; ++x) cx[x] = resolve(int(x) - rx, w, edge);
    std::vector<T> plane(pw * ph);
    utils::parallel_for(ph, double(pw) * copy_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t y = b; y < e; ++y) {
            T* d = plane.data() + y * pw;
            const int sy = resolve(int(y) - ry, h, edge);
            if (sy < 0) {
                std::fill(d, d + pw, T(0));
                continue;
            }
            const T* row = src.row(sy);
            for (std::size_t x = 0; x < pw; ++x)
                d[x] = cx[x] < 0 ? T(0) : row[std::size_t(cx[x]) * std::size_t(c) + std::size_t(k)];
        }
    });
    return plane;
}

}  // namespace detail_plane
}  // namespace stb
