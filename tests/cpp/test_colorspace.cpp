#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "stb.h"
#include "test_common.hpp"

// Colour spaces: RGB, HSV, HSL and YCbCr. The expected values of the single colours are worked out by hand.

namespace {

using stb::color_space;
using stb::image8u;
using stb::mat32f;
using stb::mat8u;

image8u patterned8(int w, int h, stb::pixel_format f, std::uint32_t seed = 91u) {
    image8u m(w, h, f);
    std::uint32_t s = seed;
    for (std::uint8_t& v : m) {
        s = s * 1664525u + 1013904223u;
        v = std::uint8_t(s >> 24);
    }
    return m;
}

mat32f patterned_f(int w, int h, int c, std::uint32_t seed = 92u) {
    mat32f m(w, h, c);
    std::uint32_t s = seed;
    for (float& v : m) {
        s = s * 1664525u + 1013904223u;
        v = float((s >> 8) & 0xFFFF) / 65535.0f;
    }
    return m;
}

mat32f one_pixel(float a, float b, float c) { return mat32f(1, 1, 3, std::vector<float>{a, b, c}); }

image8u one_pixel8(int a, int b, int c) {
    return image8u(1, 1, stb::pixel_format::rgb, std::vector<std::uint8_t>{std::uint8_t(a), std::uint8_t(b), std::uint8_t(c)});
}

bool same_bytes(const mat8u& a, const mat8u& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}
bool same_bits(const mat32f& a, const mat32f& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

template <class A, class B>
double max_diff(const A& a, const B& b) {
    double d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d = STB_MAX(d, std::fabs(double(a.data()[i]) - double(b.data()[i])));
    return d;
}

static_assert(std::is_same_v<decltype(stb::convert_color(std::declval<const image8u&>(), color_space::rgb, color_space::hsv)), image8u>);
static_assert(std::is_same_v<decltype(stb::convert_color(std::declval<const mat32f&>(), color_space::rgb, color_space::hsv)), mat32f>);
static_assert(std::is_same_v<decltype(stb::convert_color(std::declval<mat8u>(), color_space::rgb, color_space::hsv)), mat8u>);

}  // namespace

// ---------------------------------------------------------------------------------------- single colours (floats)

UTEST(libstb_colorspace_tests, test_the_primary_colours_in_hsv) {
    const struct {
        float r, g, b, h;
    } cases[] = {{1, 0, 0, 0.0f}, {1, 1, 0, 1.0f / 6.0f}, {0, 1, 0, 1.0f / 3.0f},
                 {0, 1, 1, 0.5f}, {0, 0, 1, 2.0f / 3.0f}, {1, 0, 1, 5.0f / 6.0f}};
    for (const auto& c : cases) {
        const mat32f hsv = stb::convert_color(one_pixel(c.r, c.g, c.b), color_space::rgb, color_space::hsv);
        EXPECT_NEAR(hsv(0, 0, 0), c.h, 1e-6f);
        EXPECT_NEAR(hsv(0, 0, 1), 1.0f, 1e-6f);
        EXPECT_NEAR(hsv(0, 0, 2), 1.0f, 1e-6f);
    }
}

UTEST(libstb_colorspace_tests, test_a_muted_colour_in_hsv_and_hsl) {
    // (0.5, 0.25, 0.25): max 0.5, min 0.25, range 0.25. HSV: s = 0.25 / 0.5, v = 0.5. HSL: l = 0.375, s = 0.25 / (1 - 0.25)
    const mat32f hsv = stb::convert_color(one_pixel(0.5f, 0.25f, 0.25f), color_space::rgb, color_space::hsv);
    EXPECT_NEAR(hsv(0, 0, 0), 0.0f, 1e-6f);
    EXPECT_NEAR(hsv(0, 0, 1), 0.5f, 1e-6f);
    EXPECT_NEAR(hsv(0, 0, 2), 0.5f, 1e-6f);
    const mat32f hsl = stb::convert_color(one_pixel(0.5f, 0.25f, 0.25f), color_space::rgb, color_space::hsl);
    EXPECT_NEAR(hsl(0, 0, 0), 0.0f, 1e-6f);
    EXPECT_NEAR(hsl(0, 0, 1), 1.0f / 3.0f, 1e-6f);
    EXPECT_NEAR(hsl(0, 0, 2), 0.375f, 1e-6f);
}

UTEST(libstb_colorspace_tests, test_a_gray_has_hue_0_and_saturation_0) {
    for (color_space s : {color_space::hsv, color_space::hsl}) {
        const mat32f r = stb::convert_color(one_pixel(0.4f, 0.4f, 0.4f), color_space::rgb, s);
        EXPECT_EQ(r(0, 0, 0), 0.0f);
        EXPECT_EQ(r(0, 0, 1), 0.0f);
        EXPECT_NEAR(r(0, 0, 2), 0.4f, 1e-6f);
    }
}

UTEST(libstb_colorspace_tests, test_ycbcr_of_white_red_and_black) {
    mat32f w = stb::convert_color(one_pixel(1, 1, 1), color_space::rgb, color_space::ycbcr);
    EXPECT_NEAR(w(0, 0, 0), 1.0f, 1e-6f);
    EXPECT_NEAR(w(0, 0, 1), 0.5f, 1e-6f);
    EXPECT_NEAR(w(0, 0, 2), 0.5f, 1e-6f);
    // red: Y = 0.299, Cb = 0.5 - 0.299 / 1.772, Cr = 0.5 + 0.701 / 1.402 = 1
    mat32f r = stb::convert_color(one_pixel(1, 0, 0), color_space::rgb, color_space::ycbcr);
    EXPECT_NEAR(r(0, 0, 0), 0.299f, 1e-6f);
    EXPECT_NEAR(r(0, 0, 1), 0.5f - 0.299f / 1.772f, 1e-6f);
    EXPECT_NEAR(r(0, 0, 2), 1.0f, 1e-6f);
    mat32f k = stb::convert_color(one_pixel(0, 0, 0), color_space::rgb, color_space::ycbcr);
    EXPECT_NEAR(k(0, 0, 0), 0.0f, 1e-6f);
    EXPECT_NEAR(k(0, 0, 1), 0.5f, 1e-6f);
    EXPECT_NEAR(k(0, 0, 2), 0.5f, 1e-6f);
}

UTEST(libstb_colorspace_tests, test_the_hue_of_a_float_input_wraps) {
    // hue 1 and -1/3 and 4/3 are red, blue and green
    const mat32f red = stb::convert_color(one_pixel(1.0f, 1.0f, 1.0f), color_space::hsv, color_space::rgb);
    EXPECT_NEAR(red(0, 0, 0), 1.0f, 1e-6f);
    EXPECT_NEAR(red(0, 0, 1), 0.0f, 1e-6f);
    const mat32f blue = stb::convert_color(one_pixel(-1.0f / 3.0f, 1.0f, 1.0f), color_space::hsv, color_space::rgb);
    EXPECT_NEAR(blue(0, 0, 2), 1.0f, 1e-6f);
    EXPECT_NEAR(blue(0, 0, 0), 0.0f, 1e-6f);
    const mat32f green = stb::convert_color(one_pixel(4.0f / 3.0f, 1.0f, 1.0f), color_space::hsv, color_space::rgb);
    EXPECT_NEAR(green(0, 0, 1), 1.0f, 1e-6f);
}

UTEST(libstb_colorspace_tests, test_floats_outside_the_range_are_read_as_the_nearest_end_and_a_nan_spreads_over_the_colour) {
    const mat32f r = stb::convert_color(one_pixel(2.0f, -1.0f, -3.0f), color_space::rgb, color_space::hsv);
    EXPECT_NEAR(r(0, 0, 0), 0.0f, 1e-6f);  // (1, 0, 0)
    EXPECT_NEAR(r(0, 0, 1), 1.0f, 1e-6f);
    const mat32f n = stb::convert_color(one_pixel(0.5f, std::numeric_limits<float>::quiet_NaN(), 0.2f), color_space::rgb,
                                        color_space::hsv);
    EXPECT_TRUE(std::isnan(n(0, 0, 0)) && std::isnan(n(0, 0, 1)) && std::isnan(n(0, 0, 2)));
}

// ----------------------------------------------------------------------------------------------- 8 bits

UTEST(libstb_colorspace_tests, test_the_primary_colours_in_8_bit_hsv_use_256_steps_for_the_hue) {
    const image8u red = stb::convert_color(one_pixel8(255, 0, 0), color_space::rgb, color_space::hsv);
    EXPECT_EQ(int(red(0, 0, 0)), 0);
    EXPECT_EQ(int(red(0, 0, 1)), 255);
    EXPECT_EQ(int(red(0, 0, 2)), 255);
    EXPECT_EQ(int(stb::convert_color(one_pixel8(0, 255, 0), color_space::rgb, color_space::hsv)(0, 0, 0)), 85);   // 85.33
    EXPECT_EQ(int(stb::convert_color(one_pixel8(0, 0, 255), color_space::rgb, color_space::hsv)(0, 0, 0)), 171);  // 170.67
    EXPECT_EQ(int(stb::convert_color(one_pixel8(255, 0, 255), color_space::rgb, color_space::hsv)(0, 0, 0)), 213);  // 213.33
}

UTEST(libstb_colorspace_tests, test_8_bit_ycbcr_of_white_black_and_red) {
    const image8u w = stb::convert_color(one_pixel8(255, 255, 255), color_space::rgb, color_space::ycbcr);
    EXPECT_EQ(int(w(0, 0, 0)), 255);
    EXPECT_EQ(int(w(0, 0, 1)), 128);
    EXPECT_EQ(int(w(0, 0, 2)), 128);
    const image8u r = stb::convert_color(one_pixel8(255, 0, 0), color_space::rgb, color_space::ycbcr);
    EXPECT_EQ(int(r(0, 0, 0)), 76);   // 0.299 * 255 = 76.2
    EXPECT_EQ(int(r(0, 0, 1)), 84);   // (0.5 - 0.299 / 1.772) * 255 = 84.47
    EXPECT_EQ(int(r(0, 0, 2)), 255);
}

UTEST(libstb_colorspace_tests, test_8_bit_conversions_follow_the_float_ones_within_one_level) {
    const image8u src = patterned8(37, 29, stb::pixel_format::rgb);
    const mat32f srcf = stb::cast<float>(src);
    for (color_space s : {color_space::hsl, color_space::hsv, color_space::ycbcr}) {
        const image8u got = stb::convert_color(src, color_space::rgb, s);
        const mat32f want = stb::convert_color(srcf, color_space::rgb, s);
        for (int y = 0; y < src.height(); ++y)
            for (int x = 0; x < src.width(); ++x)
                for (int k = 0; k < 3; ++k) {
                    double expected = double(want(x, y, k)) * 255.0;
                    double diff = std::fabs(double(got(x, y, k)) - expected);
                    if (k == 0 && s != color_space::ycbcr) {  // the hue has 256 steps and is circular
                        expected = double(want(x, y, k)) * 256.0;
                        diff = std::fabs(double(got(x, y, k)) - expected);
                        diff = STB_MIN(diff, 256.0 - diff);
                    }
                    ASSERT_LT(diff, 1.01);
                }
    }
}

UTEST(libstb_colorspace_tests, test_8_bit_round_trips_are_within_a_few_levels) {
    const image8u src = patterned8(60, 40, stb::pixel_format::rgb);
    const struct {
        color_space s;
        double tolerance;
    } cases[] = {{color_space::ycbcr, 3.0}, {color_space::hsv, 6.0}, {color_space::hsl, 6.0}};
    for (const auto& c : cases) {
        const image8u back = stb::convert_color(stb::convert_color(src, color_space::rgb, c.s), c.s, color_space::rgb);
        EXPECT_LE(max_diff(src, back), c.tolerance);
    }
    // grays and the grid of all levels of one channel go through hsv and hsl exactly
    image8u grays(256, 1, stb::pixel_format::rgb);
    for (int v = 0; v < 256; ++v) grays(v, 0, 0) = grays(v, 0, 1) = grays(v, 0, 2) = std::uint8_t(v);
    for (color_space s : {color_space::hsv, color_space::hsl})
        EXPECT_TRUE(same_bytes(stb::convert_color(stb::convert_color(grays, color_space::rgb, s), s, color_space::rgb), grays));
}

// ------------------------------------------------------------------------------------ floats: round trips

UTEST(libstb_colorspace_tests, test_float_round_trips_come_back) {
    const mat32f src = patterned_f(50, 30, 3);
    for (color_space s : {color_space::hsl, color_space::hsv, color_space::ycbcr}) {
        const mat32f back = stb::convert_color(stb::convert_color(src, color_space::rgb, s), s, color_space::rgb);
        EXPECT_LT(max_diff(src, back), 2e-5);
    }
    // hsv -> hsl directly goes through rgb: the lightness of a pure colour is 0.5
    const mat32f hsl = stb::convert_color(one_pixel(0.0f, 1.0f, 1.0f), color_space::hsv, color_space::hsl);
    EXPECT_NEAR(hsl(0, 0, 2), 0.5f, 1e-6f);
    EXPECT_NEAR(hsl(0, 0, 1), 1.0f, 1e-6f);
}

// --------------------------------------------------------------------------------------- alpha, forms, errors

UTEST(libstb_colorspace_tests, test_the_alpha_channel_is_never_changed) {
    const image8u src = patterned8(17, 9, stb::pixel_format::rgba);
    const mat32f srcf = patterned_f(17, 9, 4);
    for (color_space s : {color_space::hsl, color_space::hsv, color_space::ycbcr}) {
        const image8u a = stb::convert_color(src, color_space::rgb, s);
        const mat32f f = stb::convert_color(srcf, color_space::rgb, s);
        for (int y = 0; y < 9; ++y)
            for (int x = 0; x < 17; ++x) {
                ASSERT_EQ(a(x, y, 3), src(x, y, 3));
                ASSERT_EQ(f(x, y, 3), srcf(x, y, 3));
            }
    }
}

UTEST(libstb_colorspace_tests, test_the_three_forms_give_the_same_values_and_the_source_is_left_alone) {
    const image8u src = patterned8(23, 11, stb::pixel_format::rgb);
    const image8u keep(src.copy());
    const image8u copy_form = stb::convert_color(src, color_space::rgb, color_space::hsv);
    EXPECT_TRUE(same_bytes(src, keep));
    image8u moved = stb::convert_color(src.copy(), color_space::rgb, color_space::hsv);
    image8u inplace(src.copy());
    stb::convert_color_inplace(inplace, color_space::rgb, color_space::hsv);
    EXPECT_TRUE(same_bytes(copy_form, moved));
    EXPECT_TRUE(same_bytes(copy_form, inplace));
    EXPECT_TRUE(!same_bytes(copy_form, src));
}

UTEST(libstb_colorspace_tests, test_the_same_space_changes_nothing_but_still_checks_the_channels) {
    const image8u src = patterned8(8, 8, stb::pixel_format::rgb);
    EXPECT_TRUE(same_bytes(stb::convert_color(src, color_space::hsv, color_space::hsv), src));
    ASSERT_THROWS(stb::convert_color(patterned8(8, 8, stb::pixel_format::gray), color_space::rgb, color_space::rgb),
                  std::invalid_argument);
}

UTEST(libstb_colorspace_tests, test_images_without_colour_are_refused_and_left_as_they_were) {
    image8u gray = patterned8(8, 8, stb::pixel_format::gray);
    const image8u keep(gray.copy());
    ASSERT_THROWS(stb::convert_color_inplace(gray, color_space::rgb, color_space::hsv), std::invalid_argument);
    EXPECT_TRUE(same_bytes(gray, keep));
    ASSERT_THROWS(stb::convert_color(patterned8(8, 8, stb::pixel_format::gray_alpha), color_space::rgb, color_space::hsl),
                  std::invalid_argument);
    ASSERT_THROWS(stb::convert_color(mat8u(4, 4, 5), color_space::rgb, color_space::hsv), std::invalid_argument);
    ASSERT_THROWS(stb::convert_color(patterned_f(4, 4, 1), color_space::rgb, color_space::ycbcr), std::invalid_argument);
}

UTEST(libstb_colorspace_tests, test_an_empty_image_is_returned_as_it_is) {
    EXPECT_TRUE(stb::convert_color(image8u(), color_space::rgb, color_space::hsv).empty());
    EXPECT_TRUE(stb::convert_color(mat32f(), color_space::hsl, color_space::rgb).empty());
}

UTEST(libstb_colorspace_tests, test_the_result_does_not_depend_on_the_number_of_threads) {
    const image8u src = patterned8(900, 600, stb::pixel_format::rgb);
    const mat32f srcf = patterned_f(500, 400, 4);
    image8u one(src.copy());
    mat32f one_f(srcf.copy());
    {
        stb::thread_scope scope(1);
        stb::convert_color_inplace(one, color_space::rgb, color_space::hsl);
        stb::convert_color_inplace(one_f, color_space::rgb, color_space::hsv);
    }
    image8u many(src.copy());
    mat32f many_f(srcf.copy());
    stb::thread_scope scope(4);
    stb::convert_color_inplace(many, color_space::rgb, color_space::hsl);
    stb::convert_color_inplace(many_f, color_space::rgb, color_space::hsv);
    EXPECT_TRUE(same_bytes(one, many));
    EXPECT_TRUE(same_bits(one_f, many_f));
}
