#include "stb/filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <stdexcept>
#include <string>
#include <vector>

#include "image/math_kernels.hpp"
#include "stb/pixel_range.hpp"
#include "plane.hpp"
#include "stb/parallel.hpp"

namespace stb {
namespace {

using detail::math;
using detail::math_kernels;
using utils::parallel_for;

// Floors of what one core does (ns), so that a doubtful case stays on one thread, see stb/parallel.hpp.
// Not measured yet: they are low on purpose (a vectorised multiply-add is well below 0.1 ns per value).
constexpr double tap_ns = 0.1;   // one multiply-add of a filter, per value
constexpr double copy_ns = 0.3;  // moving or converting one value (padding a row, 8 bits <-> float)
constexpr double box_ns = 0.6;   // one step of a running sum, per value

// ----------------------------------------------------------------------------------- edges

using detail_plane::resolve;

// `row` (w pixels of c channels) with `r` pixels added on each side as the edge mode says: (w + 2r) * c values.
void pad_row(const float* row, int w, int c, int r, edge_mode mode, float* out) {
    const auto side = [&](int from, int to) {
        for (int x = from; x < to; ++x) {
            const int sx = resolve(x, w, mode);
            if (sx < 0) {
                for (int k = 0; k < c; ++k) *out++ = 0.0f;
            } else {
                const float* s = row + std::size_t(sx) * std::size_t(c);
                for (int k = 0; k < c; ++k) *out++ = s[k];
            }
        }
    };
    side(-r, 0);
    std::memcpy(out, row, std::size_t(w) * std::size_t(c) * sizeof(float));
    out += std::size_t(w) * std::size_t(c);
    side(w, w + r);
}

// ----------------------------------------------------------------------------------- row sources and sinks
// A filter reads rows as floats and writes rows as floats. These four adapters are the only place that knows
// whether the array is 8-bit or float: an 8-bit row is converted into a scratch row on the way in, and a
// result row is converted on the way out, so no full-size float copy of an 8-bit image is made.

struct f32_in {
    const float* d;
    std::size_t stride;
    const math_kernels* k;
    const float* row(int y, float*) const { return d + std::size_t(y) * stride; }
};
struct u8_in {
    const std::uint8_t* d;
    std::size_t stride;
    const math_kernels* k;
    const float* row(int y, float* scratch) const {
        k->u8_to_f32(d + std::size_t(y) * stride, scratch, stride);
        return scratch;
    }
};
struct f32_out {
    float* d;
    std::size_t stride;
    const math_kernels* k;
    float* acquire(int y, float*) const { return d + std::size_t(y) * stride; }
    void commit(int, const float*) const {}
};
struct u8_out {
    std::uint8_t* d;
    std::size_t stride;
    const math_kernels* k;
    float* acquire(int, float* scratch) const { return scratch; }
    void commit(int y, const float* scratch) const { k->f32_to_u8(scratch, d + std::size_t(y) * stride, stride); }
};

f32_in in_of(const mat32f& m, const math_kernels& k) { return {m.data(), m.stride(), &k}; }
u8_in in_of(const mat8u& m, const math_kernels& k) { return {m.data(), m.stride(), &k}; }
f32_out out_of(mat32f& m, const math_kernels& k) { return {m.data(), m.stride(), &k}; }
u8_out out_of(image8u& m, const math_kernels& k) { return {m.data(), m.stride(), &k}; }

// ----------------------------------------------------------------------------------- kernels

struct taps {
    const float* p;
    int n;
};

// The taps of a separable kernel: one channel, a width or a height of 1, an odd length.
taps taps_of(const mat32f& k, const char* what) {
    if (k.empty()) throw std::invalid_argument(std::string(what) + " is empty");
    if (k.channels() != 1) throw std::invalid_argument(std::string(what) + " must have one channel");
    if (k.width() != 1 && k.height() != 1)
        throw std::invalid_argument(std::string(what) + " must be a row or a column (width or height 1)");
    const int n = int(k.size());
    if (n % 2 == 0) throw std::invalid_argument(std::string(what) + " must have an odd number of taps");
    return {k.data(), n};
}

// ----------------------------------------------------------------------------------- separable

// out = col_kernel * (row_kernel * in), per channel. The horizontal pass fills a full float image (the one
// temporary), the vertical pass reads it and writes the result rows. Both passes are split over rows.
template <class In, class Out>
void run_separable(const In& in, const Out& out, int w, int h, int c, taps hk, taps vk, edge_mode edge) {
    const math_kernels& k = *in.k;
    const std::size_t stride = std::size_t(w) * std::size_t(c);
    const int hr = hk.n / 2, vr = vk.n / 2;
    // A vertical kernel of one tap that is 1 changes nothing: the horizontal pass can write the result.
    const bool v_identity = vk.n == 1 && vk.p[0] == 1.0f;
    std::vector<float> tmp;
    float* tmpp = nullptr;
    if (!v_identity) {
        tmp.resize(std::size_t(h) * stride);
        tmpp = tmp.data();
    }

    parallel_for(std::size_t(h), double(stride) * (double(hk.n) * tap_ns + 2 * copy_ns), [&](std::size_t b, std::size_t e) {
        std::vector<float> inbuf(stride), outbuf(stride), pad(std::size_t(w + 2 * hr) * std::size_t(c));
        for (std::size_t y = b; y < e; ++y) {
            const float* row = in.row(int(y), inbuf.data());
            pad_row(row, w, c, hr, edge, pad.data());
            float* dst = tmpp ? tmpp + y * stride : out.acquire(int(y), outbuf.data());
            k.fir_h(pad.data(), dst, stride, hk.p, hk.n, std::size_t(c));
            if (!tmpp) out.commit(int(y), dst);
        }
    });
    if (!tmpp) return;

    parallel_for(std::size_t(h), double(stride) * (double(vk.n) * tap_ns + copy_ns), [&](std::size_t b, std::size_t e) {
        std::vector<const float*> rows(std::size_t(vk.n));
        std::vector<float> outbuf(stride), zero;
        if (edge == edge_mode::zero) zero.assign(stride, 0.0f);
        for (std::size_t y = b; y < e; ++y) {
            for (int i = 0; i < vk.n; ++i) {
                const int ry = resolve(int(y) + i - vr, h, edge);
                rows[std::size_t(i)] = ry < 0 ? zero.data() : tmpp + std::size_t(ry) * stride;
            }
            float* dst = out.acquire(int(y), outbuf.data());
            k.fir_v(rows.data(), dst, stride, vk.p, vk.n);
            out.commit(int(y), dst);
        }
    });
}

// ----------------------------------------------------------------------------------- full 2-D kernel

// Each output row is the sum, over the kernel's rows, of a horizontal filter of the matching input row.
template <class In, class Out>
void run_2d(const In& in, const Out& out, int w, int h, int c, const mat32f& kern, edge_mode edge) {
    const math_kernels& k = *in.k;
    const std::size_t stride = std::size_t(w) * std::size_t(c);
    const int kw = kern.width(), kh = kern.height(), rx = kw / 2, ry = kh / 2;
    const float* kd = kern.data();

    parallel_for(std::size_t(h), double(stride) * (double(kw) * double(kh) * tap_ns + double(kh) * copy_ns),
                 [&](std::size_t b, std::size_t e) {
        std::vector<float> inbuf(stride), outbuf(stride), part(stride), pad(std::size_t(w + 2 * rx) * std::size_t(c));
        for (std::size_t y = b; y < e; ++y) {
            float* acc = out.acquire(int(y), outbuf.data());
            bool first = true;
            for (int ky = 0; ky < kh; ++ky) {
                const int sy = resolve(int(y) + ky - ry, h, edge);
                if (sy < 0) continue;  // a zero row adds nothing
                const float* row = in.row(sy, inbuf.data());
                pad_row(row, w, c, rx, edge, pad.data());
                k.fir_h(pad.data(), first ? acc : part.data(), stride, kd + std::size_t(ky) * std::size_t(kw), kw,
                        std::size_t(c));
                if (!first) k.add(part.data(), acc, stride);
                first = false;
            }
            if (first) std::fill(acc, acc + stride, 0.0f);
            out.commit(int(y), acc);
        }
    });
}

void check_kernel_2d(const mat32f& kern) {
    if (kern.empty()) throw std::invalid_argument("kernel is empty");
    if (kern.channels() != 1) throw std::invalid_argument("kernel must have one channel");
    if (kern.width() % 2 == 0 || kern.height() % 2 == 0)
        throw std::invalid_argument("kernel width and height must be odd");
}

// The same, for both kinds of input and output.
mat32f separable_f32(const mat32f& src, taps hk, taps vk, edge_mode edge) {
    if (src.empty()) return mat32f();
    const math_kernels& k = math();
    mat32f out(src.width(), src.height(), src.channels());
    run_separable(in_of(src, k), out_of(out, k), src.width(), src.height(), src.channels(), hk, vk, edge);
    return out;
}
image8u separable_u8(const mat8u& src, taps hk, taps vk, edge_mode edge) {
    if (src.empty()) return image8u();
    const math_kernels& k = math();
    image8u out(src.width(), src.height(), pixel_format_from_channels(src.channels()));
    run_separable(in_of(src, k), out_of(out, k), src.width(), src.height(), src.channels(), hk, vk, edge);
    return out;
}

}  // namespace

// ----------------------------------------------------------------------------------- public

mat32f gaussian_kernel(float sigma, int radius) {
    if (!(sigma > 0.0f) || !std::isfinite(sigma)) throw std::invalid_argument("sigma must be a positive finite number");
    if (radius < 0) throw std::invalid_argument("radius must be >= 0");
    if (radius == 0) {
        const double r = std::ceil(3.0 * double(sigma));
        if (r > 1.0e5) throw std::invalid_argument("sigma is too large");
        radius = std::max(1, int(r));
    }
    if (radius > 100000) throw std::invalid_argument("radius is too large");
    const int n = 2 * radius + 1;
    std::vector<double> w(static_cast<std::size_t>(n));
    double sum = 0.0;
    const double two_sigma2 = 2.0 * double(sigma) * double(sigma);
    for (int i = 0; i < n; ++i) {
        const double x = double(i - radius);
        w[std::size_t(i)] = std::exp(-(x * x) / two_sigma2);
        sum += w[std::size_t(i)];
    }
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) v[std::size_t(i)] = float(w[std::size_t(i)] / sum);
    return mat32f(n, 1, 1, std::move(v));
}

