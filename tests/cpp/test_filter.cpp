#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "stb.h"
#include "test_common.hpp"

// Filters. The expected values are written out or computed here by a plain brute-force loop.

namespace {

// A float image with values that differ everywhere, so that a wrong tap or a wrong edge shows.
stb::mat32f patterned(int w, int h, int c) {
    stb::mat32f m(w, h, c);
    std::uint32_t s = 12345u;
    for (float& v : m) {
        s = s * 1664525u + 1013904223u;
        v = float((s >> 8) & 0xFFFF) / 65535.0f;
    }
    return m;
}

stb::image8u patterned8(int w, int h, stb::pixel_format f) {
    stb::image8u m(w, h, f);
    std::uint32_t s = 777u;
    for (std::uint8_t& v : m) {
        s = s * 1664525u + 1013904223u;
        v = std::uint8_t(s >> 24);
    }
    return m;
}

bool same_bits(const stb::mat32f& a, const stb::mat32f& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}
bool same_bytes(const stb::mat8u& a, const stb::mat8u& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}
float max_diff(const stb::mat32f& a, const stb::mat32f& b) {
    float d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d = STB_MAX(d, std::fabs(a.data()[i] - b.data()[i]));
    return d;
}

// the sample at (x, y) of a one-row-or-column test image under a mode, by hand
int clampi(int i, int n) { return i < 0 ? 0 : (i >= n ? n - 1 : i); }

}  // namespace

// ---------------------------------------------------------------- kernels

UTEST(libstb_filter_tests, test_a_gaussian_kernel_is_normalised_symmetric_and_sized_by_sigma) {
    const stb::mat32f k = stb::gaussian_kernel(2.0f);  // radius ceil(6) = 6
    EXPECT_EQ(13, k.width());
    EXPECT_EQ(1, k.height());
    float sum = 0;
    for (const float v : k) sum += v;
    EXPECT_TRUE(std::fabs(sum - 1.0f) < 1e-6f);
    for (int i = 0; i < 6; ++i) EXPECT_EQ(k(i, 0), k(12 - i, 0));
    EXPECT_TRUE(k(6, 0) > k(5, 0));  // the peak is in the middle
    EXPECT_EQ(7, stb::gaussian_kernel(1.0f, 3).width());
    ASSERT_THROWS(stb::gaussian_kernel(0.0f), std::invalid_argument);
    ASSERT_THROWS(stb::gaussian_kernel(-1.0f), std::invalid_argument);
    ASSERT_THROWS(stb::gaussian_kernel(std::nanf("")), std::invalid_argument);
    ASSERT_THROWS(stb::gaussian_kernel(1.0f, -2), std::invalid_argument);
}

UTEST(libstb_filter_tests, test_bad_kernels_throw) {
    const stb::mat32f src = patterned(5, 5, 1);
    const stb::mat32f even(2, 3), two_channels(3, 3, 2), plane(3, 3), empty;
    ASSERT_THROWS(stb::convolve(src, even), std::invalid_argument);
    ASSERT_THROWS(stb::convolve(src, two_channels), std::invalid_argument);
    ASSERT_THROWS(stb::convolve(src, empty), std::invalid_argument);
    ASSERT_THROWS(stb::convolve(src, plane, plane), std::invalid_argument);  // not a row or a column
    ASSERT_THROWS(stb::convolve(src, stb::mat32f(4, 1), stb::mat32f(1, 3)), std::invalid_argument);  // even length
}

// ---------------------------------------------------------------- convolve

UTEST(libstb_filter_tests, test_the_identity_kernel_changes_nothing_in_float_and_in_8_bits) {
    const stb::mat32f id = {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}};
    const stb::mat32f f = patterned(23, 17, 3);
    EXPECT_TRUE(same_bits(stb::convolve(f, id), f));
    const stb::image8u u = patterned8(23, 17, stb::pixel_format::rgba);
    EXPECT_TRUE(same_bytes(stb::convolve(u, id), u));
    const stb::mat32f one = {{1.0f}};
    EXPECT_TRUE(same_bits(stb::convolve(f, one, one), f));
}

