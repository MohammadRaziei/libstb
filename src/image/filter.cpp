#include "stb/filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "image/math_kernels.hpp"
#include "util/parallel.hpp"

namespace stb {
namespace {

using detail::math;
using detail::math_kernels;
using detail::parallel_for;

// Floors of what one core does (ns), so that a doubtful case stays on one thread, see util/parallel.hpp.
// Not measured yet: they are low on purpose (a vectorised multiply-add is well below 0.1 ns per value).
constexpr double tap_ns = 0.1;   // one multiply-add of a filter, per value
constexpr double copy_ns = 0.3;  // moving or converting one value (padding a row, 8 bits <-> float)
constexpr double box_ns = 0.6;   // one step of a running sum, per value

// ----------------------------------------------------------------------------------- edges

// The index that stands for i in [0, n) under the edge mode, or -1 where the mode says "zero".
int resolve(int i, int n, edge_mode mode) noexcept {
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
    return to_8u(box_blur(to_f32(src), radius, edge));
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
    return sobel(to_f32(src), edge);
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

}  // namespace stb
