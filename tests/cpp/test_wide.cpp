#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <vector>

#include "conftest.hpp"
#include "stb/colorspace.hpp"
#include "stb/filter.hpp"
#include "stb/morphology.hpp"
#include "stb/threshold.hpp"
#include "stb/tone.hpp"
#include "stb/warp.hpp"

// A "wide" pixel (an unsigned integer that is not 8 bits: 16 and 32 bits) has no code of its own in the algorithms: it goes through
// float, as cast<float> reads it, and comes back with the rounding and clamping of its type. Here the same picture is run in 8 bits
// and as a wide image and the two answers are compared in 8-bit levels (a wide result is brought back with cast<uint8_t>).

namespace {

using stb::mat32f;
using stb::mat8u;

// a picture with smooth parts, a step and some noise, made by hand (no random generator)
mat8u make_picture(int w, int h, int channels) {
    mat8u m(w, h, channels);
    std::uint32_t state = 12345u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < channels; ++c) {
                state = state * 1664525u + 1013904223u;
                int v = (x * 255 / (w - 1)) / 2 + (y * 255 / (h - 1)) / 4 + int((state >> 24) & 31u);
                if (x > w / 2 && y > h / 2) v += 60;
                m.row(y)[x * channels + c] = std::uint8_t(std::min(255, v + c * 10));
            }
    return m;
}

template <class T>
stb::mat<T> widen(const mat8u& src) {
    return stb::cast<T>(src);
}

template <class T>
mat8u narrow(const stb::mat<T>& src) {
    return mat8u(stb::cast<std::uint8_t>(src));
}

// the largest difference of two 8-bit pictures, and whether they are of the same shape
int largest_difference(const mat8u& a, const mat8u& b) {
    if (a.width() != b.width() || a.height() != b.height() || a.channels() != b.channels()) return 1000;
    int worst = 0;
    for (std::size_t i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(int(a.data()[i]) - int(b.data()[i])));
    return worst;
}

UTEST(libstb_wide_tests, test_the_functions_take_a_wide_image_and_give_back_a_wide_image) {
    static_assert(std::is_same_v<decltype(stb::gamma(std::declval<const stb::mat<std::uint16_t>&>(), 2.0f)), stb::mat<std::uint16_t>>);
    static_assert(std::is_same_v<decltype(stb::equalize(std::declval<stb::mat<std::uint32_t>>())), stb::mat<std::uint32_t>>);
    static_assert(std::is_same_v<decltype(stb::threshold(std::declval<const stb::mat<std::uint16_t>&>(), std::uint16_t(1))), mat8u>);
    static_assert(std::is_same_v<decltype(stb::otsu_threshold(std::declval<const stb::mat<std::uint16_t>&>())), std::uint16_t>);
    static_assert(stb::traits::is_pixel_input_v<stb::mat<std::uint16_t>>);
    static_assert(stb::traits::is_pixel_input_v<stb::mat<std::uint32_t>>);
    static_assert(!stb::traits::is_pixel_input_v<stb::mat<std::int16_t>>);  // a signed integer is not a picture
    static_assert(!stb::traits::is_pixel_input_v<stb::mat<double>>);
    EXPECT_TRUE(stb::traits::is_pixel_input_v<stb::mat<std::uint16_t>>);
    EXPECT_FALSE(stb::traits::is_pixel_input_v<stb::mat<std::int16_t>>);
}

UTEST(libstb_wide_tests, test_a_wide_function_changes_neither_size_nor_channels_and_leaves_its_argument_alone) {
    const stb::mat<std::uint16_t> w = widen<std::uint16_t>(make_picture(13, 9, 3));
    const stb::mat<std::uint16_t> keep = w.copy();
    const stb::mat<std::uint16_t> out = stb::gamma(w, 1.8f);
    EXPECT_EQ(13, out.width());
    EXPECT_EQ(9, out.height());
    EXPECT_EQ(3, out.channels());
    for (std::size_t i = 0; i < w.size(); ++i) ASSERT_EQ(int(keep.data()[i]), int(w.data()[i]));
}

UTEST(libstb_wide_tests, test_order_filters_are_exact_because_the_widening_keeps_the_order) {
    const mat8u src = make_picture(17, 11, 3);
    const mat8u se = stb::structuring_element(stb::morph_shape::rect, 3);
    for (int variant = 0; variant < 2; ++variant) {
        const mat8u want = variant == 0 ? stb::erode(src, se) : stb::dilate(src, se);
        const mat8u got16 = narrow(variant == 0 ? stb::erode(widen<std::uint16_t>(src), se) : stb::dilate(widen<std::uint16_t>(src), se));
        const mat8u got32 = narrow(variant == 0 ? stb::erode(widen<std::uint32_t>(src), se) : stb::dilate(widen<std::uint32_t>(src), se));
        EXPECT_EQ(0, largest_difference(want, got16));
        EXPECT_EQ(0, largest_difference(want, got32));
    }
}

