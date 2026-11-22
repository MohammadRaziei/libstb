#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "stb.h"
#include "test_common.hpp"

// Tone: histograms, equalize, auto_contrast, match_histogram, gamma, levels and look-up tables. The expected
// values of the small cases are worked out by hand in the comments.

namespace {

using stb::image8u;
using stb::mat32f;
using stb::mat32i;
using stb::mat8u;
using stb::tone_mode;

// A gray image from a list of values (one row).
image8u gray_row(const std::vector<std::uint8_t>& v) {
    return image8u(int(v.size()), 1, stb::pixel_format::gray, v);
}

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
float max_diff(const mat32f& a, const mat32f& b) {
    float d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d = STB_MAX(d, std::fabs(a.data()[i] - b.data()[i]));
    return d;
}

// ----------------------------------------------------------------------------------------- the traits
static_assert(stb::traits::is_mat_v<mat8u> && stb::traits::is_mat_v<const mat32f&> && stb::traits::is_mat_v<mat32i>);
static_assert(stb::traits::is_mat_v<image8u> && stb::traits::is_mat_v<image8u&&>);
static_assert(!stb::traits::is_mat_v<int> && !stb::traits::is_mat_v<std::vector<int>> && !stb::traits::is_mat_v<void*>);
static_assert(std::is_same_v<stb::traits::mat_value_t<image8u>, std::uint8_t>);
static_assert(std::is_same_v<stb::traits::mat_value_t<const mat32f&>, float>);
static_assert(stb::traits::is_image_input_v<mat8u> && stb::traits::is_image_input_v<image8u> &&
              stb::traits::is_image_input_v<mat32f>);
static_assert(!stb::traits::is_image_input_v<mat32i> && !stb::traits::is_image_input_v<int> &&
              !stb::traits::is_image_input_v<std::vector<int>>);
static_assert(std::is_same_v<stb::traits::if_image_t<image8u>, image8u>);
static_assert(std::is_same_v<stb::traits::if_image_t<mat8u, image8u>, image8u>);
// What comes out is what went in.
static_assert(std::is_same_v<decltype(stb::equalize(std::declval<const mat8u&>())), mat8u>);
static_assert(std::is_same_v<decltype(stb::equalize(std::declval<const image8u&>())), image8u>);
static_assert(std::is_same_v<decltype(stb::equalize(std::declval<const mat32f&>())), mat32f>);
static_assert(std::is_same_v<decltype(stb::equalize(std::declval<image8u>())), image8u>);
static_assert(std::is_same_v<decltype(stb::gamma(std::declval<mat32f>(), 2.0f)), mat32f>);
static_assert(std::is_same_v<decltype(stb::equalize_inplace(std::declval<image8u&>())), image8u&>);
static_assert(std::is_same_v<decltype(stb::clahe_inplace(std::declval<image8u&>())), image8u&>);
static_assert(std::is_same_v<decltype(stb::clahe(std::declval<const image8u&>())), image8u>);
static_assert(std::is_same_v<decltype(stb::clahe(std::declval<const mat32f&>(), 3.0f, 4, 2)), mat32f>);
static_assert(std::is_same_v<decltype(stb::clahe(std::declval<image8u>())), image8u>);

// The *_inplace functions are built for mat8u (so image8u) and mat32f only, and never for a const.
template <class M, class = void>
struct can_equalize_inplace : std::false_type {};
template <class M>
struct can_equalize_inplace<M, std::void_t<decltype(stb::equalize_inplace(std::declval<M&>()))>> : std::true_type {};
static_assert(can_equalize_inplace<mat8u>::value && can_equalize_inplace<image8u>::value && can_equalize_inplace<mat32f>::value);
static_assert(!can_equalize_inplace<mat32i>::value && !can_equalize_inplace<const mat8u>::value &&
              !can_equalize_inplace<const image8u>::value && !can_equalize_inplace<int>::value);

}  // namespace

// ----------------------------------------------------------------------------------------------- histogram

UTEST(libstb_tone_tests, test_a_histogram_counts_each_level_of_each_channel) {
    // 2 x 1 RGB: (10, 20, 30) and (10, 25, 30)
    const image8u img(2, 1, stb::pixel_format::rgb, std::vector<std::uint8_t>{10, 20, 30, 10, 25, 30});
    const mat32i h = stb::histogram(img);
    ASSERT_EQ(h.width(), 256);
    ASSERT_EQ(h.height(), 1);
    ASSERT_EQ(h.channels(), 3);
    ASSERT_EQ(h(10, 0, 0), 2);
    ASSERT_EQ(h(20, 0, 1), 1);
    ASSERT_EQ(h(25, 0, 1), 1);
    ASSERT_EQ(h(30, 0, 2), 2);
    std::int64_t sum = 0;
    for (std::int32_t v : h) sum += v;
    ASSERT_EQ(sum, 6);
}