mat32f convolve(const mat32f& src, const mat32f& kernel, edge_mode edge) {
    check_kernel_2d(kernel);
    if (src.empty()) return mat32f();
    const math_kernels& k = math();
    mat32f out(src.width(), src.height(), src.channels());
    run_2d(in_of(src, k), out_of(out, k), src.width(), src.height(), src.channels(), kernel, edge);
    return out;
}

image8u convolve(const mat8u& src, const mat32f& kernel, edge_mode edge) {
    check_kernel_2d(kernel);
    if (src.empty()) return image8u();
    const math_kernels& k = math();
    image8u out(src.width(), src.height(), pixel_format_from_channels(src.channels()));
    run_2d(in_of(src, k), out_of(out, k), src.width(), src.height(), src.channels(), kernel, edge);
    return out;
}

mat32f convolve(const mat32f& src, const mat32f& row_kernel, const mat32f& col_kernel, edge_mode edge) {
    return separable_f32(src, taps_of(row_kernel, "row kernel"), taps_of(col_kernel, "column kernel"), edge);
}
image8u convolve(const mat8u& src, const mat32f& row_kernel, const mat32f& col_kernel, edge_mode edge) {
    return separable_u8(src, taps_of(row_kernel, "row kernel"), taps_of(col_kernel, "column kernel"), edge);
}

