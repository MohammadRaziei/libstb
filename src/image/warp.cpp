#include "stb/warp.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "stb/pixel_range.hpp"
#include "plane.hpp"
#include "stb/parallel.hpp"

namespace stb {
namespace {

using detail_plane::resolve;
using utils::parallel_for;

// One tap of the sampling kernel for one value, in ns: a floor, see stb/parallel.hpp. Not measured yet.
constexpr double warp_tap_ns = 1.5;

// ------------------------------------------------------------------------------------------------ 3 x 3 in doubles

struct m3 {
    double v[9];
};

m3 to_m3(const homography& h) noexcept {
    m3 r;
    for (int i = 0; i < 9; ++i) r.v[i] = double(h.m[i]);
    return r;
}

m3 multiply(const m3& a, const m3& b) noexcept {  // a * b
    m3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) s += a.v[i * 3 + k] * b.v[k * 3 + j];
            r.v[i * 3 + j] = s;
        }
    return r;
}

bool invert(const m3& a, m3& out) noexcept {
    const double* m = a.v;
    const double c00 = m[4] * m[8] - m[5] * m[7], c01 = m[5] * m[6] - m[3] * m[8], c02 = m[3] * m[7] - m[4] * m[6];
    const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    // The scale of the matrix decides what "no determinant" is: compare it with the size of the terms that make it up.
    const double scale = std::fabs(m[0] * c00) + std::fabs(m[1] * c01) + std::fabs(m[2] * c02);
    if (!(std::fabs(det) > scale * 1e-12) || !std::isfinite(det)) return false;
    const double id = 1.0 / det;
    out.v[0] = c00 * id;
    out.v[1] = (m[2] * m[7] - m[1] * m[8]) * id;
    out.v[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    out.v[3] = c01 * id;
    out.v[4] = (m[0] * m[8] - m[2] * m[6]) * id;
    out.v[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    out.v[6] = c02 * id;
    out.v[7] = (m[1] * m[6] - m[0] * m[7]) * id;
    out.v[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    return true;
}

homography from_m3(const m3& a) noexcept {
    homography h;
    // scaled so that m[8] is 1 where it can be: the form the transform is written in
    const double s = std::fabs(a.v[8]) > 1e-300 ? 1.0 / a.v[8] : 1.0;
    for (int i = 0; i < 9; ++i) h.m[i] = float(a.v[i] * s);
    return h;
}

// ------------------------------------------------------------------------------------------------ the kernels

// The weights of the four Catmull-Rom taps at the offset t in [0, 1) from the second tap.
inline void cubic_weights(float t, float w[4]) noexcept {
    const float t2 = t * t, t3 = t2 * t;
    w[0] = (-t3 + 2.0f * t2 - t) * 0.5f;
    w[1] = (3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f;
    w[2] = (-3.0f * t3 + 4.0f * t2 + t) * 0.5f;
    w[3] = (t3 - t2) * 0.5f;
}

template <class T>
inline T store_value(float v) noexcept {
    return pixel_range<T>::store(v);
}

template <class T>
mat<T> warp_run(const mat<T>& src, const homography& h, int ow, int oh, interpolation_method interpolation, edge_mode edge) {
    if (interpolation != interpolation_method::nearest && interpolation != interpolation_method::bilinear && interpolation != interpolation_method::bicubic)
        throw std::invalid_argument("interpolation must be nearest, bilinear or bicubic");
    if (ow < 0 || oh < 0) throw std::invalid_argument("the size of the result must not be negative");
    if (src.empty()) return mat<T>();
    const int c = src.channels();
    if (c < 1 || c > 4) throw std::invalid_argument("expected an image of 1 to 4 channels (gray, gray + alpha, RGB, RGBA)");
    if (ow == 0) ow = src.width();
    if (oh == 0) oh = src.height();
    if (std::int64_t(ow) * std::int64_t(oh) * std::int64_t(c) > (std::int64_t(1) << 31))
        throw std::invalid_argument("the result would be too large");
    m3 inv;
    if (!invert(to_m3(h), inv)) throw std::invalid_argument("the transform cannot be inverted");

    const int w = src.width(), hh = src.height();
    mat<T> out(ow, oh, c);

    // the pixel of the source an index pair names under the edge mode, or null for the zeros of edge_mode::zero
    const auto at = [&](int ix, int iy) -> const T* {
        const int rx = resolve(ix, w, edge), ry = resolve(iy, hh, edge);
        return rx < 0 || ry < 0 ? nullptr : src.row(ry) + std::size_t(rx) * std::size_t(c);
    };
    const int taps = interpolation == interpolation_method::nearest ? 1 : (interpolation == interpolation_method::bilinear ? 4 : 16);

    parallel_for(std::size_t(oh), double(ow) * double(c) * double(taps) * warp_tap_ns, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t yy = y0; yy < y1; ++yy) {
            T* o = out.row(int(yy));
            const double Y = double(yy) + 0.5;
            for (int x = 0; x < ow; ++x, o += c) {
                const double X = double(x) + 0.5;
                const double den = inv.v[6] * X + inv.v[7] * Y + inv.v[8];
                double sx = 0.0, sy = 0.0;
                bool inside = std::fabs(den) > 1e-12;
                if (inside) {
                    sx = (inv.v[0] * X + inv.v[1] * Y + inv.v[2]) / den;
                    sy = (inv.v[3] * X + inv.v[4] * Y + inv.v[5]) / den;
                    inside = std::fabs(sx) < 1.0e9 && std::fabs(sy) < 1.0e9;  // also false for NaN
                }
                if (!inside) {
                    for (int k = 0; k < c; ++k) o[k] = T(0);
                    continue;
                }
                if (interpolation == interpolation_method::nearest) {
                    const T* p = at(int(std::floor(sx)), int(std::floor(sy)));
                    for (int k = 0; k < c; ++k) o[k] = p ? p[k] : T(0);
                    continue;
                }
                const double u = sx - 0.5, v = sy - 0.5;  // the point in units of pixel centres
                const double fux = std::floor(u), fvy = std::floor(v);
                const int x0 = int(fux), y0i = int(fvy);
                const float fx = float(u - fux), fy = float(v - fvy);
                if (interpolation == interpolation_method::bilinear) {
                    const T* p00 = at(x0, y0i);
                    const T* p10 = at(x0 + 1, y0i);
                    const T* p01 = at(x0, y0i + 1);
                    const T* p11 = at(x0 + 1, y0i + 1);
                    for (int k = 0; k < c; ++k) {
                        const float a = p00 ? float(p00[k]) : 0.0f, b = p10 ? float(p10[k]) : 0.0f;
                        const float cc = p01 ? float(p01[k]) : 0.0f, d = p11 ? float(p11[k]) : 0.0f;
                        const float top = (1.0f - fx) * a + fx * b, bottom = (1.0f - fx) * cc + fx * d;
                        o[k] = store_value<T>((1.0f - fy) * top + fy * bottom);
                    }
                    continue;
                }
                float wx[4], wy[4];
                cubic_weights(fx, wx);
                cubic_weights(fy, wy);
                const T* p[4][4];
                for (int j = 0; j < 4; ++j)
                    for (int i = 0; i < 4; ++i) p[j][i] = at(x0 - 1 + i, y0i - 1 + j);
                for (int k = 0; k < c; ++k) {
                    float acc = 0.0f;
                    for (int j = 0; j < 4; ++j) {
                        float row = 0.0f;
                        for (int i = 0; i < 4; ++i) row += wx[i] * (p[j][i] ? float(p[j][i][k]) : 0.0f);
                        acc += wy[j] * row;
                    }
                    o[k] = store_value<T>(acc);
                }
            }
        }
    });
    return out;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ homography

homography::homography(const matrix& a) noexcept {
    m[0] = a.a;
    m[1] = a.c;
    m[2] = a.e;
    m[3] = a.b;
    m[4] = a.d;
    m[5] = a.f;
    m[6] = 0.0f;
    m[7] = 0.0f;
    m[8] = 1.0f;
}

std::optional<homography> homography::from_quad(const point src[4], const point dst[4]) {
    // eight unknowns h0..h7 (h8 = 1), two equations per pair:
    //   h0 x + h1 y + h2 - u h6 x - u h7 y = u        h3 x + h4 y + h5 - v h6 x - v h7 y = v
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = double(src[i].x), y = double(src[i].y), u = double(dst[i].x), v = double(dst[i].y);
        double* r0 = a[2 * i];
        double* r1 = a[2 * i + 1];
        r0[0] = x; r0[1] = y; r0[2] = 1.0; r0[6] = -u * x; r0[7] = -u * y; r0[8] = u;
        r1[3] = x; r1[4] = y; r1[5] = 1.0; r1[6] = -v * x; r1[7] = -v * y; r1[8] = v;
    }
    // Gauss-Jordan with partial pivoting; a pivot that is small against the largest entry means the points are degenerate
    double biggest = 0.0;
    for (const auto& row : a)
        for (double v : row) biggest = std::max(biggest, std::fabs(v));
    if (!(biggest > 0.0) || !std::isfinite(biggest)) return std::nullopt;
    for (int col = 0; col < 8; ++col) {
        int piv = col;
        for (int r = col + 1; r < 8; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (!(std::fabs(a[piv][col]) > biggest * 1e-10)) return std::nullopt;
        if (piv != col)
            for (int k = 0; k < 9; ++k) std::swap(a[piv][k], a[col][k]);
        const double d = a[col][col];
        for (int k = col; k < 9; ++k) a[col][k] /= d;
        for (int r = 0; r < 8; ++r) {
            if (r == col || a[r][col] == 0.0) continue;
            const double f = a[r][col];
            for (int k = col; k < 9; ++k) a[r][k] -= f * a[col][k];
        }
    }
    homography h;
    for (int i = 0; i < 8; ++i) {
        if (!std::isfinite(a[i][8])) return std::nullopt;
        h.m[i] = float(a[i][8]);
    }
    h.m[8] = 1.0f;
    return h;
}

homography homography::then(const homography& next) const noexcept { return from_m3(multiply(to_m3(next), to_m3(*this))); }

point homography::map(point p) const noexcept {
    const double x = double(p.x), y = double(p.y);
    const double den = double(m[6]) * x + double(m[7]) * y + double(m[8]);
    if (!(std::fabs(den) > 1e-12)) return point{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    return point{float((double(m[0]) * x + double(m[1]) * y + double(m[2])) / den), float((double(m[3]) * x + double(m[4]) * y + double(m[5])) / den)};
}

std::optional<homography> homography::inverse() const noexcept {
    m3 r;
    if (!invert(to_m3(*this), r)) return std::nullopt;
    return from_m3(r);
}

// ------------------------------------------------------------------------------------------------ the entry point

namespace detail {

template <class T>
mat<T> warp_core(const mat<T>& src, const homography& h, int out_width, int out_height, interpolation_method interpolation, edge_mode edge) {
    return warp_run(src, h, out_width, out_height, interpolation, edge);
}

template STB_API mat<std::uint8_t> warp_core<std::uint8_t>(const mat<std::uint8_t>&, const homography&, int, int, interpolation_method, edge_mode);
template STB_API mat<float> warp_core<float>(const mat<float>&, const homography&, int, int, interpolation_method, edge_mode);

}  // namespace detail
}  // namespace stb