UTEST(libstb_tone_tests, test_a_float_histogram_clamps_and_skips_nan) {
    // bins of width 0.25: 0 -> bin 0, 0.3 -> bin 1, 1 -> bin 3 (the last includes 1), 5 -> bin 3, -2 -> bin 0
    const mat32f m(6, 1, 1, std::vector<float>{0.0f, 0.3f, 1.0f, 5.0f, -2.0f, std::numeric_limits<float>::quiet_NaN()});
    const mat32i h = stb::histogram(m, 4);
    ASSERT_EQ(h.width(), 4);
    ASSERT_EQ(h(0, 0), 2);
    ASSERT_EQ(h(1, 0), 1);
    ASSERT_EQ(h(2, 0), 0);
    ASSERT_EQ(h(3, 0), 2);
    ASSERT_EQ(stb::histogram(m).width(), 256);  // the default
}

UTEST(libstb_tone_tests, test_histogram_of_nothing_and_bad_bins) {
    ASSERT_TRUE(stb::histogram(mat8u()).empty());
    ASSERT_TRUE(stb::histogram(mat32f()).empty());
    const mat32f m(2, 2, 1);
    ASSERT_THROWS(stb::histogram(m, 0), std::invalid_argument);
    ASSERT_THROWS(stb::histogram(m, 65537), std::invalid_argument);
    // a mat of any number of channels has a histogram: it is not an image function
    ASSERT_EQ(stb::histogram(mat8u(2, 2, 6)).channels(), 6);
}

// ----------------------------------------------------------------------------------------------- tables

UTEST(libstb_tone_tests, test_gamma_and_levels_tables) {
    const mat8u id = stb::gamma_lut(1.0f);
    ASSERT_EQ(id.width(), 256);
    ASSERT_EQ(id.channels(), 1);
    for (int i = 0; i < 256; ++i) ASSERT_EQ(int(id(i, 0)), i);
    const mat8u g2 = stb::gamma_lut(2.0f);  // 255 * sqrt(i / 255); i = 64: 127.75 -> 128
    ASSERT_EQ(int(g2(0, 0)), 0);
    ASSERT_EQ(int(g2(64, 0)), 128);
    ASSERT_EQ(int(g2(255, 0)), 255);
    const mat8u g_half = stb::gamma_lut(0.5f);  // 255 * (i / 255)^2; i = 128: 64.25 -> 64
    ASSERT_EQ(int(g_half(128, 0)), 64);

    const mat8u lv = stb::levels_lut(0.2f, 0.8f);  // 51 -> 0, 204 -> 255, 127.5 -> 127.5 (the middle)
    ASSERT_EQ(int(lv(0, 0)), 0);
    ASSERT_EQ(int(lv(51, 0)), 0);
    ASSERT_EQ(int(lv(204, 0)), 255);
    ASSERT_EQ(int(lv(255, 0)), 255);
    ASSERT_TRUE(int(lv(127, 0)) >= 126 && int(lv(128, 0)) <= 129);
    const mat8u inv = stb::levels_lut(0.0f, 1.0f, 1.0f, 1.0f, 0.0f);  // out_black > out_white inverts
    ASSERT_EQ(int(inv(0, 0)), 255);
    ASSERT_EQ(int(inv(255, 0)), 0);
}

UTEST(libstb_tone_tests, test_bad_arguments_throw_before_anything_changes) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    ASSERT_THROWS(stb::gamma_lut(0.0f), std::invalid_argument);
    ASSERT_THROWS(stb::gamma_lut(-1.0f), std::invalid_argument);
    ASSERT_THROWS(stb::gamma_lut(nan), std::invalid_argument);
    ASSERT_THROWS(stb::gamma_lut(std::numeric_limits<float>::infinity()), std::invalid_argument);
    ASSERT_THROWS(stb::levels_lut(0.5f, 0.5f), std::invalid_argument);   // empty input range
    ASSERT_THROWS(stb::levels_lut(0.6f, 0.4f), std::invalid_argument);
    ASSERT_THROWS(stb::levels_lut(-0.1f, 0.4f), std::invalid_argument);
    ASSERT_THROWS(stb::levels_lut(0.1f, 1.5f), std::invalid_argument);
    ASSERT_THROWS(stb::levels_lut(0.1f, 0.9f, 1.0f, 0.0f, 2.0f), std::invalid_argument);
    ASSERT_THROWS(stb::levels_lut(nan, 0.9f), std::invalid_argument);

    image8u img = patterned8(5, 4, stb::pixel_format::rgb);
    const image8u before(img.copy());
    ASSERT_THROWS(stb::auto_contrast_inplace(img, 50.0f), std::invalid_argument);
    ASSERT_THROWS(stb::auto_contrast_inplace(img, -1.0f), std::invalid_argument);
    ASSERT_THROWS(stb::auto_contrast_inplace(img, nan), std::invalid_argument);
    ASSERT_THROWS(stb::gamma_inplace(img, 0.0f), std::invalid_argument);
    ASSERT_THROWS(stb::levels_inplace(img, 0.9f, 0.1f), std::invalid_argument);
    ASSERT_THROWS(stb::match_histogram_inplace(img, mat8u()), std::invalid_argument);                     // empty reference
    ASSERT_THROWS(stb::match_histogram_inplace(img, patterned8(3, 3, stb::pixel_format::gray)), std::invalid_argument);
    ASSERT_TRUE(same_bytes(img, before));
}