UTEST(libstb_filter_tests, test_a_2d_kernel_matches_the_brute_force_sum_with_clamped_edges) {
    const stb::mat32f k = {{1, 2, 0}, {0, -3, 1}, {2, 0, 4}};
    const stb::mat32f src = patterned(9, 7, 2);
    const stb::mat32f out = stb::convolve(src, k);
    float worst = 0;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x)
            for (int c = 0; c < 2; ++c) {
                float want = 0;
                for (int ky = 0; ky < 3; ++ky)
                    for (int kx = 0; kx < 3; ++kx) want += k(kx, ky) * src(clampi(x + kx - 1, 9), clampi(y + ky - 1, 7), c);
                worst = STB_MAX(worst, std::fabs(want - out(x, y, c)));
            }
    EXPECT_TRUE(worst < 1e-5f);
}

UTEST(libstb_filter_tests, test_a_separable_kernel_gives_the_same_picture_as_its_product) {
    const stb::mat32f row = {{1.0f, 2.0f, 1.0f}}, col = {{-1.0f}, {0.0f}, {1.0f}};  // a row and a column
    stb::mat32f product(3, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) product(x, y) = row(x, 0) * col(0, y);
    const stb::mat32f src = patterned(31, 13, 3);
    EXPECT_TRUE(max_diff(stb::convolve(src, row, col), stb::convolve(src, product)) < 1e-5f);
}

// What the window sees outside, one mode at a time. A 4-pixel row [1 2 3 4] seen through the taps {1 0 0}
// returns the left neighbour of each pixel, through {0 0 1} the right one.
UTEST(libstb_filter_tests, test_every_edge_mode_reads_the_outside_as_documented) {
    const stb::mat32f src(4, 1, 1, std::vector<float>{1, 2, 3, 4});
    const stb::mat32f left = {{1.0f, 0.0f, 0.0f}}, right = {{0.0f, 0.0f, 1.0f}}, one = {{1.0f}};
    struct row {
        stb::edge_mode mode;
        float l[4], r[4];
    };
    const row table[] = {
        {stb::edge_mode::clamp, {1, 1, 2, 3}, {2, 3, 4, 4}},
        {stb::edge_mode::reflect, {1, 1, 2, 3}, {2, 3, 4, 4}},
        {stb::edge_mode::mirror, {2, 1, 2, 3}, {2, 3, 4, 3}},
        {stb::edge_mode::wrap, {4, 1, 2, 3}, {2, 3, 4, 1}},
        {stb::edge_mode::zero, {0, 1, 2, 3}, {2, 3, 4, 0}},
    };
    for (const row& t : table) {
        const stb::mat32f l = stb::convolve(src, left, one, t.mode), r = stb::convolve(src, right, one, t.mode);
        for (int x = 0; x < 4; ++x) {
            EXPECT_EQ(t.l[x], l(x, 0));
            EXPECT_EQ(t.r[x], r(x, 0));
        }
    }
}

UTEST(libstb_filter_tests, test_the_vertical_pass_reads_the_outside_the_same_way) {
    const stb::mat32f src(1, 4, 1, std::vector<float>{1, 2, 3, 4});
    const stb::mat32f up = {{1.0f}, {0.0f}, {0.0f}}, one = {{1.0f}};
    const stb::mat32f got_clamp = stb::convolve(src, one, up, stb::edge_mode::clamp);
    const stb::mat32f got_wrap = stb::convolve(src, one, up, stb::edge_mode::wrap);
    const stb::mat32f got_zero = stb::convolve(src, one, up, stb::edge_mode::zero);
    const stb::mat32f got_mirror = stb::convolve(src, one, up, stb::edge_mode::mirror);
    EXPECT_EQ(1.0f, got_clamp(0, 0));
    EXPECT_EQ(4.0f, got_wrap(0, 0));
    EXPECT_EQ(0.0f, got_zero(0, 0));
    EXPECT_EQ(2.0f, got_mirror(0, 0));
    for (int y = 1; y < 4; ++y) EXPECT_EQ(src(0, y - 1), got_clamp(0, y));
}