UTEST(libstb_wide_tests, test_tone_functions_agree_with_the_8_bit_ones_to_a_level) {
    const mat8u src = make_picture(19, 13, 3);
    const stb::mat<std::uint16_t> w = widen<std::uint16_t>(src);
    EXPECT_LE(largest_difference(stb::gamma(src, 1.7f), narrow(stb::gamma(w, 1.7f))), 1);
    EXPECT_LE(largest_difference(stb::levels(src, 0.1f, 0.9f, 1.3f, 0.05f, 0.95f), narrow(stb::levels(w, 0.1f, 0.9f, 1.3f, 0.05f, 0.95f))), 1);
    EXPECT_LE(largest_difference(stb::auto_contrast(src, 2.0f), narrow(stb::auto_contrast(w, 2.0f))), 2);
    EXPECT_LE(largest_difference(stb::unsharp_mask(src, 1.2f, 1.0f, 0.0f), narrow(stb::unsharp_mask(w, 1.2f, 1.0f, 0.0f))), 1);
    EXPECT_LE(largest_difference(stb::pixelize(src, 3, 3), narrow(stb::pixelize(w, 3, 3))), 1);
}

UTEST(libstb_wide_tests, test_equalize_and_clahe_spread_the_levels_of_a_wide_image) {
    mat8u low(16, 16, 1);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) low.row(y)[x] = std::uint8_t(90 + (x + y) % 12);  // levels 90 to 101 only
    const stb::mat<std::uint16_t> out = stb::equalize(widen<std::uint16_t>(low));
    std::uint16_t lo = 65535, hi = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        lo = std::min(lo, out.data()[i]);
        hi = std::max(hi, out.data()[i]);
    }
    EXPECT_GT(int(hi) - int(lo), 40000);  // the 12 levels now cover most of the 16-bit range
    const stb::mat<std::uint16_t> c = stb::clahe(widen<std::uint16_t>(make_picture(32, 32, 1)), 2.0f, 4, 4);
    EXPECT_EQ(32, c.width());
}

UTEST(libstb_wide_tests, test_match_histogram_and_convert_color_of_a_wide_image) {
    const mat8u a = make_picture(15, 11, 3), b = make_picture(12, 14, 3);
    const stb::mat<std::uint16_t> m = stb::match_histogram(widen<std::uint16_t>(a), widen<std::uint16_t>(b));
    EXPECT_EQ(15, m.width());
    EXPECT_EQ(3, m.channels());
    const stb::mat<std::uint16_t> hsv = stb::convert_color(widen<std::uint16_t>(a), stb::color_space::rgb, stb::color_space::hsv);
    const stb::mat<std::uint16_t> back = stb::convert_color(hsv, stb::color_space::hsv, stb::color_space::rgb);
    EXPECT_LE(largest_difference(a, narrow(back)), 1);  // a round trip through hsv, in 16 bits
}

UTEST(libstb_wide_tests, test_threshold_is_exact_and_otsu_adaptive_canny_follow_the_8_bit_ones) {
    mat8u gray(21, 17, 1);
    {
        const mat8u p = make_picture(21, 17, 1);
        gray = p.copy();
    }
    const stb::mat<std::uint16_t> w16 = widen<std::uint16_t>(gray);
    const stb::mat<std::uint32_t> w32 = widen<std::uint32_t>(gray);
    for (int level : {0, 37, 100, 128, 200, 254}) {
        const mat8u want = stb::threshold(gray, std::uint8_t(level));
        EXPECT_EQ(0, largest_difference(want, stb::threshold(w16, std::uint16_t(level * 257))));
        EXPECT_EQ(0, largest_difference(want, stb::threshold(w32, std::uint32_t(level) * 16843009u)));
    }
    const int otsu8 = stb::otsu_threshold(gray);
    const int otsu16 = int(stb::otsu_threshold(w16)) / 257;
    EXPECT_LE(std::abs(otsu8 - otsu16), 2);
    // the offset is in levels of the type: 4 levels of 8 bits are 4 * 257 levels of 16 bits
    EXPECT_EQ(0, largest_difference(stb::adaptive_threshold(gray, 3, 4.0f), stb::adaptive_threshold(w16, 3, 4.0f * 257.0f)));
    EXPECT_EQ(0, largest_difference(stb::canny(gray), stb::canny(w16)));
}

UTEST(libstb_wide_tests, test_a_wide_image_is_warped_like_an_8_bit_one) {
    const mat8u src = make_picture(18, 12, 3);
    const stb::matrix shift = stb::matrix::translation(3.0f, 2.0f);
    const mat8u want = stb::warp_affine(src, shift, 0, 0, stb::interpolation_method::nearest);
    const mat8u got = narrow(stb::warp_affine(widen<std::uint16_t>(src), shift, 0, 0, stb::interpolation_method::nearest));
    EXPECT_EQ(0, largest_difference(want, got));
    const mat8u smooth = stb::warp_affine(src, stb::matrix::rotation(0.2f), 0, 0, stb::interpolation_method::bilinear);
    const mat8u smooth16 = narrow(stb::warp_affine(widen<std::uint16_t>(src), stb::matrix::rotation(0.2f), 0, 0, stb::interpolation_method::bilinear));
    EXPECT_LE(largest_difference(smooth, smooth16), 1);
}

}  // namespace