mat32f gaussian_blur(const mat32f& src, float sigma, edge_mode edge) {
    const mat32f g = gaussian_kernel(sigma);
    return separable_f32(src, taps_of(g, "kernel"), taps_of(g, "kernel"), edge);
}
image8u gaussian_blur(const mat8u& src, float sigma, edge_mode edge) {
    const mat32f g = gaussian_kernel(sigma);
    return separable_u8(src, taps_of(g, "kernel"), taps_of(g, "kernel"), edge);
}

// ---- box blur: running sums in double, so the cost is the same for every radius and the rounding of
// a long window does not pile up. Sequential along the window, so the work is split over rows (horizontal
// pass) and over columns (vertical pass); each value is still computed in one fixed order.
mat32f box_blur(const mat32f& src, int radius, edge_mode edge) {
    if (radius < 0) throw std::invalid_argument("radius must be >= 0");
    if (src.empty()) return mat32f();
    if (radius == 0) return src.copy();
    const int w = src.width(), h = src.height(), c = src.channels();
    const std::size_t stride = src.stride();
    const int n = 2 * radius + 1;
    const double inv = 1.0 / double(n);
    mat32f tmp(w, h, c), out(w, h, c);

    parallel_for(std::size_t(h), double(stride) * (box_ns + copy_ns), [&](std::size_t b, std::size_t e) {
        std::vector<float> pad(std::size_t(w + 2 * radius) * std::size_t(c));
        std::vector<double> acc(static_cast<std::size_t>(c));
        for (std::size_t y = b; y < e; ++y) {
            pad_row(src.row(int(y)), w, c, radius, edge, pad.data());
            float* d = tmp.row(int(y));
            std::fill(acc.begin(), acc.end(), 0.0);
            for (int i = 0; i < n; ++i)
                for (int k = 0; k < c; ++k) acc[std::size_t(k)] += double(pad[std::size_t(i) * std::size_t(c) + std::size_t(k)]);
            for (int x = 0; x < w; ++x) {
                for (int k = 0; k < c; ++k) d[std::size_t(x) * std::size_t(c) + std::size_t(k)] = float(acc[std::size_t(k)] * inv);
                if (x + 1 < w)
                    for (int k = 0; k < c; ++k)
                        acc[std::size_t(k)] += double(pad[std::size_t(x + n) * std::size_t(c) + std::size_t(k)]) -
                                               double(pad[std::size_t(x) * std::size_t(c) + std::size_t(k)]);
            }
        }
    });

    parallel_for(stride, double(h) * box_ns, [&](std::size_t c0, std::size_t c1) {
        const std::size_t len = c1 - c0;
        std::vector<double> acc(len, 0.0);
        std::vector<float> zero;
        if (edge == edge_mode::zero) zero.assign(stride, 0.0f);
        const auto rowp = [&](int y) -> const float* {
            const int ry = resolve(y, h, edge);
            return ry < 0 ? zero.data() : tmp.row(ry);
        };
        for (int i = 0; i < n; ++i) {
            const float* r = rowp(i - radius) + c0;
            for (std::size_t j = 0; j < len; ++j) acc[j] += double(r[j]);
        }
        for (int y = 0; y < h; ++y) {
            float* d = out.row(y) + c0;
            for (std::size_t j = 0; j < len; ++j) d[j] = float(acc[j] * inv);
            if (y + 1 < h) {
                const float* add = rowp(y + 1 + radius) + c0;
                const float* sub = rowp(y - radius) + c0;
                for (std::size_t j = 0; j < len; ++j) acc[j] += double(add[j]) - double(sub[j]);
            }
        }
    });
    return out;
}

image8u box_blur(const mat8u& src, int radius, edge_mode edge) {
    if (radius < 0) throw std::invalid_argument("radius must be >= 0");
    if (src.empty()) return image8u();
    return cast<std::uint8_t>(box_blur(cast<float>(src), radius, edge));
}

gradients sobel(const mat32f& src, edge_mode edge) {
    static const float diff[3] = {-1.0f, 0.0f, 1.0f};
    static const float smooth[3] = {1.0f, 2.0f, 1.0f};
    gradients g;
    g.dx = separable_f32(src, {diff, 3}, {smooth, 3}, edge);
    g.dy = separable_f32(src, {smooth, 3}, {diff, 3}, edge);
    return g;
}

gradients sobel(const mat8u& src, edge_mode edge) {
    if (src.empty()) return gradients();
    return sobel(cast<float>(src), edge);
}

gradients scharr(const mat32f& src, edge_mode edge) {
    static const float diff[3] = {-1.0f, 0.0f, 1.0f};
    static const float smooth[3] = {3.0f, 10.0f, 3.0f};
    gradients g;
    g.dx = separable_f32(src, {diff, 3}, {smooth, 3}, edge);
    g.dy = separable_f32(src, {smooth, 3}, {diff, 3}, edge);
    return g;
}

gradients scharr(const mat8u& src, edge_mode edge) {
    if (src.empty()) return gradients();
    return scharr(cast<float>(src), edge);
}