UTEST(libstb_filter_tests, test_a_window_wider_than_the_image_still_works_in_every_mode) {
    const stb::mat32f src(2, 2, 1, std::vector<float>{1, 2, 3, 4});
    const stb::mat32f g = stb::gaussian_kernel(2.0f);  // 13 taps over a 2-pixel image
    for (const stb::edge_mode m : {stb::edge_mode::clamp, stb::edge_mode::reflect, stb::edge_mode::mirror,
                                   stb::edge_mode::wrap, stb::edge_mode::zero}) {
        const stb::mat32f out = stb::convolve(src, g, g, m);
        for (const float v : out) EXPECT_TRUE(std::isfinite(v));
    }
}

// ---------------------------------------------------------------- blurs

UTEST(libstb_filter_tests, test_blurring_a_constant_picture_changes_nothing_except_against_zero_edges) {
    const stb::mat32f flat = [] {
        stb::mat32f m(20, 15, 3);
        m.fill(0.75f);
        return m;
    }();
    for (const stb::edge_mode m : {stb::edge_mode::clamp, stb::edge_mode::reflect, stb::edge_mode::mirror, stb::edge_mode::wrap}) {
        EXPECT_TRUE(max_diff(stb::gaussian_blur(flat, 1.7f, m), flat) < 1e-5f);
        EXPECT_TRUE(max_diff(stb::box_blur(flat, 4, m), flat) < 1e-5f);
    }
    const stb::mat32f dark = stb::gaussian_blur(flat, 1.7f, stb::edge_mode::zero);
    EXPECT_TRUE(dark(0, 0, 0) < 0.75f);
    EXPECT_TRUE(std::fabs(dark(10, 7, 0) - 0.75f) < 1e-3f);  // far from the edge nothing is lost
}

UTEST(libstb_filter_tests, test_gaussian_blur_of_an_impulse_is_the_kernel) {
    stb::mat32f dot(21, 21, 1);
    dot(10, 10) = 1.0f;
    const stb::mat32f g = stb::gaussian_kernel(1.5f);
    const stb::mat32f out = stb::gaussian_blur(dot, 1.5f);
    const int r = g.width() / 2;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) EXPECT_TRUE(std::fabs(out(10 + dx, 10 + dy) - g(r + dx, 0) * g(r + dy, 0)) < 1e-6f);
}

UTEST(libstb_filter_tests, test_box_blur_matches_the_brute_force_mean) {
    const stb::mat32f src = patterned(13, 11, 2);
    const stb::mat32f out = stb::box_blur(src, 2);
    float worst = 0;
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 13; ++x)
            for (int c = 0; c < 2; ++c) {
                double sum = 0;
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) sum += double(src(clampi(x + dx, 13), clampi(y + dy, 11), c));
                worst = STB_MAX(worst, std::fabs(float(sum / 25.0) - out(x, y, c)));
            }
    EXPECT_TRUE(worst < 1e-5f);
    EXPECT_TRUE(same_bits(stb::box_blur(src, 0), src));
    ASSERT_THROWS(stb::box_blur(src, -1), std::invalid_argument);
}

UTEST(libstb_filter_tests, test_8_bit_filters_equal_the_float_filter_rounded_once) {
    const stb::image8u u = patterned8(37, 29, stb::pixel_format::rgb);
    const stb::mat32f f = stb::to_f32(u);
    EXPECT_TRUE(same_bytes(stb::gaussian_blur(u, 1.3f), stb::to_8u(stb::gaussian_blur(f, 1.3f))));
    EXPECT_TRUE(same_bytes(stb::box_blur(u, 3), stb::to_8u(stb::box_blur(f, 3))));
    const stb::mat32f k = {{0, -1, 0}, {-1, 5, -1}, {0, -1, 0}};
    EXPECT_TRUE(same_bytes(stb::convolve(u, k), stb::to_8u(stb::convolve(f, k))));
}

UTEST(libstb_filter_tests, test_filters_keep_size_channels_and_format) {
    const stb::image8u u = patterned8(10, 6, stb::pixel_format::gray_alpha);
    const stb::image8u out = stb::gaussian_blur(u, 1.0f);
    EXPECT_EQ(10, out.width());
    EXPECT_EQ(6, out.height());
    EXPECT_TRUE(out.format() == stb::pixel_format::gray_alpha);
    EXPECT_TRUE(stb::gaussian_blur(stb::mat32f(), 1.0f).empty());
    EXPECT_TRUE(stb::box_blur(stb::mat8u(), 2).empty());
}

