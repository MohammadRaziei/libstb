#include <cmath>
#include <cstdint>
#include <cstring>
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
    for (std::size_t i = 0; i < a.size(); ++i) d = std::max(d, std::fabs(a.data()[i] - b.data()[i]));
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
                worst = std::max(worst, std::fabs(want - out(x, y, c)));
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
                worst = std::max(worst, std::fabs(float(sum / 25.0) - out(x, y, c)));
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