UTEST(libstb_tone_tests, test_apply_lut_with_one_table_and_one_table_per_channel) {
    const image8u img(2, 1, stb::pixel_format::rgba, std::vector<std::uint8_t>{0, 100, 200, 255, 10, 20, 30, 40});
    mat8u invert(256, 1, 1);
    for (int i = 0; i < 256; ++i) invert(i, 0) = std::uint8_t(255 - i);
    const image8u a = stb::apply_lut(img, invert);  // one table: all four channels, alpha too
    const std::vector<std::uint8_t> want_a = {255, 155, 55, 0, 245, 235, 225, 215};
    ASSERT_TRUE(std::memcmp(a.data(), want_a.data(), want_a.size()) == 0);

    mat8u per(256, 1, 4);  // channel k adds k (saturating at 255)
    for (int i = 0; i < 256; ++i)
        for (int k = 0; k < 4; ++k) per(i, 0, k) = std::uint8_t(STB_MIN(255, i + k));
    const image8u b = stb::apply_lut(img, per);
    const std::vector<std::uint8_t> want_b = {0, 101, 202, 255, 10, 21, 32, 43};
    ASSERT_TRUE(std::memcmp(b.data(), want_b.data(), want_b.size()) == 0);

    ASSERT_THROWS(stb::apply_lut(img, mat8u(255, 1, 1)), std::invalid_argument);
    ASSERT_THROWS(stb::apply_lut(img, mat8u(256, 2, 1)), std::invalid_argument);
    ASSERT_THROWS(stb::apply_lut(img, mat8u(256, 1, 3)), std::invalid_argument);  // 3 tables for 4 channels
    ASSERT_THROWS(stb::apply_lut(img, mat8u()), std::invalid_argument);
    ASSERT_TRUE(stb::apply_lut(mat8u(), invert).empty());
}

// ----------------------------------------------------------------------------------------------- equalize

UTEST(libstb_tone_tests, test_equalize_8_bits_matches_the_hand_computed_table) {
    // values 0 0 1 1 2 2 3 3: n = 8, cdf_min = 2, so level v -> ((cdf(v) - 2) * 255 + 3) / 6 = 0, 85, 170, 255
    const image8u out = stb::equalize(gray_row({0, 0, 1, 1, 2, 2, 3, 3}));
    const std::vector<std::uint8_t> want = {0, 0, 85, 85, 170, 170, 255, 255};
    ASSERT_TRUE(std::memcmp(out.data(), want.data(), want.size()) == 0);
}

UTEST(libstb_tone_tests, test_equalize_leaves_a_flat_picture_and_an_empty_one_alone) {
    const image8u flat = gray_row({7, 7, 7, 7});
    ASSERT_TRUE(same_bytes(stb::equalize(flat), flat));
    ASSERT_TRUE(stb::equalize(image8u()).empty());
    mat32f flat_f(3, 1, 1, std::vector<float>{0.4f, 0.4f, 0.4f});
    ASSERT_TRUE(same_bits(stb::equalize(flat_f), flat_f));
}

UTEST(libstb_tone_tests, test_the_alpha_channel_is_never_touched) {
    image8u rgba = patterned8(9, 7, stb::pixel_format::rgba);
    image8u ga = patterned8(9, 7, stb::pixel_format::gray_alpha);
    const image8u rgba0(rgba.copy()), ga0(ga.copy());
    for (tone_mode mode : {tone_mode::luma, tone_mode::per_channel}) {
        const image8u a = stb::equalize(rgba, mode), b = stb::auto_contrast(rgba, 5.0f, mode),
                      c = stb::equalize(ga, mode);
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 9; ++x) {
                ASSERT_EQ(int(a(x, y, 3)), int(rgba0(x, y, 3)));
                ASSERT_EQ(int(b(x, y, 3)), int(rgba0(x, y, 3)));
                ASSERT_EQ(int(c(x, y, 1)), int(ga0(x, y, 1)));
            }
    }
    const image8u g = stb::gamma(rgba, 2.0f), l = stb::levels(rgba, 0.2f, 0.8f);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x) {
            ASSERT_EQ(int(g(x, y, 3)), int(rgba0(x, y, 3)));
            ASSERT_EQ(int(l(x, y, 3)), int(rgba0(x, y, 3)));
        }
    ASSERT_TRUE(same_bytes(rgba, rgba0));  // the const& form did not change its argument
}

UTEST(libstb_tone_tests, test_luma_mode_of_gray_rgb_equals_equalizing_the_gray_image) {
    const image8u g = patterned8(16, 8, stb::pixel_format::gray, 99u);
    image8u rgb(16, 8, stb::pixel_format::rgb);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x)
            for (int k = 0; k < 3; ++k) rgb(x, y, k) = g(x, y);
    const image8u eg = stb::equalize(g);
    const image8u er = stb::equalize(rgb, tone_mode::luma);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x)
            for (int k = 0; k < 3; ++k) ASSERT_EQ(int(er(x, y, k)), int(eg(x, y)));
}