// ---------------------------------------------------------------- sobel

UTEST(libstb_filter_tests, test_sobel_of_a_vertical_step_is_four_across_the_step_and_zero_elsewhere) {
    stb::mat32f step(8, 6, 1);
    for (int y = 0; y < 6; ++y)
        for (int x = 4; x < 8; ++x) step(x, y) = 1.0f;
    const stb::gradients g = stb::sobel(step);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 8; ++x) {
            const float want = (x == 3 || x == 4) ? 4.0f : 0.0f;
            EXPECT_EQ(want, g.dx(x, y));
            EXPECT_EQ(0.0f, g.dy(x, y));
        }
    }
    const stb::mat32f mag = stb::magnitude(g);
    EXPECT_EQ(4.0f, mag(3, 2));
    EXPECT_EQ(0.0f, mag(0, 2));
}

UTEST(libstb_filter_tests, test_sobel_of_a_horizontal_step_points_down_and_an_8_bit_input_reads_value_over_255) {
    stb::image8u step(6, 8, stb::pixel_format::gray);
    for (int y = 4; y < 8; ++y)
        for (int x = 0; x < 6; ++x) step(x, y) = 255;
    const stb::gradients g = stb::sobel(step);
    EXPECT_EQ(4.0f, g.dy(2, 3));
    EXPECT_EQ(4.0f, g.dy(2, 4));
    EXPECT_EQ(0.0f, g.dx(2, 3));
    EXPECT_TRUE(stb::sobel(stb::mat8u()).dx.empty());
}

UTEST(libstb_filter_tests, test_magnitude_refuses_mismatched_gradients) {
    stb::gradients g;
    g.dx = stb::mat32f(4, 4);
    g.dy = stb::mat32f(4, 5);
    ASSERT_THROWS(stb::magnitude(g), std::invalid_argument);
    EXPECT_TRUE(stb::magnitude(stb::gradients()).empty());
}

// ---------------------------------------------------------------- the same bits everywhere

UTEST(libstb_filter_tests, test_every_simd_backend_gives_the_same_bits) {
    const stb::mat32f f = patterned(129, 67, 3);
    const stb::image8u u = patterned8(129, 67, stb::pixel_format::rgb);
    const stb::mat32f k = {{1, 2, 3, 4, 5}, {5, 4, 3, 2, 1}, {0, 1, 0, 1, 0}, {2, 2, 2, 2, 2}, {9, 0, 0, 0, -9}};
    stb::mat32f ref_g, ref_k, ref_box, ref_sobel;
    stb::image8u ref_u;
    bool first = true;
    for (const std::string& be : stb::simd_backends()) {
        stb::simd_scope scope(be);
        const stb::mat32f g = stb::gaussian_blur(f, 2.3f, stb::edge_mode::reflect);
        const stb::mat32f c = stb::convolve(f, k, stb::edge_mode::mirror);
        const stb::mat32f b = stb::box_blur(f, 3);
        const stb::mat32f s = stb::magnitude(stb::sobel(f));
        const stb::image8u gu = stb::gaussian_blur(u, 2.3f);
        if (first) {
            ref_g = g.copy();
            ref_k = c.copy();
            ref_box = b.copy();
            ref_sobel = s.copy();
            ref_u = stb::image8u(gu.copy());
            first = false;
        } else {
            EXPECT_TRUE(same_bits(g, ref_g));
            EXPECT_TRUE(same_bits(c, ref_k));
            EXPECT_TRUE(same_bits(b, ref_box));
            EXPECT_TRUE(same_bits(s, ref_sobel));
            EXPECT_TRUE(same_bytes(gu, ref_u));
        }
    }
}

UTEST(libstb_filter_tests, test_the_number_of_threads_does_not_change_the_bits) {
    const stb::mat32f f = patterned(301, 211, 3);
    stb::mat32f one_g, one_box;
    {
        stb::thread_scope threads(1);
        one_g = stb::gaussian_blur(f, 3.0f);
        one_box = stb::box_blur(f, 5);
    }
    stb::thread_scope threads(4);
    EXPECT_TRUE(same_bits(stb::gaussian_blur(f, 3.0f), one_g));
    EXPECT_TRUE(same_bits(stb::box_blur(f, 5), one_box));
}