mat32f magnitude(const gradients& g) {
    if (g.dx.empty() && g.dy.empty()) return mat32f();
    if (g.dx.width() != g.dy.width() || g.dx.height() != g.dy.height() || g.dx.channels() != g.dy.channels())
        throw std::invalid_argument("dx and dy must have the same size and channels");
    mat32f out(g.dx.width(), g.dx.height(), g.dx.channels());
    const math_kernels& k = math();
    const float* dx = g.dx.data();
    const float* dy = g.dy.data();
    float* d = out.data();
    parallel_for(out.size(), copy_ns, [&](std::size_t b, std::size_t e) { k.magnitude(dx + b, dy + b, d + b, e - b); });
    return out;
}


// ============================================================================================ median

namespace {

constexpr double median_u8_ns = 0.5;    // one step of the sliding histogram, per value and per row of the window
constexpr double median_f32_ns = 1.5;   // one value of a window handed to nth_element, per value of the window

// The median of every window of one 8-bit plane, by a histogram that slides along the row: a step to the right
// removes one column of the window and adds one, and the median (the value `m` with `below` values under it) moves
// to where it belongs. The cost per value hardly depends on the radius.
void median_plane(const std::vector<std::uint8_t>& plane, int w, int h, int c, int k, int r, std::uint8_t* out) {
    const std::size_t pw = std::size_t(w) + 2 * std::size_t(r);
    const int side = 2 * r + 1;
    const int target = (side * side) / 2;  // the index of the median among the sorted values of a window
    parallel_for(std::size_t(h), double(w) * double(side) * median_u8_ns, [&](std::size_t b, std::size_t e) {
        int hist[256];
        for (std::size_t y = b; y < e; ++y) {
            std::fill(hist, hist + 256, 0);
            const std::uint8_t* top = plane.data() + y * pw;
            for (int ry = 0; ry < side; ++ry)
                for (int x = 0; x < side; ++x) ++hist[top[std::size_t(ry) * pw + std::size_t(x)]];
            int m = 0, below = 0;
            while (below + hist[m] <= target) below += hist[m++];
            std::uint8_t* dst = out + y * std::size_t(w) * std::size_t(c) + std::size_t(k);
            dst[0] = std::uint8_t(m);
            for (int x = 1; x < w; ++x) {
                for (int ry = 0; ry < side; ++ry) {
                    const std::uint8_t* line = top + std::size_t(ry) * pw;
                    const int gone = line[x - 1], came = line[x + side - 1];
                    --hist[gone];
                    if (gone < m) --below;
                    ++hist[came];
                    if (came < m) ++below;
                }
                while (below > target) below -= hist[--m];
                while (below + hist[m] <= target) below += hist[m++];
                dst[std::size_t(x) * std::size_t(c)] = std::uint8_t(m);
            }
        }
    });
}

// The same for floats: the window is gathered and the middle one is selected (NaNs sort last).
void median_plane(const std::vector<float>& plane, int w, int h, int c, int k, int r, float* out) {
    const std::size_t pw = std::size_t(w) + 2 * std::size_t(r);
    const int side = 2 * r + 1;
    const std::size_t n = std::size_t(side) * std::size_t(side);
    parallel_for(std::size_t(h), double(w) * double(n) * median_f32_ns, [&](std::size_t b, std::size_t e) {
        std::vector<float> win(n);
        for (std::size_t y = b; y < e; ++y) {
            for (int x = 0; x < w; ++x) {
                std::size_t i = 0;
                for (int ry = 0; ry < side; ++ry) {
                    const float* line = plane.data() + (y + std::size_t(ry)) * pw + std::size_t(x);
                    for (int rx = 0; rx < side; ++rx) win[i++] = line[rx];
                }
                std::nth_element(win.begin(), win.begin() + std::ptrdiff_t(n / 2), win.end(),
                                 [](float a, float b2) { return a < b2 || (b2 != b2 && a == a); });
                out[y * std::size_t(w) * std::size_t(c) + std::size_t(x) * std::size_t(c) + std::size_t(k)] = win[n / 2];
            }
        }
    });
}

template <class T>
void median_into(const mat<T>& src, int radius, edge_mode edge, T* out) {
    for (int k = 0; k < src.channels(); ++k)
        median_plane(detail_plane::padded_plane(src, k, radius, radius, edge), src.width(), src.height(), src.channels(), k, radius, out);
}

void check_radius(int radius) {
    if (radius < 1 || radius > 1000) throw std::invalid_argument("radius must be in 1..1000");
}

}  // namespace

mat32f median_filter(const mat32f& src, int radius, edge_mode edge) {
    check_radius(radius);
    if (src.empty()) return mat32f();
    mat32f out(src.width(), src.height(), src.channels());
    median_into(src, radius, edge, out.data());
    return out;
}

image8u median_filter(const mat8u& src, int radius, edge_mode edge) {
    check_radius(radius);
    if (src.empty()) return image8u();
    image8u out(src.width(), src.height(), pixel_format_from_channels(src.channels()));
    median_into(src, radius, edge, out.data());
    return out;
}

// ============================================================================================ bilateral

namespace {

constexpr double bilateral_ns = 1.2;  // one tap of the window (weights, difference, sums), per colour channel and tap

void check_bilateral(int radius, float sigma_space, float sigma_range) {
    if (radius < 0 || radius > 1000) throw std::invalid_argument("radius must be in 0..1000");
    if (!(sigma_space > 0.0f) || !std::isfinite(sigma_space)) throw std::invalid_argument("sigma_space must be a positive finite number");
    if (!(sigma_range > 0.0f) || !std::isfinite(sigma_range)) throw std::invalid_argument("sigma_range must be a positive finite number");
}

// The weight of each tap of the window by its distance: exp(-(dx^2 + dy^2) / (2 sigma^2)), row by row.
std::vector<float> bilateral_space_weights(int r, float sigma) {
    const int side = 2 * r + 1;
    const double inv = 1.0 / (2.0 * double(sigma) * double(sigma));
    std::vector<float> ws(std::size_t(side) * std::size_t(side));
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) ws[std::size_t(dy + r) * std::size_t(side) + std::size_t(dx + r)] = float(std::exp(-double(dx * dx + dy * dy) * inv));
    return ws;
}