UTEST(libstb_tone_tests, test_luma_mode_keeps_the_hue_that_per_channel_may_shift) {
    // A reddish picture: per channel each channel is stretched on its own, luma moves all three together,
    // so the difference between channels (before clamping at the ends) stays.
    image8u img(8, 1, stb::pixel_format::rgb);
    for (int x = 0; x < 8; ++x) {
        img(x, 0, 0) = std::uint8_t(100 + 8 * x);
        img(x, 0, 1) = std::uint8_t(60 + 8 * x);
        img(x, 0, 2) = std::uint8_t(40 + 8 * x);
    }
    const image8u l = stb::equalize(img, tone_mode::luma);
    for (int x = 0; x < 8; ++x) {
        const int dr = int(l(x, 0, 0)) - int(img(x, 0, 0)), dg = int(l(x, 0, 1)) - int(img(x, 0, 1)),
                  db = int(l(x, 0, 2)) - int(img(x, 0, 2));
        ASSERT_TRUE(dr == dg || l(x, 0, 0) == 255 || l(x, 0, 1) == 0);  // the same amount, unless clamped
        ASSERT_TRUE(dg == db || l(x, 0, 1) == 255 || l(x, 0, 2) == 0 || l(x, 0, 2) == 255);
    }
    const image8u p = stb::equalize(img, tone_mode::per_channel);
    // each channel on its own became a ramp over the whole range
    for (int k = 0; k < 3; ++k) {
        ASSERT_EQ(int(p(0, 0, k)), 0);
        ASSERT_EQ(int(p(7, 0, k)), 255);
    }
}

UTEST(libstb_tone_tests, test_more_than_four_channels_throw_and_change_nothing) {
    mat8u m(3, 3, 5);
    for (std::uint8_t& v : m) v = 9;
    ASSERT_THROWS(stb::equalize(m), std::invalid_argument);
    ASSERT_THROWS(stb::gamma(m, 2.0f), std::invalid_argument);
    ASSERT_THROWS(stb::equalize_inplace(m), std::invalid_argument);
    for (std::uint8_t v : m) ASSERT_EQ(int(v), 9);
}

// ---------------------------------------------------------------------------------------------- the three forms

UTEST(libstb_tone_tests, test_the_copy_the_rvalue_and_the_inplace_form_give_the_same_bytes) {
    const image8u src = patterned8(37, 23, stb::pixel_format::rgb, 5u);
    const image8u reference = patterned8(11, 9, stb::pixel_format::rgb, 6u);

    const auto check = [&](auto make_copy, auto make_rvalue, auto make_inplace) {
        const image8u before(src.copy());
        const image8u a = make_copy(src);
        ASSERT_TRUE(same_bytes(src, before));  // the const& form left its argument alone
        image8u r(src.copy());
        const std::uint8_t* buffer = r.data();
        const image8u b = make_rvalue(std::move(r));
        ASSERT_TRUE(b.data() == buffer);  // and the rvalue form worked in the buffer it was given
        image8u c(src.copy());
        make_inplace(c);
        ASSERT_TRUE(same_bytes(a, b));
        ASSERT_TRUE(same_bytes(a, c));
        ASSERT_FALSE(same_bytes(a, src));  // and did something
    };
    check([](const image8u& s) { return stb::equalize(s); }, [](image8u&& s) { return stb::equalize(std::move(s)); },
          [](image8u& s) { stb::equalize_inplace(s); });
    check([](const image8u& s) { return stb::auto_contrast(s, 2.0f); },
          [](image8u&& s) { return stb::auto_contrast(std::move(s), 2.0f); },
          [](image8u& s) { stb::auto_contrast_inplace(s, 2.0f); });
    check([&](const image8u& s) { return stb::match_histogram(s, reference); },
          [&](image8u&& s) { return stb::match_histogram(std::move(s), reference); },
          [&](image8u& s) { stb::match_histogram_inplace(s, reference); });
    check([](const image8u& s) { return stb::gamma(s, 1.7f); }, [](image8u&& s) { return stb::gamma(std::move(s), 1.7f); },
          [](image8u& s) { stb::gamma_inplace(s, 1.7f); });
    check([](const image8u& s) { return stb::levels(s, 0.1f, 0.7f, 1.3f, 0.05f, 0.9f); },
          [](image8u&& s) { return stb::levels(std::move(s), 0.1f, 0.7f, 1.3f, 0.05f, 0.9f); },
          [](image8u& s) { stb::levels_inplace(s, 0.1f, 0.7f, 1.3f, 0.05f, 0.9f); });
}

UTEST(libstb_tone_tests, test_a_temporary_is_reused_down_a_chain) {
    const image8u src = patterned8(20, 10, stb::pixel_format::rgb, 8u);
    const image8u chained = stb::equalize(stb::gamma(src.copy(), 1.4f));
    image8u step = stb::gamma(src, 1.4f);
    stb::equalize_inplace(step);
    ASSERT_TRUE(same_bytes(chained, step));
}

UTEST(libstb_tone_tests, test_the_forms_work_on_mat8u_mat32f_and_on_views_of_foreign_memory) {
    std::vector<std::uint8_t> bytes = {0, 0, 1, 1, 2, 2, 3, 3};
    mat8u view = mat8u::wrap(8, 1, 1, bytes.data());  // someone else's memory
    stb::equalize_inplace(view);
    const std::vector<std::uint8_t> want = {0, 0, 85, 85, 170, 170, 255, 255};
    ASSERT_TRUE(bytes == want);  // the memory of the owner changed

    mat32f f = patterned_f(10, 5, 3);
    const mat32f f2 = stb::equalize(f);
    mat32f f3(f.copy());
    stb::equalize_inplace(f3);
    ASSERT_TRUE(same_bits(f2, f3));
    const mat32f f4 = stb::gamma(std::move(f3), 2.0f);
    ASSERT_TRUE(f3.empty());  // moved from
    ASSERT_EQ(f4.channels(), 3);
}