// ---------------------------------------------------------------- median, unsharp mask, pixelize

namespace {

using stb::edge_mode;

// The index that stands for i in [0, n) under a mode (-1 for "zero"): the definition of the modes, written out.
int edge_index(int i, int n, edge_mode mode) {
    if (i >= 0 && i < n) return i;
    switch (mode) {
        case edge_mode::zero: return -1;
        case edge_mode::wrap: return ((i % n) + n) % n;
        case edge_mode::reflect: {
            const int p = ((i % (2 * n)) + 2 * n) % (2 * n);
            return p < n ? p : 2 * n - 1 - p;
        }
        case edge_mode::mirror: {
            if (n == 1) return 0;
            const int p = ((i % (2 * (n - 1))) + 2 * (n - 1)) % (2 * (n - 1));
            return p < n ? p : 2 * (n - 1) - p;
        }
        case edge_mode::clamp: break;
    }
    return clampi(i, n);
}

// The median of the window around (x, y) of channel k by sorting it.
template <class M>
auto brute_median(const M& m, int x, int y, int k, int r, edge_mode mode) {
    using T = typename M::value_type;
    std::vector<T> win;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const int sx = edge_index(x + dx, m.width(), mode), sy = edge_index(y + dy, m.height(), mode);
            win.push_back(sx < 0 || sy < 0 ? T(0) : m(sx, sy, k));
        }
    std::sort(win.begin(), win.end());
    return win[win.size() / 2];
}

const edge_mode all_modes[] = {edge_mode::clamp, edge_mode::reflect, edge_mode::mirror, edge_mode::wrap, edge_mode::zero};

}  // namespace

UTEST(libstb_filter_tests, test_the_median_removes_a_single_outlier) {
    stb::image8u img(5, 5, stb::pixel_format::gray, std::vector<std::uint8_t>(25, 100));
    img(2, 2, 0) = 255;
    const stb::image8u out = stb::median_filter(img, 1);
    for (std::uint8_t v : out) ASSERT_EQ(int(v), 100);
    stb::mat32f f(5, 5, 1, std::vector<float>(25, 0.25f));
    f(1, 3, 0) = 1.0f;
    for (float v : stb::median_filter(f, 1)) ASSERT_EQ(v, 0.25f);
}

UTEST(libstb_filter_tests, test_the_median_matches_sorting_every_window_for_every_edge_mode) {
    const stb::image8u u = patterned8(19, 13, stb::pixel_format::rgb);
    const stb::mat32f f = patterned(17, 11, 2);
    for (edge_mode mode : all_modes)
        for (int r : {1, 2, 3}) {
            const stb::image8u gu = stb::median_filter(u, r, mode);
            const stb::mat32f gf = stb::median_filter(f, r, mode);
            ASSERT_EQ(gu.channels(), 3);
            for (int y = 0; y < u.height(); ++y)
                for (int x = 0; x < u.width(); ++x)
                    for (int k = 0; k < 3; ++k) ASSERT_EQ(int(gu(x, y, k)), int(brute_median(u, x, y, k, r, mode)));
            for (int y = 0; y < f.height(); ++y)
                for (int x = 0; x < f.width(); ++x)
                    for (int k = 0; k < 2; ++k) ASSERT_EQ(gf(x, y, k), brute_median(f, x, y, k, r, mode));
        }
}

UTEST(libstb_filter_tests, test_the_median_with_a_radius_larger_than_the_image) {
    const stb::image8u u = patterned8(4, 3, stb::pixel_format::gray);
    for (edge_mode mode : all_modes) {
        const stb::image8u g = stb::median_filter(u, 9, mode);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x) ASSERT_EQ(int(g(x, y, 0)), int(brute_median(u, x, y, 0, 9, mode)));
    }
}