// Every colour channel of a pixel is changed by the weighted mean of the window, where a tap weighs
// exp(-distance^2 / (2 sigma_space^2)) * exp(-d^2 / (2 sigma_range^2)) and d is the distance of the two pixels as colours
// (all colour channels together, in units of the full range: a level divided by the maximum of the type). The alpha channel is
// copied. On floats a tap with a NaN is left out and a NaN pixel stays NaN.
//
// An integer type keeps the squared distance as an integer, in levels squared, and takes the range weight from a table of every
// possible value when the table is small (8 bits: 195076 entries); a type with more levels (16 bits) evaluates it for every tap.
template <class T>
void bilateral_run(const mat<T>& src, mat<T>& out, int r, float sigma_space, float sigma_range, edge_mode edge) {
    using px_range = pixel_range<T>;
    const int w = src.width(), h = src.height(), c = src.channels();
    const int colour = (c == 2 || c == 4) ? c - 1 : c;
    const int side = 2 * r + 1;
    const std::size_t pw = std::size_t(w) + 2 * std::size_t(r);
    const std::vector<float> ws = bilateral_space_weights(r, sigma_space);
    const double inv_range = 1.0 / (2.0 * double(sigma_range) * double(sigma_range));
    const double max2 = double(px_range::max) * double(px_range::max);  // the largest squared distance of one channel, in levels

    constexpr std::size_t table_limit = std::size_t(1) << 20;  // entries
    const bool use_table = px_range::is_integer && double(colour) * max2 + 1.0 <= double(table_limit);
    std::vector<float> lut;  // the range weight of every possible squared distance (in levels squared)
    if (use_table) {
        lut.resize(std::size_t(colour) * std::size_t(max2) + 1u);
        for (std::size_t d = 0; d < lut.size(); ++d) lut[d] = float(std::exp(-(double(d) / max2) * inv_range));
    }
    const float inv_range_f = float(inv_range);

    std::vector<std::vector<T>> planes;
    planes.reserve(std::size_t(colour));
    for (int k = 0; k < colour; ++k) planes.push_back(detail_plane::padded_plane(src, k, r, r, edge));

    parallel_for(std::size_t(h), double(w) * double(side) * double(side) * double(colour + 1) * bilateral_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t y = b; y < e; ++y) {
            const T* in = src.row(int(y));
            T* o = out.row(int(y));
            for (int x = 0; x < w; ++x) {
                T* px = o + std::size_t(x) * std::size_t(c);
                for (int k = colour; k < c; ++k) px[k] = in[std::size_t(x) * std::size_t(c) + std::size_t(k)];  // alpha
                const std::size_t mid = (y + std::size_t(r)) * pw + std::size_t(x) + std::size_t(r);
                T centre[4];
                bool centre_nan = false;
                for (int k = 0; k < colour; ++k) {
                    centre[k] = planes[std::size_t(k)][mid];
                    if constexpr (!px_range::is_integer) centre_nan = centre_nan || centre[k] != centre[k];
                }
                if (centre_nan) {
                    for (int k = 0; k < colour; ++k) px[k] = std::numeric_limits<T>::quiet_NaN();
                    continue;
                }
                float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                float wsum = 0.0f;
                for (int dy = 0; dy < side; ++dy) {
                    const std::size_t row = (y + std::size_t(dy)) * pw + std::size_t(x);
                    for (int dx = 0; dx < side; ++dx) {
                        const std::size_t idx = row + std::size_t(dx);
                        T v[4];
                        std::conditional_t<px_range::is_integer, std::int64_t, float> d2 = 0;
                        bool skip = false;
                        for (int k = 0; k < colour; ++k) {
                            v[k] = planes[std::size_t(k)][idx];
                            if constexpr (px_range::is_integer) {
                                const std::int64_t d = std::int64_t(v[k]) - std::int64_t(centre[k]);
                                d2 += d * d;
                            } else {
                                skip = skip || v[k] != v[k];
                                const float d = v[k] - centre[k];
                                d2 += d * d;
                            }
                        }
                        if (skip) continue;
                        float rw;
                        if constexpr (px_range::is_integer) rw = use_table ? lut[std::size_t(d2)] : float(std::exp(-(double(d2) / max2) * inv_range));
                        else rw = std::exp(-d2 * inv_range_f);
                        const float wgt = ws[std::size_t(dy) * std::size_t(side) + std::size_t(dx)] * rw;
                        wsum += wgt;
                        for (int k = 0; k < colour; ++k) acc[k] += wgt * float(v[k]);
                    }
                }
                for (int k = 0; k < colour; ++k) px[k] = px_range::store(acc[k] / wsum);  // the middle tap alone weighs 1, so wsum >= 1
            }
        }
    });
}

}  // namespace

mat32f bilateral_filter(const mat32f& src, int radius, float sigma_space, float sigma_range, edge_mode edge) {
    check_bilateral(radius, sigma_space, sigma_range);
    if (src.empty()) return mat32f();
    if (src.channels() > 4) throw std::invalid_argument("expected an image of 1 to 4 channels (gray, gray + alpha, RGB, RGBA)");
    if (radius == 0) return src.copy();
    mat32f out(src.width(), src.height(), src.channels());
    bilateral_run(src, out, radius, sigma_space, sigma_range, edge);
    return out;
}