// --------------------------------------------------------------------------------------------- auto_contrast

UTEST(libstb_tone_tests, test_auto_contrast_stretches_the_range_to_the_ends) {
    // 50 100 150 -> (v - 50) * 255 / 100 = 0, 127.5 -> 128, 255
    const image8u out = stb::auto_contrast(gray_row({50, 100, 150}));
    const std::vector<std::uint8_t> want = {0, 128, 255};
    ASSERT_TRUE(std::memcmp(out.data(), want.data(), 3) == 0);
    // already full range: nothing to do; a single value: nothing to do
    const image8u full = gray_row({0, 90, 255});
    ASSERT_TRUE(same_bytes(stb::auto_contrast(full), full));
    const image8u flat = gray_row({9, 9, 9});
    ASSERT_TRUE(same_bytes(stb::auto_contrast(flat), flat));
}

UTEST(libstb_tone_tests, test_auto_contrast_cutoff_drops_the_ends_like_pillow) {
    // 100 values: ninety-eight 100s... build 0 x 1, 100 x 98, 200 x 1: a cutoff of 1 % drops the outliers
    std::vector<std::uint8_t> v(100, 100);
    v[0] = 0;
    v[99] = 200;
    const image8u img = gray_row(v);
    const image8u cut = stb::auto_contrast(img, 1.0f);  // 1 value is cut at each end: lo = hi = 100: unchanged
    ASSERT_TRUE(same_bytes(cut, img));
    const image8u no_cut = stb::auto_contrast(img, 0.0f);  // 0 -> 0, 100 -> 128 (127.5), 200 -> 255
    ASSERT_EQ(int(no_cut(0, 0)), 0);
    ASSERT_EQ(int(no_cut(50, 0)), 128);
    ASSERT_EQ(int(no_cut(99, 0)), 255);
    // a bigger cutoff clips: 20 % of 10 values is 2 at each end
    const image8u ten = gray_row({10, 20, 30, 40, 50, 60, 70, 80, 90, 100});
    const image8u c = stb::auto_contrast(ten, 20.0f);  // lo = 30, hi = 80: (v - 30) * 255 / 50, clamped
    ASSERT_EQ(int(c(0, 0)), 0);
    ASSERT_EQ(int(c(2, 0)), 0);
    ASSERT_EQ(int(c(3, 0)), 51);  // 40
    ASSERT_EQ(int(c(7, 0)), 255);
    ASSERT_EQ(int(c(9, 0)), 255);
}

// ------------------------------------------------------------------------------------------ match_histogram

UTEST(libstb_tone_tests, test_matching_a_picture_to_itself_changes_nothing) {
    const image8u img = patterned8(40, 30, stb::pixel_format::rgb, 3u);
    for (tone_mode mode : {tone_mode::per_channel}) {
        const image8u out = stb::match_histogram(img, img, mode);
        ASSERT_TRUE(same_bytes(out, img));
    }
    const image8u g = patterned8(40, 30, stb::pixel_format::gray, 4u);
    ASSERT_TRUE(same_bytes(stb::match_histogram(g, g), g));
}

UTEST(libstb_tone_tests, test_matching_a_dark_picture_to_a_bright_one_brightens_it) {
    image8u dark(16, 16, stb::pixel_format::gray), bright(8, 8, stb::pixel_format::gray);
    for (int i = 0; i < 256; ++i) dark.data()[i] = std::uint8_t(i / 4);        // 0..63
    for (int i = 0; i < 64; ++i) bright.data()[i] = std::uint8_t(192 + i);      // 192..255 (another size)
    const image8u out = stb::match_histogram(dark, bright);
    ASSERT_TRUE(int(out(0, 0)) >= 192);
    ASSERT_EQ(int(out(15, 15)), 255);
    for (int i = 1; i < 256; ++i) ASSERT_TRUE(out.data()[i] >= out.data()[i - 1]);  // order is kept
}

// --------------------------------------------------------------------------------------------- gamma / levels

UTEST(libstb_tone_tests, test_gamma_and_levels_on_an_image_use_the_tables) {
    const image8u img = patterned8(12, 6, stb::pixel_format::rgb, 11u);
    const mat8u g = stb::gamma_lut(1.8f), l = stb::levels_lut(0.1f, 0.7f, 1.3f, 0.05f, 0.9f);
    ASSERT_TRUE(same_bytes(stb::gamma(img, 1.8f), stb::apply_lut(img, g)));
    ASSERT_TRUE(same_bytes(stb::levels(img, 0.1f, 0.7f, 1.3f, 0.05f, 0.9f), stb::apply_lut(img, l)));
    ASSERT_TRUE(same_bytes(stb::gamma(img, 1.0f), img));
}