UTEST(libstb_filter_tests, test_a_nan_sorts_last_in_the_float_median) {
    stb::mat32f f(3, 3, 1, std::vector<float>(9, 0.5f));
    f(1, 1, 0) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(stb::median_filter(f, 1)(1, 1, 0), 0.5f);  // one NaN of nine: the median is a number
    stb::mat32f all(3, 3, 1, std::vector<float>(9, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_TRUE(std::isnan(stb::median_filter(all, 1)(1, 1, 0)));
}

UTEST(libstb_filter_tests, test_the_median_refuses_a_bad_radius_and_handles_an_empty_input) {
    const stb::image8u u = patterned8(6, 6, stb::pixel_format::gray);
    ASSERT_THROWS(stb::median_filter(u, 0), std::invalid_argument);
    ASSERT_THROWS(stb::median_filter(u, -2), std::invalid_argument);
    ASSERT_THROWS(stb::median_filter(u, 1001), std::invalid_argument);
    ASSERT_THROWS(stb::median_filter(stb::mat32f(), 0), std::invalid_argument);
    EXPECT_TRUE(stb::median_filter(stb::mat8u(), 1).empty());
    EXPECT_TRUE(stb::median_filter(stb::mat32f(), 1).empty());
}

UTEST(libstb_filter_tests, test_the_median_does_not_depend_on_the_number_of_threads) {
    const stb::image8u u = patterned8(400, 300, stb::pixel_format::rgb);
    const stb::mat32f f = patterned(160, 120, 3);
    stb::image8u one_u;
    stb::mat32f one_f;
    {
        stb::thread_scope scope(1);
        one_u = stb::median_filter(u, 4);
        one_f = stb::median_filter(f, 2);
    }
    stb::thread_scope scope(4);
    EXPECT_TRUE(same_bytes(stb::median_filter(u, 4), one_u));
    EXPECT_TRUE(same_bits(stb::median_filter(f, 2), one_f));
}

// ---- unsharp mask

UTEST(libstb_filter_tests, test_unsharp_mask_is_the_source_plus_amount_times_its_difference_from_the_blur) {
    const stb::mat32f f = patterned(40, 30, 3);
    const stb::mat32f blur = stb::gaussian_blur(f, 1.5f);
    const stb::mat32f got = stb::unsharp_mask(f, 1.5f, 0.8f);
    for (std::size_t i = 0; i < f.size(); ++i) {
        float want = f.data()[i] + 0.8f * (f.data()[i] - blur.data()[i]);
        want = want < 0.0f ? 0.0f : (want > 1.0f ? 1.0f : want);
        ASSERT_NEAR(got.data()[i], want, 1e-6f);
    }
}

UTEST(libstb_filter_tests, test_unsharp_mask_with_amount_0_or_a_high_threshold_changes_nothing) {
    const stb::image8u u = patterned8(30, 20, stb::pixel_format::rgb);
    EXPECT_TRUE(same_bytes(stb::unsharp_mask(u, 2.0f, 0.0f), u));
    const stb::mat32f f = patterned(30, 20, 1);
    EXPECT_TRUE(same_bits(stb::unsharp_mask(f, 2.0f, 0.0f), f));
    // a threshold above every difference of this smooth image
    stb::mat32f smooth(20, 20, 1);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x) smooth(x, y, 0) = 0.3f + 0.001f * float(x);
    EXPECT_TRUE(same_bits(stb::unsharp_mask(smooth, 1.0f, 5.0f, 0.05f), smooth));
}

UTEST(libstb_filter_tests, test_unsharp_mask_sharpens_an_edge_and_never_touches_alpha) {
    stb::image8u step(8, 1, stb::pixel_format::gray_alpha);
    for (int x = 0; x < 8; ++x) {
        step(x, 0, 0) = x < 4 ? 64 : 192;
        step(x, 0, 1) = std::uint8_t(10 + x);
    }
    const stb::image8u out = stb::unsharp_mask(step, 1.0f, 1.0f);
    EXPECT_LT(int(out(3, 0, 0)), 64);   // darker on the dark side of the edge
    EXPECT_GT(int(out(4, 0, 0)), 192);  // brighter on the bright side
    EXPECT_EQ(int(out(0, 0, 0)), 64);   // flat far from the edge
    for (int x = 0; x < 8; ++x) EXPECT_EQ(int(out(x, 0, 1)), 10 + x);
    const stb::mat32f rgba = patterned(12, 12, 4);
    const stb::mat32f sharp = stb::unsharp_mask(rgba, 1.0f, 2.0f);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x) ASSERT_EQ(sharp(x, y, 3), rgba(x, y, 3));
}