image8u bilateral_filter(const mat8u& src, int radius, float sigma_space, float sigma_range, edge_mode edge) {
    check_bilateral(radius, sigma_space, sigma_range);
    if (src.empty()) return image8u();
    if (src.channels() > 4) throw std::invalid_argument("expected an image of 1 to 4 channels (gray, gray + alpha, RGB, RGBA)");
    if (radius == 0) return image8u(src.copy());
    image8u out(src.width(), src.height(), pixel_format_from_channels(src.channels()));
    bilateral_run<std::uint8_t>(src, out, radius, sigma_space, sigma_range, edge);
    return out;
}

// ============================================================================================ guided

namespace {

constexpr double guided_ns = 2.0;  // one step of the per-pixel algebra, per value

void check_guided(int radius, float eps) {
    if (radius < 0 || radius > 1000) throw std::invalid_argument("radius must be in 0..1000");
    if (!(eps > 0.0f) || !std::isfinite(eps)) throw std::invalid_argument("eps must be a positive finite number");
}

int guided_colour(int channels) {  // the colour channels of an image: all but the alpha of gray + alpha and RGBA
    if (channels < 1 || channels > 4) throw std::invalid_argument("expected an image of 1 to 4 channels (gray, gray + alpha, RGB, RGBA)");
    return (channels == 2 || channels == 4) ? channels - 1 : channels;
}

template <class F>
void guided_each(std::size_t n, F&& f) {
    parallel_for(n, guided_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i) f(i);
    });
}

// The guided filter of He, Sun and Tang: in every (2 r + 1)-square the output is a linear function of the guide,
// q = a . I + b, with a and b the least squares fit of the input p by the guide plus a penalty eps on a; the a and b
// of the squares that cover a pixel are averaged. Every plane is filtered with the same guide (1 or 3 colour channels).
mat32f guided_run(const mat32f& src, const mat32f& guide, int r, float eps, edge_mode edge) {
    const int w = src.width(), h = src.height(), c = src.channels();
    const int ck = guided_colour(c), cg = guided_colour(guide.channels());
    const std::size_t n = std::size_t(w) * std::size_t(h);
    const auto plane_of = [&](const mat32f& m, int k) {
        mat32f p(w, h, 1);
        const float* in = m.data();
        float* d = p.data();
        const std::size_t mc = std::size_t(m.channels());
        guided_each(n, [&](std::size_t i) { d[i] = in[i * mc + std::size_t(k)]; });
        return p;
    };
    const auto box = [&](const mat32f& m) { return box_blur(m, r, edge); };
    const auto prod = [&](const mat32f& x, const mat32f& y) {
        mat32f p(w, h, 1);
        const float *a = x.data(), *b = y.data();
        float* d = p.data();
        guided_each(n, [&](std::size_t i) { d[i] = a[i] * b[i]; });
        return p;
    };

    mat32f out(w, h, c);
    if (ck < c) {  // alpha is copied
        const float* in = src.data();
        float* o = out.data();
        guided_each(n, [&](std::size_t i) { o[i * std::size_t(c) + std::size_t(c - 1)] = in[i * std::size_t(c) + std::size_t(c - 1)]; });
    }
    const auto put = [&](const mat32f& q, int k) {
        const float* in = q.data();
        float* o = out.data();
        guided_each(n, [&](std::size_t i) { o[i * std::size_t(c) + std::size_t(k)] = in[i]; });
    };

    std::vector<mat32f> g, mg;  // the guide's planes and their means
    for (int j = 0; j < cg; ++j) {
        g.push_back(plane_of(guide, j));
        mg.push_back(box(g.back()));
    }

    if (cg == 1) {
        const mat32f mgg = box(prod(g[0], g[0]));
        mat32f den(w, h, 1);  // the variance of the guide, plus eps
        {
            const float *m2 = mgg.data(), *m1 = mg[0].data();
            float* d = den.data();
            guided_each(n, [&](std::size_t i) { d[i] = m2[i] - m1[i] * m1[i] + eps; });
        }
        for (int k = 0; k < ck; ++k) {
            const mat32f p = plane_of(src, k);
            const mat32f mp = box(p), mgp = box(prod(g[0], p));
            mat32f a(w, h, 1), b(w, h, 1);
            {
                const float *mgp_ = mgp.data(), *mg_ = mg[0].data(), *mp_ = mp.data(), *dn = den.data();
                float *ad = a.data(), *bd = b.data();
                guided_each(n, [&](std::size_t i) {
                    ad[i] = (mgp_[i] - mg_[i] * mp_[i]) / dn[i];
                    bd[i] = mp_[i] - ad[i] * mg_[i];
                });
            }
            const mat32f ma = box(a), mb = box(b);
            mat32f q(w, h, 1);
            {
                const float *ma_ = ma.data(), *mb_ = mb.data(), *g_ = g[0].data();
                float* qd = q.data();
                guided_each(n, [&](std::size_t i) { qd[i] = ma_[i] * g_[i] + mb_[i]; });
            }
            put(q, k);
        }
        return out;
    }

    // three guide channels: the covariance matrix of the guide plus eps on the diagonal, inverted at every pixel
    // (entries 00 01 02 11 12 22; the matrix is symmetric)
    mat32f cov[6];
    {
        const int pi[6] = {0, 0, 0, 1, 1, 2}, pj[6] = {0, 1, 2, 1, 2, 2};
        for (int e = 0; e < 6; ++e) {
            cov[e] = box(prod(g[std::size_t(pi[e])], g[std::size_t(pj[e])]));
            const float *m1 = mg[std::size_t(pi[e])].data(), *m2 = mg[std::size_t(pj[e])].data();
            float* d = cov[e].data();
            const float add = pi[e] == pj[e] ? eps : 0.0f;
            guided_each(n, [&](std::size_t i) { d[i] = d[i] - m1[i] * m2[i] + add; });
        }
    }
    mat32f inv[6];
    for (mat32f& m : inv) m = mat32f(w, h, 1);
    {
        const float *sa = cov[0].data(), *sb = cov[1].data(), *sc = cov[2].data(), *sd = cov[3].data(), *se = cov[4].data(), *sf = cov[5].data();
        float *i00 = inv[0].data(), *i01 = inv[1].data(), *i02 = inv[2].data(), *i11 = inv[3].data(), *i12 = inv[4].data(), *i22 = inv[5].data();
        guided_each(n, [&](std::size_t i) {
            const float a = sa[i], b = sb[i], cc = sc[i], d = sd[i], e = se[i], f = sf[i];
            const float c00 = d * f - e * e, c01 = cc * e - b * f, c02 = b * e - cc * d;
            const float c11 = a * f - cc * cc, c12 = b * cc - a * e, c22 = a * d - b * b;
            const float det = a * c00 + b * c01 + cc * c02;  // > 0: the matrix is positive definite thanks to eps
            const float idet = 1.0f / det;
            i00[i] = c00 * idet;
            i01[i] = c01 * idet;
            i02[i] = c02 * idet;
            i11[i] = c11 * idet;
            i12[i] = c12 * idet;
            i22[i] = c22 * idet;
        });
    }
    for (int k = 0; k < ck; ++k) {
        const mat32f p = plane_of(src, k);
        const mat32f mp = box(p);
        mat32f cp[3];
        for (int j = 0; j < 3; ++j) {
            cp[j] = box(prod(g[std::size_t(j)], p));
            const float *m1 = mg[std::size_t(j)].data(), *m2 = mp.data();
            float* d = cp[j].data();
            guided_each(n, [&](std::size_t i) { d[i] -= m1[i] * m2[i]; });
        }
        mat32f a[3], b(w, h, 1);
        for (mat32f& m : a) m = mat32f(w, h, 1);
        {
            const float *i00 = inv[0].data(), *i01 = inv[1].data(), *i02 = inv[2].data(), *i11 = inv[3].data(), *i12 = inv[4].data(), *i22 = inv[5].data();
            const float *c0 = cp[0].data(), *c1 = cp[1].data(), *c2 = cp[2].data(), *mp_ = mp.data();
            const float *m0 = mg[0].data(), *m1 = mg[1].data(), *m2 = mg[2].data();
            float *a0 = a[0].data(), *a1 = a[1].data(), *a2 = a[2].data(), *bd = b.data();
            guided_each(n, [&](std::size_t i) {
                a0[i] = i00[i] * c0[i] + i01[i] * c1[i] + i02[i] * c2[i];
                a1[i] = i01[i] * c0[i] + i11[i] * c1[i] + i12[i] * c2[i];
                a2[i] = i02[i] * c0[i] + i12[i] * c1[i] + i22[i] * c2[i];
                bd[i] = mp_[i] - a0[i] * m0[i] - a1[i] * m1[i] - a2[i] * m2[i];
            });
        }
        const mat32f ma0 = box(a[0]), ma1 = box(a[1]), ma2 = box(a[2]), mb = box(b);
        mat32f q(w, h, 1);
        {
            const float *x0 = ma0.data(), *x1 = ma1.data(), *x2 = ma2.data(), *y = mb.data();
            const float *g0 = g[0].data(), *g1 = g[1].data(), *g2 = g[2].data();
            float* qd = q.data();
            guided_each(n, [&](std::size_t i) { qd[i] = x0[i] * g0[i] + x1[i] * g1[i] + x2[i] * g2[i] + y[i]; });
        }
        put(q, k);
    }
    return out;
}