UTEST(libstb_tone_tests, test_gamma_and_levels_on_floats_follow_the_formulas) {
    const mat32f m(4, 1, 1, std::vector<float>{0.0f, 0.25f, 1.0f, 0.5f});
    const mat32f g = stb::gamma(m, 2.0f);  // sqrt
    ASSERT_TRUE(std::fabs(g(1, 0) - 0.5f) < 1e-6f);
    ASSERT_EQ(g(0, 0), 0.0f);
    ASSERT_EQ(g(2, 0), 1.0f);
    const mat32f l = stb::levels(m, 0.25f, 0.75f);  // (v - 0.25) / 0.5, clamped
    ASSERT_EQ(l(0, 0), 0.0f);
    ASSERT_EQ(l(1, 0), 0.0f);
    ASSERT_EQ(l(2, 0), 1.0f);
    ASSERT_TRUE(std::fabs(l(3, 0) - 0.5f) < 1e-6f);
    ASSERT_TRUE(same_bits(stb::gamma(m, 1.0f), m));
}

// ------------------------------------------------------------------------------------------------------ floats

UTEST(libstb_tone_tests, test_float_values_outside_the_range_are_clamped_and_nan_passes_through) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    mat32f m(5, 1, 1, std::vector<float>{-3.0f, 0.2f, 0.7f, 9.0f, nan});
    const mat32f e = stb::equalize(m);
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(e(i, 0) >= 0.0f && e(i, 0) <= 1.0f);
    ASSERT_TRUE(std::isnan(e(4, 0)));
    const mat32f a = stb::auto_contrast(m);
    ASSERT_EQ(a(0, 0), 0.0f);
    ASSERT_EQ(a(3, 0), 1.0f);
    ASSERT_TRUE(std::isnan(a(4, 0)));
    const mat32f g = stb::gamma(m, 2.0f);
    ASSERT_EQ(g(0, 0), 0.0f);
    ASSERT_EQ(g(3, 0), 1.0f);
    ASSERT_TRUE(std::isnan(g(4, 0)));
    // a luma image with a NaN in one pixel: that pixel stays, the others are processed
    mat32f rgb(3, 1, 3, std::vector<float>{0.1f, 0.2f, 0.3f, nan, 0.5f, 0.5f, 0.7f, 0.8f, 0.9f});
    const mat32f r = stb::equalize(rgb);
    ASSERT_TRUE(std::isnan(r(1, 0, 0)));
    ASSERT_EQ(r(1, 0, 1), 0.5f);
}

UTEST(libstb_tone_tests, test_equalizing_a_uniform_float_ramp_changes_little) {
    mat32f ramp(4096, 1, 1);
    for (int i = 0; i < 4096; ++i) ramp(i, 0) = (float(i) + 0.5f) / 4096.0f;
    ASSERT_TRUE(max_diff(stb::equalize(ramp), ramp) < 0.01f);
    // a float auto_contrast of the ramp stretches (0.5 / 4096 .. 4095.5 / 4096) to the ends
    const mat32f a = stb::auto_contrast(ramp);
    ASSERT_EQ(a(0, 0), 0.0f);
    ASSERT_EQ(a(4095, 0), 1.0f);
}

UTEST(libstb_tone_tests, test_float_match_histogram_to_itself_and_toward_a_brighter_reference) {
    const mat32f m = patterned_f(30, 20, 1, 21u);
    ASSERT_TRUE(max_diff(stb::match_histogram(m, m), m) < 0.01f);
    mat32f bright(10, 10, 1);
    for (std::size_t i = 0; i < bright.size(); ++i) bright.data()[i] = 0.75f + 0.25f * float(i) / float(bright.size());
    const mat32f out = stb::match_histogram(m, bright);
    for (float v : out) ASSERT_TRUE(v >= 0.74f && v <= 1.0f);
    ASSERT_THROWS(stb::match_histogram(m, mat32f(2, 2, 3)), std::invalid_argument);
    ASSERT_THROWS(stb::match_histogram(m, mat32f()), std::invalid_argument);
}

UTEST(libstb_tone_tests, test_float_forms_agree_with_each_other) {
    const mat32f src = patterned_f(33, 17, 4, 2u);
    const mat32f ref = patterned_f(9, 9, 4, 3u);
    mat32f in_place(src.copy());
    stb::auto_contrast_inplace(in_place, 3.0f);
    ASSERT_TRUE(same_bits(in_place, stb::auto_contrast(src, 3.0f)));
    ASSERT_TRUE(same_bits(stb::match_histogram(src, ref), stb::match_histogram(src.copy(), ref)));
    ASSERT_TRUE(same_bits(stb::levels(src, 0.1f, 0.9f, 1.2f), stb::levels(src.copy(), 0.1f, 0.9f, 1.2f)));
    for (int y = 0; y < 17; ++y)
        for (int x = 0; x < 33; ++x) ASSERT_EQ(in_place(x, y, 3), src(x, y, 3));  // alpha of a float RGBA image
}

// ------------------------------------------------------------------------------------------------- clahe

namespace {
std::uint8_t at8(const mat8u& m, int x, int y, int k = 0) { return m.row(y)[std::size_t(x) * std::size_t(m.channels()) + std::size_t(k)]; }
}  // namespace

