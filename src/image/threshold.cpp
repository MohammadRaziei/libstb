#include "stb/threshold.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "pixel_range.hpp"
#include "stb/filter.hpp"
#include "stb/parallel.hpp"

namespace stb {
namespace {

using utils::parallel_for;

// Floors of what one core does (ns), so that a doubtful case stays on one thread (stb/parallel.hpp).
constexpr double compare_ns = 0.3;  // one comparison and the store of its result, per value

template <class T>
void need_one_channel(const mat<T>& src) {
    if (!src.empty() && src.channels() != 1) throw std::invalid_argument("expected a picture of one channel");
}

// The values as floats of the 0 to 1 range: an 8-bit picture is divided by 255, a float one is used as it is.
const mat32f& as_unit(const mat32f& src, mat32f&) { return src; }
const mat32f& as_unit(const mat8u& src, mat32f& scratch) {
    scratch = to_f32(src);
    return scratch;
}

// The between-class variance of every split of a histogram (counts[i] values at position pos(i), in order) and the
// index of the first best one: the split puts bins 0..i in the lower class and i+1.. in the upper one. At least two bins.
template <class Pos>
std::size_t best_split(const std::vector<double>& counts, Pos pos) {
    const std::size_t n = counts.size();
    std::vector<double> w1(n), s1(n);  // the weight and the sum of position * weight up to and including bin i
    double w = 0.0, s = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        w += counts[i];
        s += counts[i] * pos(i);
        w1[i] = w;
        s1[i] = s;
    }
    std::size_t best = 0;
    double best_var = -1.0;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double wl = w1[i], wu = w - w1[i];
        if (wl <= 0.0 || wu <= 0.0) continue;
        const double ml = s1[i] / wl, mu = (s - s1[i]) / wu;
        const double var = wl * wu * (ml - mu) * (ml - mu);
        if (var > best_var) {
            best_var = var;
            best = i;
        }
    }
    return best;
}

}  // namespace

namespace detail {

template <class T>
mat8u threshold_mat(const mat<T>& src, T t, bool invert) {
    need_one_channel(src);
    if (src.empty()) return mat8u();
    mat8u out(src.width(), src.height(), 1);
    const std::size_t w = std::size_t(src.width());
    parallel_for(std::size_t(src.height()), double(w) * compare_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t y = b; y < e; ++y) {
            const T* s = src.row(int(y));
            std::uint8_t* d = out.row(int(y));
            for (std::size_t x = 0; x < w; ++x) d[x] = std::uint8_t(((s[x] > t) != invert) ? 255 : 0);
        }
    });
    return out;
}

template <class T>
T otsu_threshold_mat(const mat<T>& src) {
    need_one_channel(src);
    if (src.empty()) throw std::invalid_argument("the picture is empty");
    if constexpr (!std::is_floating_point_v<T>) {  // an integer type: the histogram has one bin for every level
        constexpr std::size_t levels = std::size_t(detail_pixel::range<T>::max) + 1;
        std::vector<double> hist(levels, 0.0);
        std::size_t lo = levels - 1, hi = 0;
        for (int y = 0; y < src.height(); ++y) {
            const T* s = src.row(y);
            for (int x = 0; x < src.width(); ++x) hist[s[x]] += 1.0;
        }
        for (std::size_t i = 0; i < levels; ++i)
            if (hist[i] > 0.0) {
                if (lo == levels - 1 && i < lo) lo = i;
                hi = i;
            }
        if (lo == hi) return T(lo);
        const std::vector<double> counts(hist.begin() + std::ptrdiff_t(lo), hist.begin() + std::ptrdiff_t(hi) + 1);
        return T(lo + best_split(counts, [&](std::size_t i) { return double(lo + i); }));
    } else {
        double lo = 0.0, hi = 0.0;
        bool any = false;
        for (int y = 0; y < src.height(); ++y) {
            const float* s = src.row(y);
            for (int x = 0; x < src.width(); ++x) {
                if (!std::isfinite(s[x])) continue;
                const double v = double(s[x]);
                if (!any) lo = hi = v;
                any = true;
                lo = v < lo ? v : lo;
                hi = v > hi ? v : hi;
            }
        }
        if (!any) throw std::invalid_argument("the picture has no finite value");
        if (lo == hi) return float(lo);
        constexpr std::size_t bins = 256;
        std::vector<double> counts(bins, 0.0);
        const double scale = double(bins) / (hi - lo);
        for (int y = 0; y < src.height(); ++y) {
            const float* s = src.row(y);
            for (int x = 0; x < src.width(); ++x) {
                if (!std::isfinite(s[x])) continue;
                std::size_t i = std::size_t((double(s[x]) - lo) * scale);
                counts[i < bins ? i : bins - 1] += 1.0;
            }
        }
        const double width = (hi - lo) / double(bins);
        return float(lo + (double(best_split(counts, [&](std::size_t i) { return lo + (double(i) + 0.5) * width; })) + 0.5) * width);
    }
}

template <class T>
mat8u adaptive_threshold_mat(const mat<T>& src, int radius, float offset, adaptive_method method, bool invert, edge_mode edge) {
    if (radius < 1 || radius > 1000) throw std::invalid_argument("radius must be in 1..1000");
    if (!std::isfinite(offset)) throw std::invalid_argument("offset must be a finite number");
    need_one_channel(src);
    if (src.empty()) return mat8u();
    mat32f scratch;
    const mat32f& f = as_unit(src, scratch);
    // the offset is in levels for an integer type (a value is read as value / maximum) and in the unit range for floats
    const float off = offset / float(detail_pixel::range<T>::max);
    mat32f local;
    if (method == adaptive_method::mean) {
        local = box_blur(f, radius, edge);
    } else {
        const mat32f g = gaussian_kernel(0.3f * float(radius) + 0.5f, radius);
        local = convolve(f, g, g, edge);
    }
    constexpr float tolerance = 1e-5f;  // below this a value is equal to its neighbourhood (a flat area stays 0)
    mat8u out(src.width(), src.height(), 1);
    const std::size_t w = std::size_t(src.width());
    parallel_for(std::size_t(src.height()), double(w) * compare_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t y = b; y < e; ++y) {
            const float* v = f.row(int(y));
            const float* l = local.row(int(y));
            std::uint8_t* d = out.row(int(y));
            for (std::size_t x = 0; x < w; ++x) d[x] = std::uint8_t(((v[x] > l[x] - off + tolerance) != invert) ? 255 : 0);
        }
    });
    return out;
}

template STB_API mat8u threshold_mat<std::uint8_t>(const mat<std::uint8_t>&, std::uint8_t, bool);
template STB_API mat8u threshold_mat<float>(const mat<float>&, float, bool);
template STB_API std::uint8_t otsu_threshold_mat<std::uint8_t>(const mat<std::uint8_t>&);
template STB_API float otsu_threshold_mat<float>(const mat<float>&);
template STB_API mat8u adaptive_threshold_mat<std::uint8_t>(const mat<std::uint8_t>&, int, float, adaptive_method, bool, edge_mode);
template STB_API mat8u adaptive_threshold_mat<float>(const mat<float>&, int, float, adaptive_method, bool, edge_mode);

}  // namespace detail
}  // namespace stb