void check_guided_pair(const mat32f& src, const mat32f& guide) {
    if (guide.width() != src.width() || guide.height() != src.height())
        throw std::invalid_argument("the guide must be as wide and as high as the image");
    guided_colour(guide.channels());
}

}  // namespace

mat32f guided_filter(const mat32f& src, int radius, float eps, edge_mode edge) {
    check_guided(radius, eps);
    if (src.empty()) return mat32f();
    guided_colour(src.channels());
    if (radius == 0) return src.copy();
    return guided_run(src, src, radius, eps, edge);
}

mat32f guided_filter(const mat32f& src, const mat32f& guide, int radius, float eps, edge_mode edge) {
    check_guided(radius, eps);
    if (src.empty()) {
        if (!guide.empty()) throw std::invalid_argument("the guide must be as wide and as high as the image");
        return mat32f();
    }
    guided_colour(src.channels());
    check_guided_pair(src, guide);
    if (radius == 0) return src.copy();
    return guided_run(src, guide, radius, eps, edge);
}

image8u guided_filter(const mat8u& src, int radius, float eps, edge_mode edge) {
    check_guided(radius, eps);
    if (src.empty()) return image8u();
    guided_colour(src.channels());
    if (radius == 0) return image8u(src.copy());
    const mat32f f = cast<float>(src);
    return cast<std::uint8_t>(guided_run(f, f, radius, eps, edge));
}

image8u guided_filter(const mat8u& src, const mat8u& guide, int radius, float eps, edge_mode edge) {
    check_guided(radius, eps);
    if (src.empty()) {
        if (!guide.empty()) throw std::invalid_argument("the guide must be as wide and as high as the image");
        return image8u();
    }
    guided_colour(src.channels());
    if (guide.width() != src.width() || guide.height() != src.height())
        throw std::invalid_argument("the guide must be as wide and as high as the image");
    guided_colour(guide.channels());
    if (radius == 0) return image8u(src.copy());
    return cast<std::uint8_t>(guided_run(cast<float>(src), cast<float>(guide), radius, eps, edge));
}

// ============================================================================================ unsharp mask, pixelize

namespace {

constexpr double sharpen_ns = 1.0;   // the combination of one value, per value
constexpr double pixelize_ns = 1.0;  // reading and writing one value, per value

inline float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }  // NaN passes

