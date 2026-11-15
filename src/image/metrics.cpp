#include "stb/metrics.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "stb/parallel.hpp"

namespace stb {
namespace {

using utils::parallel_for;

// Floors of what one core does (ns) per value, so that a doubtful case stays on one thread (stb/parallel.hpp).
constexpr double diff_ns = 0.3;  // one difference and its sum, per value
constexpr double ssim_ns = 6.0;  // one value of the Gaussian window sums, per value

template <class T>
void check_pair(const mat<T>& a, const mat<T>& b) {
    if (a.empty() || b.empty()) throw std::invalid_argument("the pictures are empty");
    if (a.width() != b.width() || a.height() != b.height() || a.channels() != b.channels())
        throw std::invalid_argument("the two pictures must have the same size and number of channels");
}

// The sum over all values of f(a, b), as the sum of one partial sum per row: each row is added in order on one thread and
// the rows are added in order afterwards, so the result is the same for every split of the work.
template <class T, class Sum, class F>
Sum sum_rows(const mat<T>& a, const mat<T>& b, F f) {
    const std::size_t h = std::size_t(a.height()), stride = a.stride();
    std::vector<Sum> rows(h);
    parallel_for(h, double(stride) * diff_ns, [&](std::size_t begin, std::size_t end) {
        for (std::size_t y = begin; y < end; ++y) {
            const T* pa = a.row(int(y));
            const T* pb = b.row(int(y));
            Sum s = 0;
            for (std::size_t i = 0; i < stride; ++i) s += f(pa[i], pb[i]);
            rows[y] = s;
        }
    });
    Sum total = 0;
    for (const Sum& s : rows) total += s;
    return total;
}

double psnr_from_mse(double mse, double peak) {
    if (mse == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(peak * peak / mse);
}

// ------------------------------------------------------------------------------------------------------ SSIM

constexpr int win = 11;          // the window is win x win values
constexpr int win_r = win / 2;   // 5
constexpr double ssim_sigma = 1.5;

// The 11 weights of a Gaussian of sigma 1.5, added to 1 (what scipy's gaussian_filter makes for truncate = 3.5).
std::vector<double> ssim_weights() {
    std::vector<double> w(win);
    double sum = 0.0;
    for (int i = 0; i < win; ++i) {
        const double x = double(i - win_r);
        w[std::size_t(i)] = std::exp(-(x * x) / (2.0 * ssim_sigma * ssim_sigma));
        sum += w[std::size_t(i)];
    }
    for (double& v : w) v /= sum;
    return w;
}

inline double as_unit(std::uint8_t v) { return double(v) / 255.0; }
inline double as_unit(float v) { return double(v); }

// One output row block: output rows [y0, y1) of every channel. The five windowed sums (of a, b, a^2, b^2, a*b) are
// made horizontally for the input rows y0 .. y1 + win - 2 and then vertically, all in double and in one fixed order.
template <class T>
void ssim_block(const mat<T>& a, const mat<T>& b, const std::vector<double>& w, int y0, int y1, mat32f& out) {
    const int c = a.channels();
    const int ow = out.width();
    const int rows_in = (y1 - y0) + win - 1;
    constexpr int maps = 5;
    // h[map][row][x * c + channel]
    const std::size_t line = std::size_t(ow) * std::size_t(c);
    std::vector<double> h(std::size_t(maps) * std::size_t(rows_in) * line);
    for (int r = 0; r < rows_in; ++r) {
        const T* pa = a.row(y0 + r);
        const T* pb = b.row(y0 + r);
        for (int x = 0; x < ow; ++x) {
            for (int k = 0; k < c; ++k) {
                double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
                for (int t = 0; t < win; ++t) {
                    const std::size_t i = std::size_t(x + t) * std::size_t(c) + std::size_t(k);
                    const double va = as_unit(pa[i]), vb = as_unit(pb[i]);
                    const double wt = w[std::size_t(t)];
                    sa += wt * va;
                    sb += wt * vb;
                    saa += wt * va * va;
                    sbb += wt * vb * vb;
                    sab += wt * va * vb;
                }
                const std::size_t o = std::size_t(x) * std::size_t(c) + std::size_t(k);
                const std::size_t base = std::size_t(r) * line + o;
                h[0 * std::size_t(rows_in) * line + base] = sa;
                h[1 * std::size_t(rows_in) * line + base] = sb;
                h[2 * std::size_t(rows_in) * line + base] = saa;
                h[3 * std::size_t(rows_in) * line + base] = sbb;
                h[4 * std::size_t(rows_in) * line + base] = sab;
            }
        }
    }
    constexpr double c1 = 0.01 * 0.01, c2 = 0.03 * 0.03;  // (K * L)^2 with L = 1
    for (int y = y0; y < y1; ++y) {
        float* dst = out.row(y);
        for (std::size_t o = 0; o < line; ++o) {
            double m[maps] = {0, 0, 0, 0, 0};
            for (int t = 0; t < win; ++t) {
                const std::size_t r = std::size_t(y - y0 + t);
                for (int q = 0; q < maps; ++q)
                    m[q] += w[std::size_t(t)] * h[std::size_t(q) * std::size_t(rows_in) * line + r * line + o];
            }
            const double mu_a = m[0], mu_b = m[1];
            const double var_a = m[2] - mu_a * mu_a, var_b = m[3] - mu_b * mu_b, cov = m[4] - mu_a * mu_b;
            const double num = (2.0 * mu_a * mu_b + c1) * (2.0 * cov + c2);
            const double den = (mu_a * mu_a + mu_b * mu_b + c1) * (var_a + var_b + c2);
            dst[o] = float(num / den);
        }
    }
}

template <class T>
mat32f ssim_map_of(const mat<T>& a, const mat<T>& b) {
    check_pair(a, b);
    if (a.width() < win || a.height() < win) throw std::invalid_argument("ssim needs a picture of at least 11 x 11");
    const int ow = a.width() - (win - 1), oh = a.height() - (win - 1);
    mat32f out(ow, oh, a.channels());
    const std::vector<double> w = ssim_weights();
    constexpr int block = 32;  // output rows per piece of work (the input rows around them are read again)
    const std::size_t blocks = (std::size_t(oh) + block - 1) / block;
    parallel_for(blocks, double(block) * double(out.stride()) * ssim_ns, [&](std::size_t b0, std::size_t b1) {
        for (std::size_t i = b0; i < b1; ++i) {
            const int y0 = int(i) * block;
            ssim_block(a, b, w, y0, STB_MIN(oh, y0 + block), out);
        }
    });
    return out;
}

double mean_of(const mat32f& m) {
    double total = 0.0;
    const std::size_t h = std::size_t(m.height());
    for (std::size_t y = 0; y < h; ++y) {
        const float* r = m.row(int(y));
        double s = 0.0;
        for (std::size_t i = 0; i < m.stride(); ++i) s += double(r[i]);
        total += s;
    }
    return total / double(m.size());
}

}  // namespace

// ---------------------------------------------------------------------------------------------------- public

double mae(const mat8u& a, const mat8u& b) {
    check_pair(a, b);
    const std::uint64_t s = sum_rows<std::uint8_t, std::uint64_t>(
        a, b, [](std::uint8_t x, std::uint8_t y) { return std::uint64_t(x > y ? x - y : y - x); });
    return double(s) / double(a.size());
}
double mae(const mat32f& a, const mat32f& b) {
    check_pair(a, b);
    return sum_rows<float, double>(a, b, [](float x, float y) { return std::fabs(double(x) - double(y)); }) / double(a.size());
}

double mse(const mat8u& a, const mat8u& b) {
    check_pair(a, b);
    const std::uint64_t s = sum_rows<std::uint8_t, std::uint64_t>(a, b, [](std::uint8_t x, std::uint8_t y) {
        const std::uint64_t d = std::uint64_t(x > y ? x - y : y - x);
        return d * d;
    });
    return double(s) / double(a.size());
}
double mse(const mat32f& a, const mat32f& b) {
    check_pair(a, b);
    return sum_rows<float, double>(a, b,
                                   [](float x, float y) {
                                       const double d = double(x) - double(y);
                                       return d * d;
                                   }) /
           double(a.size());
}

double psnr(const mat8u& a, const mat8u& b) { return psnr_from_mse(mse(a, b), 255.0); }
double psnr(const mat32f& a, const mat32f& b) { return psnr_from_mse(mse(a, b), 1.0); }

mat32f ssim_map(const mat8u& a, const mat8u& b) { return ssim_map_of(a, b); }
mat32f ssim_map(const mat32f& a, const mat32f& b) { return ssim_map_of(a, b); }
double ssim(const mat8u& a, const mat8u& b) { return mean_of(ssim_map_of(a, b)); }
double ssim(const mat32f& a, const mat32f& b) { return mean_of(ssim_map_of(a, b)); }

}  // namespace stb
