#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "stb.h"
#include "conftest.hpp"

// warp_affine and warp_perspective. The small cases are worked out by hand in the comments; the rest is compared with a
// straightforward sampler written in doubles from stb::matrix::inverse.

namespace {

using stb::edge_mode;
using stb::homography;
using stb::image8u;
using stb::interpolation_method;
using stb::mat32f;
using stb::mat8u;
using stb::matrix;
using stb::point;

image8u gray_row(const std::vector<std::uint8_t>& v) { return image8u(int(v.size()), 1, stb::pixel_format::gray, v); }

image8u patterned8(int w, int h, stb::pixel_format f, std::uint32_t seed = 777u) {
    image8u m(w, h, f);
    std::uint32_t s = seed;
    for (std::uint8_t& v : m) {
        s = s * 1664525u + 1013904223u;
        v = std::uint8_t(s >> 24);
    }
    return m;
}

mat32f patterned_f(int w, int h, int c, std::uint32_t seed = 12345u) {
    mat32f m(w, h, c);
    std::uint32_t s = seed;
    for (float& v : m) {
        s = s * 1664525u + 1013904223u;
        v = float((s >> 8) & 0xFFFF) / 65535.0f;
    }
    return m;
}

bool same_bytes(const mat8u& a, const mat8u& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}
bool same_bits(const mat32f& a, const mat32f& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

const interpolation_method all_interp[] = {interpolation_method::nearest, interpolation_method::bilinear, interpolation_method::bicubic};
const edge_mode all_edges[] = {edge_mode::clamp, edge_mode::reflect, edge_mode::mirror, edge_mode::wrap, edge_mode::zero};

// A value of `m` under an edge mode: 0 where the mode says "zero" (testutil::edge_index is -1 there).
template <class M>
double sample(const M& m, int x, int y, int k, edge_mode mode) {
    const int sx = testutil::edge_index(x, m.width(), mode), sy = testutil::edge_index(y, m.height(), mode);
    return sx < 0 || sy < 0 ? 0.0 : double(m(sx, sy, k));
}

// The warp in doubles: for each pixel of the result, the source point from the inverse matrix, then the kernel.
template <class M>
double brute_warp(const M& src, const matrix& inv, int x, int y, int k, interpolation_method interp, edge_mode mode) {
    const point p = inv.map(float(x) + 0.5f, float(y) + 0.5f);
    const double sx = double(p.x), sy = double(p.y);
    if (interp == interpolation_method::nearest) return sample(src, int(std::floor(sx)), int(std::floor(sy)), k, mode);
    const double u = sx - 0.5, v = sy - 0.5;
    const int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    const double fx = u - x0, fy = v - y0;
    if (interp == interpolation_method::bilinear) {
        const double top = (1 - fx) * sample(src, x0, y0, k, mode) + fx * sample(src, x0 + 1, y0, k, mode);
        const double bottom = (1 - fx) * sample(src, x0, y0 + 1, k, mode) + fx * sample(src, x0 + 1, y0 + 1, k, mode);
        return (1 - fy) * top + fy * bottom;
    }
    const auto weights = [](double t, double w[4]) {
        w[0] = (-t * t * t + 2 * t * t - t) / 2;
        w[1] = (3 * t * t * t - 5 * t * t + 2) / 2;
        w[2] = (-3 * t * t * t + 4 * t * t + t) / 2;
        w[3] = (t * t * t - t * t) / 2;
    };
    double wx[4], wy[4];
    weights(fx, wx);
    weights(fy, wy);
    double acc = 0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) acc += wy[j] * wx[i] * sample(src, x0 - 1 + i, y0 - 1 + j, k, mode);
    return acc;
}

}  // namespace

static_assert(std::is_same_v<decltype(stb::warp_affine(std::declval<const image8u&>(), matrix())), image8u>);
static_assert(std::is_same_v<decltype(stb::warp_affine(std::declval<const mat8u&>(), matrix())), mat8u>);
static_assert(std::is_same_v<decltype(stb::warp_perspective(std::declval<const mat32f&>(), homography())), mat32f>);