UTEST(libstb_filter_tests, test_unsharp_mask_forms_and_errors) {
    const stb::image8u u = patterned8(20, 20, stb::pixel_format::rgb);
    stb::image8u in_place(u.copy());
    stb::unsharp_mask_inplace(in_place, 1.0f, 1.5f, 0.02f);
    EXPECT_TRUE(same_bytes(stb::unsharp_mask(u, 1.0f, 1.5f, 0.02f), in_place));
    EXPECT_TRUE(same_bytes(stb::unsharp_mask(u.copy(), 1.0f, 1.5f, 0.02f), in_place));
    stb::image8u keep(u.copy());
    ASSERT_THROWS(stb::unsharp_mask_inplace(keep, 0.0f), std::invalid_argument);
    ASSERT_THROWS(stb::unsharp_mask_inplace(keep, 1.0f, -1.0f), std::invalid_argument);
    ASSERT_THROWS(stb::unsharp_mask_inplace(keep, 1.0f, std::numeric_limits<float>::infinity()), std::invalid_argument);
    ASSERT_THROWS(stb::unsharp_mask_inplace(keep, 1.0f, 1.0f, 1.5f), std::invalid_argument);
    ASSERT_THROWS(stb::unsharp_mask_inplace(keep, 1.0f, 1.0f, -0.1f), std::invalid_argument);
    ASSERT_THROWS(stb::unsharp_mask(stb::mat8u(4, 4, 5), 1.0f), std::invalid_argument);
    EXPECT_TRUE(same_bytes(keep, u));
    EXPECT_TRUE(stb::unsharp_mask(stb::image8u(), 1.0f).empty());
}

UTEST(libstb_filter_tests, test_unsharp_mask_does_not_depend_on_the_number_of_threads) {
    const stb::image8u u = patterned8(500, 400, stb::pixel_format::rgb);
    stb::image8u one(u.copy()), many(u.copy());
    {
        stb::thread_scope scope(1);
        stb::unsharp_mask_inplace(one, 2.0f, 1.2f, 0.01f);
    }
    stb::thread_scope scope(4);
    stb::unsharp_mask_inplace(many, 2.0f, 1.2f, 0.01f);
    EXPECT_TRUE(same_bytes(one, many));
}

// ---- pixelize

UTEST(libstb_filter_tests, test_pixelize_fills_each_block_with_its_rounded_mean) {
    std::vector<std::uint8_t> v(16);
    for (int i = 0; i < 16; ++i) v[std::size_t(i)] = std::uint8_t(i);
    const stb::image8u img(4, 4, stb::pixel_format::gray, v);
    const stb::image8u out = stb::pixelize(img, 2);
    // blocks: {0,1,4,5} -> 2.5 -> 3, {2,3,6,7} -> 4.5 -> 5, {8,9,12,13} -> 10.5 -> 11, {10,11,14,15} -> 12.5 -> 13
    const int want[4][4] = {{3, 3, 5, 5}, {3, 3, 5, 5}, {11, 11, 13, 13}, {11, 11, 13, 13}};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) ASSERT_EQ(int(out(x, y, 0)), want[y][x]);
}

UTEST(libstb_filter_tests, test_pixelize_blocks_at_the_edges_are_smaller_and_block_h_defaults_to_block_w) {
    stb::image8u img(5, 3, stb::pixel_format::gray, std::vector<std::uint8_t>{0, 2, 10, 20, 40, 4, 6, 30, 40, 60, 8, 8, 8, 8, 8});
    const stb::image8u out = stb::pixelize(img, 2);
    // block (0,0): 0 2 4 6 -> 3; (2,0): 10 20 30 40 -> 25; (4,0): 40 60 -> 50; row 2: (0): 8 8 -> 8, (2): 8 8 -> 8, (4): 8
    const int want[3][5] = {{3, 3, 25, 25, 50}, {3, 3, 25, 25, 50}, {8, 8, 8, 8, 8}};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) ASSERT_EQ(int(out(x, y, 0)), want[y][x]);
    EXPECT_TRUE(same_bytes(stb::pixelize(img, 2, 2), out));
}

