#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "stb.h"
#include "conftest.hpp"

// Metrics: mae, mse, psnr and ssim. The small cases are worked out by hand; ssim is checked against a plain
// double-precision loop written here.

namespace {

using stb::image8u;
using stb::mat32f;
using stb::mat8u;

image8u gray_row(const std::vector<std::uint8_t>& v) { return image8u(int(v.size()), 1, stb::pixel_format::gray, v); }

mat32f float_row(const std::vector<float>& v) { return mat32f(int(v.size()), 1, 1, v); }

image8u patterned8(int w, int h, stb::pixel_format f, std::uint32_t seed) {
    image8u m(w, h, f);
    std::uint32_t s = seed;
    for (std::uint8_t& v : m) {
        s = s * 1664525u + 1013904223u;
        v = std::uint8_t(s >> 24);
    }
    return m;
}

mat32f patterned_f(int w, int h, int c, std::uint32_t seed) {
    mat32f m(w, h, c);
    std::uint32_t s = seed;
    for (float& v : m) {
        s = s * 1664525u + 1013904223u;
        v = float((s >> 8) & 0xFFFF) / 65535.0f;
    }
    return m;
}

// b is a with some noise: a mix of the two, so that the pictures are similar but not equal.
mat32f mixed(const mat32f& a, const mat32f& noise, float t) {
    mat32f out(a.width(), a.height(), a.channels());
    for (std::size_t i = 0; i < a.size(); ++i) out.data()[i] = (1.0f - t) * a.data()[i] + t * noise.data()[i];
    return out;
}

bool same_bits(const mat32f& a, const mat32f& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

// SSIM of one window position by the definition, in double (gray image, window 11 x 11, sigma 1.5).
double ssim_at(const mat32f& a, const mat32f& b, int x0, int y0) {
    double w[11], sum = 0;
    for (int i = 0; i < 11; ++i) {
        w[i] = std::exp(-double((i - 5) * (i - 5)) / (2.0 * 1.5 * 1.5));
        sum += w[i];
    }
    double ma = 0, mb = 0, saa = 0, sbb = 0, sab = 0;
    for (int j = 0; j < 11; ++j)
        for (int i = 0; i < 11; ++i) {
            const double wt = w[i] * w[j] / (sum * sum);
            const double va = a(x0 + i, y0 + j), vb = b(x0 + i, y0 + j);
            ma += wt * va;
            mb += wt * vb;
            saa += wt * va * va;
            sbb += wt * vb * vb;
            sab += wt * va * vb;
        }
    const double c1 = 0.01 * 0.01, c2 = 0.03 * 0.03;
    return ((2 * ma * mb + c1) * (2 * (sab - ma * mb) + c2)) /
           ((ma * ma + mb * mb + c1) * ((saa - ma * ma) + (sbb - mb * mb) + c2));
}

}  // namespace

// ------------------------------------------------------------------------------------------- mae, mse, psnr

UTEST(libstb_metrics_tests, test_mae_mse_and_psnr_of_8_bit_values_by_hand) {
    // differences 0, 3, 4: mae 7/3, mse 25/3
    const image8u a = gray_row({0, 10, 20}), b = gray_row({0, 13, 16});
    EXPECT_NEAR(stb::mae(a, b), 7.0 / 3.0, 1e-12);
    EXPECT_NEAR(stb::mse(a, b), 25.0 / 3.0, 1e-12);
    EXPECT_NEAR(stb::psnr(a, b), 10.0 * std::log10(255.0 * 255.0 / (25.0 / 3.0)), 1e-12);
    EXPECT_EQ(stb::mae(a, b), stb::mae(b, a));
}

UTEST(libstb_metrics_tests, test_mae_mse_and_psnr_of_float_values_by_hand) {
    // differences 0, 0.25, 0.5: mae 0.25, mse 0.3125 / 3
    const mat32f a = float_row({0.0f, 0.5f, 1.0f}), b = float_row({0.0f, 0.25f, 0.5f});
    EXPECT_NEAR(stb::mae(a, b), 0.25, 1e-12);
    EXPECT_NEAR(stb::mse(a, b), 0.3125 / 3.0, 1e-12);
    EXPECT_NEAR(stb::psnr(a, b), 10.0 * std::log10(1.0 / (0.3125 / 3.0)), 1e-12);
}

UTEST(libstb_metrics_tests, test_equal_pictures_have_no_error_and_infinite_psnr) {
    const image8u a = patterned8(20, 15, stb::pixel_format::rgb, 5u);
    EXPECT_EQ(stb::mae(a, a), 0.0);
    EXPECT_EQ(stb::mse(a, a), 0.0);
    EXPECT_TRUE(std::isinf(stb::psnr(a, a)) && stb::psnr(a, a) > 0);
    const mat32f f = patterned_f(20, 15, 3, 6u);
    EXPECT_TRUE(std::isinf(stb::psnr(f, f)));
}

UTEST(libstb_metrics_tests, test_all_channels_count_alpha_included) {
    const image8u a(1, 1, stb::pixel_format::rgba, std::vector<std::uint8_t>{10, 10, 10, 0});
    const image8u b(1, 1, stb::pixel_format::rgba, std::vector<std::uint8_t>{10, 10, 10, 40});
    EXPECT_NEAR(stb::mae(a, b), 10.0, 1e-12);  // 40 over four values
}

UTEST(libstb_metrics_tests, test_a_nan_makes_the_float_result_nan) {
    const mat32f a = float_row({0.0f, std::numeric_limits<float>::quiet_NaN()}), b = float_row({0.0f, 0.5f});
    EXPECT_TRUE(std::isnan(stb::mae(a, b)));
    EXPECT_TRUE(std::isnan(stb::mse(a, b)));
}

UTEST(libstb_metrics_tests, test_pictures_that_do_not_match_are_refused) {
    const image8u a = patterned8(12, 12, stb::pixel_format::rgb, 1u);
    ASSERT_THROWS(stb::mae(a, patterned8(12, 11, stb::pixel_format::rgb, 1u)), std::invalid_argument);
    ASSERT_THROWS(stb::mse(a, patterned8(11, 12, stb::pixel_format::rgb, 1u)), std::invalid_argument);
    ASSERT_THROWS(stb::psnr(a, patterned8(12, 12, stb::pixel_format::rgba, 1u)), std::invalid_argument);
    ASSERT_THROWS(stb::ssim(a, patterned8(12, 12, stb::pixel_format::gray, 1u)), std::invalid_argument);
    ASSERT_THROWS(stb::mae(image8u(), image8u()), std::invalid_argument);
    ASSERT_THROWS(stb::psnr(mat32f(), mat32f()), std::invalid_argument);
}

// ------------------------------------------------------------------------------------------------------ ssim

UTEST(libstb_metrics_tests, test_ssim_of_equal_pictures_is_one_and_the_map_has_the_size_of_the_windows) {
    const mat32f a = patterned_f(30, 20, 3, 11u);
    const mat32f map = stb::ssim_map(a, a);
    EXPECT_EQ(map.width(), 20);
    EXPECT_EQ(map.height(), 10);
    EXPECT_EQ(map.channels(), 3);
    for (float v : map) EXPECT_NEAR(v, 1.0f, 1e-6f);
    EXPECT_NEAR(stb::ssim(a, a), 1.0, 1e-6);
    const image8u u = patterned8(30, 20, stb::pixel_format::rgb, 12u);
    EXPECT_NEAR(stb::ssim(u, u), 1.0, 1e-6);
}

UTEST(libstb_metrics_tests, test_ssim_matches_the_definition_in_double_precision) {
    const mat32f a = patterned_f(26, 22, 1, 21u), noise = patterned_f(26, 22, 1, 22u);
    const mat32f b = mixed(a, noise, 0.3f);
    const mat32f map = stb::ssim_map(a, b);
    ASSERT_EQ(map.width(), 16);
    ASSERT_EQ(map.height(), 12);
    double total = 0;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x) {
            const double want = ssim_at(a, b, x, y);
            ASSERT_NEAR(map(x, y), want, 1e-6);
            total += want;
        }
    EXPECT_NEAR(stb::ssim(a, b), total / double(map.width() * map.height()), 1e-6);
}