UTEST(libstb_tone_tests, test_clahe_cuts_the_histogram_and_shares_the_excess_as_computed_by_hand) {
    // 8 x 1 gray: seven 0 and one 255, one tile, clip_limit 1: clip = max(1, int(1 * 8 / 256)) = 1.
    // bin 0 has 7: 6 are cut. 6 / 256 = 0 for every bin and the 6 left go to bins 0, 42, 84, 126, 168, 210
    // (step 256 / 6 = 42). So bin 0 holds 2 and 255 holds 1: the cumulative share at 0 is 2 / 8 = 0.25 and at 255 it is 1.
    // 0.25 * 255 = 63.75 and the result is rounded: 64; 255 stays 255.
    const image8u img = gray_row({0, 0, 0, 0, 0, 0, 0, 255});
    const image8u out = stb::clahe(img, 1.0f, 1);
    for (int x = 0; x < 7; ++x) ASSERT_EQ(int(at8(out, x, 0)), 64);
    ASSERT_EQ(int(at8(out, 7, 0)), 255);
}

UTEST(libstb_tone_tests, test_clahe_of_one_tile_without_a_cut_is_the_cumulative_histogram) {
    const image8u img = patterned8(16, 12, stb::pixel_format::gray, 41u);
    const image8u out = stb::clahe(img, 1.0e6f, 1);  // a limit nobody reaches: plain equalisation by the cumulative share
    std::vector<double> cum(256, 0.0);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 16; ++x) cum[at8(img, x, y)] += 1.0;
    for (int v = 1; v < 256; ++v) cum[std::size_t(v)] += cum[std::size_t(v) - 1];
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 16; ++x) {
            const double want = 255.0 * cum[at8(img, x, y)] / 192.0;
            ASSERT_TRUE(std::fabs(double(at8(out, x, y)) - want) <= 0.5 + 1e-3);
        }
}

UTEST(libstb_tone_tests, test_clahe_tiles_are_local) {
    // the left half is dark, the right half bright; with two tiles each half is spread on its own
    std::vector<std::uint8_t> v;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x) v.push_back(std::uint8_t(x < 4 ? 10 * (x + 1) : 200 + 10 * (x - 3)));
    const image8u img(8, 4, stb::pixel_format::gray, v);
    const image8u global = stb::clahe(img, 1.0e6f, 1);
    const image8u local = stb::clahe(img, 1.0e6f, 2, 1);
    ASSERT_EQ(int(at8(img, 3, 0)), 40);
    ASSERT_TRUE(at8(global, 3, 0) < 200);  // the brightest of the dark half is only half way up in the whole picture
    ASSERT_EQ(int(at8(local, 0, 0)), 64);  // column 0 is before the first centre: the left tile alone, 10 is a quarter up
    ASSERT_EQ(int(at8(local, 7, 0)), 255);
}

UTEST(libstb_tone_tests, test_clahe_leaves_a_flat_picture_and_an_empty_one_alone) {
    image8u flat(9, 7, stb::pixel_format::rgb);
    for (std::uint8_t& v : flat) v = 77;
    ASSERT_TRUE(same_bytes(stb::clahe(flat, 2.0f, 3), flat));
    mat32f flat_f(9, 7, 3);
    for (float& v : flat_f) v = 0.37f;
    ASSERT_TRUE(same_bits(stb::clahe(flat_f, 2.0f, 3), flat_f));
    // as many tiles as pixels: every tile has one value
    const image8u img = patterned8(4, 4, stb::pixel_format::gray, 5u);
    ASSERT_TRUE(same_bytes(stb::clahe(img, 2.0f, 4, 4), img));
    ASSERT_TRUE(stb::clahe(image8u(), 2.0f, 3).empty());
}

UTEST(libstb_tone_tests, test_clahe_bad_arguments_throw_and_change_nothing) {
    image8u img = patterned8(10, 8, stb::pixel_format::rgb, 6u);
    const image8u before(img.copy());
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    ASSERT_THROWS(stb::clahe_inplace(img, 0.5f), std::invalid_argument);
    ASSERT_THROWS(stb::clahe_inplace(img, nan), std::invalid_argument);
    ASSERT_THROWS(stb::clahe_inplace(img, inf), std::invalid_argument);
    ASSERT_THROWS(stb::clahe_inplace(img, 2.0f, 0), std::invalid_argument);
    ASSERT_THROWS(stb::clahe_inplace(img, 2.0f, 4, -1), std::invalid_argument);
    ASSERT_THROWS(stb::clahe_inplace(img, 2.0f, 11, 1), std::invalid_argument);  // wider than the picture
    ASSERT_THROWS(stb::clahe_inplace(img, 2.0f, 2, 9), std::invalid_argument);   // taller than the picture
    ASSERT_TRUE(same_bytes(img, before));
    mat8u five(4, 4, 5);
    ASSERT_THROWS(stb::clahe_inplace(five, 2.0f, 2), std::invalid_argument);
}