UTEST(libstb_filter_tests, test_pixelize_with_a_block_taller_than_wide_or_larger_than_the_image) {
    const stb::image8u img(4, 2, stb::pixel_format::gray, std::vector<std::uint8_t>{0, 4, 8, 12, 100, 104, 108, 112});
    const stb::image8u rows = stb::pixelize(img, 4, 1);  // each row is one block
    EXPECT_EQ(int(rows(0, 0, 0)), 6);
    EXPECT_EQ(int(rows(3, 1, 0)), 106);
    const stb::image8u one = stb::pixelize(img, 50);  // one block: the mean of all, 56
    for (std::uint8_t v : one) ASSERT_EQ(int(v), 56);
}

UTEST(libstb_filter_tests, test_pixelize_weights_the_colour_by_alpha_and_averages_alpha) {
    // a red pixel that is opaque and a green one that is transparent: the block is red, half transparent
    const stb::image8u img(2, 1, stb::pixel_format::rgba, std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 0});
    const stb::image8u out = stb::pixelize(img, 2);
    for (int x = 0; x < 2; ++x) {
        EXPECT_EQ(int(out(x, 0, 0)), 255);
        EXPECT_EQ(int(out(x, 0, 1)), 0);
        EXPECT_EQ(int(out(x, 0, 2)), 0);
        EXPECT_EQ(int(out(x, 0, 3)), 128);  // 127.5 rounds up
    }
    // nothing opaque: the plain mean of the colours
    const stb::image8u clear(2, 1, stb::pixel_format::rgba, std::vector<std::uint8_t>{10, 20, 30, 0, 20, 40, 60, 0});
    const stb::image8u c = stb::pixelize(clear, 2);
    EXPECT_EQ(int(c(1, 0, 0)), 15);
    EXPECT_EQ(int(c(1, 0, 1)), 30);
    EXPECT_EQ(int(c(0, 0, 2)), 45);
    EXPECT_EQ(int(c(0, 0, 3)), 0);
}

UTEST(libstb_filter_tests, test_pixelize_floats_and_forms_and_errors) {
    const stb::mat32f f(2, 2, 1, std::vector<float>{0.0f, 0.25f, 0.5f, 0.75f});
    for (float v : stb::pixelize(f, 2)) ASSERT_NEAR(v, 0.375f, 1e-6f);
    const stb::image8u u = patterned8(31, 17, stb::pixel_format::rgb);
    stb::image8u in_place(u.copy());
    stb::pixelize_inplace(in_place, 4, 3);
    EXPECT_TRUE(same_bytes(stb::pixelize(u, 4, 3), in_place));
    EXPECT_TRUE(same_bytes(stb::pixelize(u.copy(), 4, 3), in_place));
    ASSERT_THROWS(stb::pixelize(u, 0), std::invalid_argument);
    ASSERT_THROWS(stb::pixelize(u, 2, -1), std::invalid_argument);
    ASSERT_THROWS(stb::pixelize(stb::mat8u(4, 4, 5), 2), std::invalid_argument);
    EXPECT_TRUE(stb::pixelize(stb::mat32f(), 2).empty());
}

UTEST(libstb_filter_tests, test_pixelize_does_not_depend_on_the_number_of_threads) {
    const stb::image8u u = patterned8(600, 400, stb::pixel_format::rgba);
    const stb::mat32f f = patterned(500, 300, 4);
    stb::image8u one(u.copy());
    stb::mat32f one_f(f.copy());
    {
        stb::thread_scope scope(1);
        stb::pixelize_inplace(one, 7, 5);
        stb::pixelize_inplace(one_f, 9);
    }
    stb::image8u many(u.copy());
    stb::mat32f many_f(f.copy());
    stb::thread_scope scope(4);
    stb::pixelize_inplace(many, 7, 5);
    stb::pixelize_inplace(many_f, 9);
    EXPECT_TRUE(same_bytes(one, many));
    EXPECT_TRUE(same_bits(one_f, many_f));
}