// 1 gray, 2 gray + alpha, 3 RGB, 4 RGBA: how many channels are colour. Throws for any other count.
int colour_channels(int channels) {
    if (channels < 1 || channels > 4)
        throw std::invalid_argument("expected an image of 1 to 4 channels (gray, gray + alpha, RGB, RGBA)");
    return (channels == 2 || channels == 4) ? channels - 1 : channels;
}

// What goes into one block of pixelize: sums in 64-bit integers for an integer type (exact) and in double for floats.
template <class T>
using block_sum_t = std::conditional_t<std::is_floating_point_v<T>, double, std::uint64_t>;

// The mean num / den, rounded to the nearest for integers.
template <class T>
T mean_value(block_sum_t<T> num, block_sum_t<T> den) {
    if constexpr (std::is_floating_point_v<T>) return T(num / den);
    else return T((2 * num + den) / (2 * den));
}

}  // namespace

template <class T>
traits::if_image_t<mat<T>, void> detail::unsharp_mask_inplace_mat(mat<T>& img, float sigma, float amount, float threshold,
                                                      edge_mode edge) {
    const mat32f g = gaussian_kernel(sigma);  // throws for a bad sigma
    if (!(amount >= 0.0f) || !std::isfinite(amount)) throw std::invalid_argument("amount must be a finite number >= 0");
    if (!(threshold >= 0.0f && threshold <= 1.0f)) throw std::invalid_argument("threshold must be in [0, 1]");
    if (img.empty()) return;
    const int c = img.channels(), colour = colour_channels(c);

    // `base` holds the values as floats and is where the result is made; `blur` is the blurred copy.
    mat32f converted;
    if constexpr (std::is_same_v<T, std::uint8_t>) converted = cast<float>(img);
    mat32f& base = [&]() -> mat32f& {
        if constexpr (std::is_same_v<T, std::uint8_t>) return converted;
        else return img;
    }();
    const mat32f blur = separable_f32(base, taps_of(g, "kernel"), taps_of(g, "kernel"), edge);

    const std::size_t pixels = std::size_t(img.width()) * std::size_t(img.height());
    float* d = base.data();
    const float* bl = blur.data();
    parallel_for(pixels, double(c) * sharpen_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t p = b; p < e; ++p) {
            const std::size_t o = p * std::size_t(c);
            for (int ch = 0; ch < colour; ++ch) {
                const float v = d[o + std::size_t(ch)];
                const float diff = v - bl[o + std::size_t(ch)];
                d[o + std::size_t(ch)] = clamp01(std::fabs(diff) < threshold ? v : v + amount * diff);
            }
            // the alpha channel (if any) keeps its value; for floats it is not clamped, as nothing here reads it as a level
        }
    });
    if constexpr (std::is_same_v<T, std::uint8_t>) math().f32_to_u8(base.data(), img.data(), img.size());
}

template <class T>
traits::if_image_t<mat<T>, void> detail::pixelize_inplace_mat(mat<T>& img, int block_w, int block_h) {
    if (block_w < 1) throw std::invalid_argument("block_w must be >= 1");
    if (block_h < 0) throw std::invalid_argument("block_h must be >= 0");
    if (block_h == 0) block_h = block_w;
    if (img.empty()) return;
    const int w = img.width(), h = img.height(), c = img.channels();
    const int colour = colour_channels(c);
    const bool alpha = colour < c;
    using sum_t = block_sum_t<T>;

    const std::size_t block_rows = (std::size_t(h) + std::size_t(block_h) - 1) / std::size_t(block_h);
    parallel_for(block_rows, double(block_h) * double(img.stride()) * pixelize_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t by = b; by < e; ++by) {
            const int y0 = int(by) * block_h, y1 = STB_MIN(h, y0 + block_h);
            for (int x0 = 0; x0 < w; x0 += block_w) {
                const int x1 = STB_MIN(w, x0 + block_w);
                sum_t weighted[3] = {0, 0, 0}, plain[3] = {0, 0, 0}, alpha_sum = 0;
                for (int y = y0; y < y1; ++y) {
                    const T* row = img.row(y);
                    for (int x = x0; x < x1; ++x) {
                        const T* px = row + std::size_t(x) * std::size_t(c);
                        const sum_t a = alpha ? sum_t(px[colour]) : sum_t(1);
                        for (int k = 0; k < colour; ++k) {
                            weighted[k] += sum_t(px[k]) * a;
                            plain[k] += sum_t(px[k]);
                        }
                        if (alpha) alpha_sum += a;
                    }
                }
                const sum_t count = sum_t(y1 - y0) * sum_t(x1 - x0);
                T value[4] = {};
                for (int k = 0; k < colour; ++k)
                    value[k] = (alpha && alpha_sum != sum_t(0)) ? mean_value<T>(weighted[k], alpha_sum) : mean_value<T>(plain[k], count);
                if (alpha) value[colour] = mean_value<T>(alpha_sum, count);
                for (int y = y0; y < y1; ++y) {
                    T* row = img.row(y);
                    for (int x = x0; x < x1; ++x)
                        for (int k = 0; k < c; ++k) row[std::size_t(x) * std::size_t(c) + std::size_t(k)] = value[k];
                }
            }
        }
    });
}

template STB_API void detail::unsharp_mask_inplace_mat<std::uint8_t>(mat<std::uint8_t>&, float, float, float, edge_mode);
template STB_API void detail::unsharp_mask_inplace_mat<float>(mat<float>&, float, float, float, edge_mode);
template STB_API void detail::pixelize_inplace_mat<std::uint8_t>(mat<std::uint8_t>&, int, int);
template STB_API void detail::pixelize_inplace_mat<float>(mat<float>&, int, int);

}  // namespace stb