UTEST(libstb_metrics_tests, test_ssim_falls_as_the_picture_gets_noisier_and_is_symmetric) {
    const mat32f a = patterned_f(40, 40, 1, 31u), noise = patterned_f(40, 40, 1, 32u);
    const double slight = stb::ssim(a, mixed(a, noise, 0.1f));
    const double heavy = stb::ssim(a, mixed(a, noise, 0.6f));
    EXPECT_LT(heavy, slight);
    EXPECT_LT(slight, 1.0);
    EXPECT_NEAR(stb::ssim(a, noise), stb::ssim(noise, a), 1e-12);  // the products are added in another order
}

UTEST(libstb_metrics_tests, test_ssim_reads_8_bit_values_as_value_over_255) {
    const image8u a = patterned8(24, 24, stb::pixel_format::gray, 41u), b = patterned8(24, 24, stb::pixel_format::gray, 42u);
    // (the 8-bit path divides in double, cast<float> in float: they differ in the 8th digit)
    EXPECT_NEAR(stb::ssim(a, b), stb::ssim(stb::cast<float>(a), stb::cast<float>(b)), 1e-6);
}

UTEST(libstb_metrics_tests, test_ssim_needs_a_picture_of_at_least_11_by_11) {
    ASSERT_THROWS(stb::ssim(patterned8(10, 20, stb::pixel_format::gray, 1u), patterned8(10, 20, stb::pixel_format::gray, 2u)),
                  std::invalid_argument);
    ASSERT_THROWS(stb::ssim_map(patterned_f(20, 10, 1, 1u), patterned_f(20, 10, 1, 2u)), std::invalid_argument);
    EXPECT_EQ(stb::ssim_map(patterned_f(11, 11, 1, 1u), patterned_f(11, 11, 1, 2u)).width(), 1);
}

// --------------------------------------------------------------------------------------------------- threads

UTEST(libstb_metrics_tests, test_the_result_does_not_depend_on_the_number_of_threads) {
    const mat32f a = patterned_f(700, 500, 3, 51u), b = mixed(a, patterned_f(700, 500, 3, 52u), 0.2f);
    const image8u ua = patterned8(700, 500, stb::pixel_format::rgb, 53u), ub = patterned8(700, 500, stb::pixel_format::rgb, 54u);
    double mse1, ssim1, mae1;
    mat32f map1;
    {
        stb::thread_scope scope(1);
        mse1 = stb::mse(a, b);
        mae1 = stb::mae(ua, ub);
        ssim1 = stb::ssim(a, b);
        map1 = stb::ssim_map(a, b);
    }
    stb::thread_scope scope(4);
    EXPECT_EQ(stb::mse(a, b), mse1);
    EXPECT_EQ(stb::mae(ua, ub), mae1);
    EXPECT_EQ(stb::ssim(a, b), ssim1);
    EXPECT_TRUE(same_bits(stb::ssim_map(a, b), map1));
}