UTEST(libstb_tone_tests, test_clahe_never_touches_alpha_and_luma_of_gray_rgb_equals_the_gray_result) {
    const image8u rgba = patterned8(21, 13, stb::pixel_format::rgba, 8u);
    const image8u out = stb::clahe(rgba, 3.0f, 3, 2);
    for (int y = 0; y < 13; ++y)
        for (int x = 0; x < 21; ++x) ASSERT_EQ(int(at8(out, x, y, 3)), int(at8(rgba, x, y, 3)));
    const image8u gray = patterned8(21, 13, stb::pixel_format::gray, 9u);
    image8u grey_rgb(21, 13, stb::pixel_format::rgb);
    for (int y = 0; y < 13; ++y)
        for (int x = 0; x < 21; ++x)
            for (int k = 0; k < 3; ++k) grey_rgb.row(y)[std::size_t(x) * 3 + std::size_t(k)] = at8(gray, x, y);
    const image8u g = stb::clahe(gray, 3.0f, 3, 2);
    const image8u c = stb::clahe(grey_rgb, 3.0f, 3, 2);
    for (int y = 0; y < 13; ++y)
        for (int x = 0; x < 21; ++x)
            for (int k = 0; k < 3; ++k) ASSERT_EQ(int(at8(c, x, y, k)), int(at8(g, x, y)));
}

UTEST(libstb_tone_tests, test_clahe_copy_rvalue_and_inplace_forms_give_the_same_bytes_and_floats_follow_the_formula) {
    const image8u src = patterned8(33, 21, stb::pixel_format::rgb, 10u);
    image8u a(src.copy()), b(src.copy());
    const image8u copy = stb::clahe(src, 2.5f, 4, 3);
    stb::clahe_inplace(a, 2.5f, 4, 3);
    const std::uint8_t* before = b.data();
    const image8u moved = stb::clahe(std::move(b), 2.5f, 4, 3);
    ASSERT_TRUE(same_bytes(copy, a));
    ASSERT_TRUE(same_bytes(copy, moved));
    ASSERT_TRUE(moved.data() == before);  // the buffer was reused

    // floats, one tile, no cut: the cumulative share interpolated between the 256 bin edges
    const mat32f f = patterned_f(20, 15, 1, 11u);
    const mat32f out = stb::clahe(f, 1.0e6f, 1);
    std::vector<double> count(256, 0.0);
    for (std::size_t i = 0; i < f.size(); ++i) {
        int bin = int(f.data()[i] * 256.0f);
        count[std::size_t(bin > 255 ? 255 : bin)] += 1.0;
    }
    std::vector<double> below(257, 0.0);
    for (int k = 0; k < 256; ++k) below[std::size_t(k) + 1] = below[std::size_t(k)] + count[std::size_t(k)];
    for (std::size_t i = 0; i < f.size(); ++i) {
        const double x = double(f.data()[i]) * 256.0;
        int k = int(x);
        if (k > 255) k = 255;
        const double want = (below[std::size_t(k)] + (x - k) * count[std::size_t(k)]) / 300.0;
        ASSERT_TRUE(std::fabs(double(out.data()[i]) - want) < 2e-5);
    }
}

UTEST(libstb_tone_tests, test_clahe_floats_clamp_and_pass_nan_through) {
    mat32f f = patterned_f(12, 9, 3, 13u);
    f.data()[7] = std::numeric_limits<float>::quiet_NaN();
    f.data()[40] = 1.7f;
    f.data()[41] = -0.4f;
    const mat32f out = stb::clahe(f, 2.0f, 2, 2, tone_mode::per_channel);
    ASSERT_TRUE(std::isnan(out.data()[7]));
    for (std::size_t i = 0; i < out.size(); ++i)
        if (i != 7) ASSERT_TRUE(out.data()[i] >= 0.0f && out.data()[i] <= 1.0f);
    const mat32f luma = stb::clahe(f, 2.0f, 2, 2);  // a NaN in one channel leaves the whole pixel alone
    ASSERT_TRUE(std::isnan(luma.data()[7]));
    ASSERT_TRUE(luma.data()[6] == f.data()[6] && luma.data()[8] == f.data()[8]);
}

// ------------------------------------------------------------------------------------------------- threads

UTEST(libstb_tone_tests, test_the_result_does_not_depend_on_the_number_of_threads) {
    const image8u src = patterned8(900, 700, stb::pixel_format::rgb, 31u);
    const mat32f srcf = patterned_f(500, 400, 3, 32u);
    image8u one(src.copy()), many(src.copy());
    mat32f one_f(srcf.copy()), many_f(srcf.copy());
    {
        stb::thread_scope scope(1);
        stb::equalize_inplace(one);
        stb::gamma_inplace(one, 1.5f);
        stb::equalize_inplace(one_f);
        stb::levels_inplace(one_f, 0.1f, 0.9f, 1.4f);
        stb::clahe_inplace(one, 3.0f, 6, 5);
        stb::clahe_inplace(one_f, 3.0f, 6, 5);
    }
    {
        stb::thread_scope scope(4);
        stb::equalize_inplace(many);
        stb::gamma_inplace(many, 1.5f);
        stb::equalize_inplace(many_f);
        stb::levels_inplace(many_f, 0.1f, 0.9f, 1.4f);
        stb::clahe_inplace(many, 3.0f, 6, 5);
        stb::clahe_inplace(many_f, 3.0f, 6, 5);
    }
    ASSERT_TRUE(same_bytes(one, many));
    ASSERT_TRUE(same_bits(one_f, many_f));
}