UTEST(libstb_warp_tests, test_identity_returns_the_picture_for_every_kernel_and_edge_mode) {
    const image8u u = patterned8(13, 9, stb::pixel_format::rgba);
    const mat32f f = patterned_f(11, 8, 3);
    for (interpolation_method ip : all_interp)
        for (edge_mode e : all_edges) {
            ASSERT_TRUE(same_bytes(stb::warp_affine(u, matrix(), 0, 0, ip, e), u));
            ASSERT_TRUE(same_bits(stb::warp_affine(f, matrix(), 0, 0, ip, e), f));
            ASSERT_TRUE(same_bytes(stb::warp_perspective(u, homography(), 0, 0, ip, e), u));
        }
}

UTEST(libstb_warp_tests, test_whole_pixel_shifts_and_the_edge_modes_as_computed_by_hand) {
    // 4 x 1 gray [10, 20, 30, 40] moved one pixel to the right: the result at x comes from x - 1.
    const image8u img = gray_row({10, 20, 30, 40});
    matrix right;
    right.translate(1, 0);
    for (interpolation_method ip : all_interp) {
        const image8u zero = stb::warp_affine(img, right, 0, 0, ip, edge_mode::zero);
        const image8u clamp = stb::warp_affine(img, right, 0, 0, ip, edge_mode::clamp);
        const image8u wrap = stb::warp_affine(img, right, 0, 0, ip, edge_mode::wrap);
        const std::uint8_t z[4] = {0, 10, 20, 30}, c[4] = {10, 10, 20, 30}, w[4] = {40, 10, 20, 30};
        for (int x = 0; x < 4; ++x) {
            ASSERT_EQ(int(zero(x, 0, 0)), int(z[x]));
            ASSERT_EQ(int(clamp(x, 0, 0)), int(c[x]));
            ASSERT_EQ(int(wrap(x, 0, 0)), int(w[x]));
        }
    }
}

UTEST(libstb_warp_tests, test_half_a_pixel_and_a_doubling_as_computed_by_hand) {
    // Moving [0, 100, 200, 40] by half a pixel: the result at x reads the picture at the point x - 0.5 between pixel
    // centres, i.e. halfway between pixels x - 1 and x: (0 + 0) / 2, (0 + 100) / 2, (100 + 200) / 2, (200 + 40) / 2.
    matrix half;
    half.translate(0.5f, 0);
    const image8u a = stb::warp_affine(gray_row({0, 100, 200, 40}), half);
    ASSERT_EQ(int(a(0, 0, 0)), 0);
    ASSERT_EQ(int(a(1, 0, 0)), 50);
    ASSERT_EQ(int(a(2, 0, 0)), 150);
    ASSERT_EQ(int(a(3, 0, 0)), 120);
    // [10, 20] scaled by 2 into 4 pixels. Nearest: 10 10 20 20. Bilinear (edge clamp): the centres of the 4 result
    // pixels are the source points 0.25, 0.75, 1.25, 1.75, i.e. 10, 10 + 0.25 * 10 = 12.5, 10 + 0.75 * 10 = 17.5 and 20,
    // and the halves are rounded up: 10 13 18 20.
    const image8u two = gray_row({10, 20});
    const image8u n = stb::warp_affine(two, matrix::scaling(2), 4, 1, interpolation_method::nearest, edge_mode::clamp);
    const image8u b = stb::warp_affine(two, matrix::scaling(2), 4, 1, interpolation_method::bilinear, edge_mode::clamp);
    const std::uint8_t wn[4] = {10, 10, 20, 20}, wb[4] = {10, 13, 18, 20};
    for (int x = 0; x < 4; ++x) {
        ASSERT_EQ(int(n(x, 0, 0)), int(wn[x]));
        ASSERT_EQ(int(b(x, 0, 0)), int(wb[x]));
    }
}

UTEST(libstb_warp_tests, test_a_quarter_turn_about_the_centre_is_rotate90) {
    const image8u img = patterned8(7, 7, stb::pixel_format::rgb);
    matrix m;
    m.translate(3.5f, 3.5f).rotate(1.57079632679f).translate(-3.5f, -3.5f);  // the last call acts first: centre to origin, turn, back
    ASSERT_TRUE(same_bytes(stb::warp_affine(img, m, 0, 0, interpolation_method::nearest), img.rotate90(-1)));
    ASSERT_TRUE(same_bytes(stb::warp_affine(img, m, 0, 0, interpolation_method::bilinear), img.rotate90(-1)));
}

UTEST(libstb_warp_tests, test_the_result_size_defaults_to_the_picture_and_can_be_changed) {
    const image8u img = patterned8(10, 8, stb::pixel_format::rgb);
    ASSERT_EQ(stb::warp_affine(img, matrix::scaling(2)).width(), 10);
    ASSERT_EQ(stb::warp_affine(img, matrix::scaling(2)).height(), 8);
    const image8u crop = stb::warp_affine(img, matrix(), 6, 3);  // the top left corner, as it is
    ASSERT_EQ(crop.width(), 6);
    ASSERT_EQ(crop.height(), 3);
    ASSERT_TRUE(same_bytes(crop, img.crop(0, 0, 6, 3)));
    const image8u big = stb::warp_affine(img, matrix(), 14, 11, interpolation_method::nearest, edge_mode::zero);  // outside: 0
    ASSERT_EQ(int(big(13, 10, 0)) + int(big(10, 2, 1)), 0);
}

UTEST(libstb_warp_tests, test_every_kernel_matches_a_sampler_in_doubles_for_every_edge_mode) {
    const image8u u = patterned8(16, 12, stb::pixel_format::rgba);
    const mat32f f = patterned_f(15, 11, 2);
    matrix m;
    m.translate(-8, -6).rotate(0.4f).scale(1.3f, 0.9f).translate(9, 7);
    const matrix inv = *m.inverse();
    for (interpolation_method ip : all_interp)
        for (edge_mode e : all_edges) {
            const image8u gu = stb::warp_affine(u, m, 18, 14, ip, e);
            const mat32f gf = stb::warp_affine(f, m, 18, 14, ip, e);
            for (int y = 0; y < 14; ++y)
                for (int x = 0; x < 18; ++x) {
                    for (int k = 0; k < 4; ++k) {
                        double want = brute_warp(u, inv, x, y, k, ip, e);
                        want = std::min(255.0, std::max(0.0, want));
                        ASSERT_TRUE(std::fabs(double(gu(x, y, k)) - want) <= 1.0);  // rounding, and the float of the point
                    }
                    for (int k = 0; k < 2; ++k) {
                        const double want = brute_warp(f, inv, x, y, k, ip, e);
                        ASSERT_TRUE(std::fabs(double(gf(x, y, k)) - want) < 2e-3);
                    }
                }
        }
}

UTEST(libstb_warp_tests, test_homography_from_four_points_maps_them_and_composes_and_inverts) {
    const point src[4] = {{0, 0}, {10, 0}, {10, 8}, {0, 8}};
    const point dst[4] = {{1, 2}, {12, 1}, {9, 10}, {-1, 7}};
    const std::optional<homography> h = homography::from_quad(src, dst);
    ASSERT_TRUE(h.has_value());
    for (int i = 0; i < 4; ++i) {
        const point p = h->map(src[i]);
        ASSERT_TRUE(std::fabs(p.x - dst[i].x) < 1e-3f && std::fabs(p.y - dst[i].y) < 1e-3f);
    }
    const std::optional<homography> inv = h->inverse();
    ASSERT_TRUE(inv.has_value());
    for (int i = 0; i < 4; ++i) {
        const point p = inv->map(h->map(src[i]));
        ASSERT_TRUE(std::fabs(p.x - src[i].x) < 1e-3f && std::fabs(p.y - src[i].y) < 1e-3f);
    }
    const point back = h->then(*inv).map(3.0f, 4.0f);  // first h, then its inverse: where it started
    ASSERT_TRUE(std::fabs(back.x - 3.0f) < 1e-3f && std::fabs(back.y - 4.0f) < 1e-3f);
    // a translation by two points is the matrix that translates, and its warp is the affine warp
    const point moved[4] = {{2, 0}, {12, 0}, {12, 8}, {2, 8}};
    const std::optional<homography> t = homography::from_quad(src, moved);
    ASSERT_TRUE(t.has_value());
    const image8u img = patterned8(12, 9, stb::pixel_format::rgb);
    matrix shift;
    shift.translate(2, 0);
    ASSERT_TRUE(same_bytes(stb::warp_perspective(img, *t, 0, 0, interpolation_method::nearest), stb::warp_affine(img, shift, 0, 0, interpolation_method::nearest)));
    ASSERT_TRUE(same_bytes(stb::warp_perspective(img, homography(shift)), stb::warp_affine(img, shift)));
}

UTEST(libstb_warp_tests, test_degenerate_points_and_matrices_have_no_inverse) {
    const point line[4] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    const point dst[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    ASSERT_TRUE(!homography::from_quad(line, dst).has_value());
    ASSERT_TRUE(!homography::from_quad(dst, line).has_value());
    homography zero;
    for (float& v : zero.m) v = 0.0f;
    ASSERT_TRUE(!zero.inverse().has_value());
    ASSERT_TRUE(std::isnan(zero.map(1, 1).x));
}

UTEST(libstb_warp_tests, test_a_vanishing_horizon_gives_zeros_and_perspective_warps_are_sane) {
    // squeeze the top edge of a bright picture: the result has the picture inside the trapezoid and 0 outside it
    image8u img(32, 32, stb::pixel_format::gray);
    for (std::uint8_t& v : img) v = 200;
    const point src[4] = {{0, 0}, {32, 0}, {32, 32}, {0, 32}};
    const point dst[4] = {{8, 0}, {24, 0}, {32, 32}, {0, 32}};
    const std::optional<homography> h = homography::from_quad(src, dst);
    ASSERT_TRUE(h.has_value());
    const image8u out = stb::warp_perspective(img, *h);
    ASSERT_EQ(int(out(2, 1, 0)), 0);     // top left corner: outside the trapezoid
    ASSERT_EQ(int(out(29, 1, 0)), 0);    // top right: outside
    ASSERT_EQ(int(out(16, 1, 0)), 200);  // top middle: inside
    ASSERT_EQ(int(out(2, 30, 0)), 200);  // the bottom keeps its full width
    ASSERT_EQ(int(out(29, 30, 0)), 200);
}

UTEST(libstb_warp_tests, test_bad_arguments_throw_and_empty_pictures_stay_empty) {
    const image8u img = patterned8(8, 6, stb::pixel_format::rgb);
    ASSERT_THROWS(stb::warp_affine(img, matrix::scaling(0, 1)), std::invalid_argument);            // no inverse
    ASSERT_THROWS(stb::warp_affine(img, matrix(), -1, 4), std::invalid_argument);
    ASSERT_THROWS(stb::warp_affine(img, matrix(), 4, -1), std::invalid_argument);
    ASSERT_THROWS(stb::warp_affine(img, matrix(), 100000, 100000), std::invalid_argument);         // too large
    ASSERT_THROWS(stb::warp_affine(img, matrix(), 0, 0, static_cast<interpolation_method>(9)), std::invalid_argument);
    ASSERT_THROWS(stb::warp_affine(mat8u(4, 4, 5), matrix()), std::invalid_argument);
    ASSERT_THROWS(stb::warp_perspective(mat32f(4, 4, 5), homography()), std::invalid_argument);
    ASSERT_TRUE(stb::warp_affine(mat8u(), matrix()).empty());
    ASSERT_TRUE(stb::warp_perspective(mat32f(), homography()).empty());
}

UTEST(libstb_warp_tests, test_the_result_is_the_same_on_any_number_of_threads) {
    const image8u src = patterned8(300, 200, stb::pixel_format::rgba);
    const mat32f srcf = patterned_f(150, 120, 3);
    matrix m;
    m.translate(-75, -60).rotate(0.3f).translate(80, 60);
    image8u one, many;
    mat32f one_f, many_f;
    {
        stb::thread_scope scope(1);
        one = stb::warp_affine(src, m, 320, 220, interpolation_method::bicubic);
        one_f = stb::warp_affine(srcf, m, 160, 130, interpolation_method::bilinear);
    }
    {
        stb::thread_scope scope(4);
        many = stb::warp_affine(src, m, 320, 220, interpolation_method::bicubic);
        many_f = stb::warp_affine(srcf, m, 160, 130, interpolation_method::bilinear);
    }
    ASSERT_TRUE(same_bytes(one, many));
    ASSERT_TRUE(same_bits(one_f, many_f));
}
