#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "stb.h"
#include "test_common.hpp"

// Thresholds: threshold, otsu_threshold and adaptive_threshold. The small cases are worked out by hand.

namespace {

using stb::image8u;
using stb::mat32f;
using stb::mat8u;

image8u gray_row(const std::vector<std::uint8_t>& v) { return image8u(int(v.size()), 1, stb::pixel_format::gray, v); }

image8u patterned8(int w, int h, stb::pixel_format f, std::uint32_t seed) {
    image8u m(w, h, f);
    std::uint32_t s = seed;
    for (std::uint8_t& v : m) {
        s = s * 1664525u + 1013904223u;
        v = std::uint8_t(s >> 24);
    }
    return m;
}

bool same_bytes(const mat8u& a, const mat8u& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

std::vector<int> row_of(const mat8u& m) {
    std::vector<int> v;
    for (int x = 0; x < m.width(); ++x) v.push_back(int(m(x, 0, 0)));
    return v;
}

// otsu by the definition, in double (one bin per level between the smallest and the largest)
int reference_otsu(const mat8u& m) {
    double hist[256] = {0};
    for (std::uint8_t v : m) hist[v] += 1.0;
    int lo = 255, hi = 0;
    for (int i = 0; i < 256; ++i)
        if (hist[i] > 0) {
            lo = std::min(lo, i);
            hi = i;
        }
    if (lo == hi) return lo;
    int best = lo;
    double best_var = -1.0;
    for (int t = lo; t < hi; ++t) {
        double w1 = 0, s1 = 0, w2 = 0, s2 = 0;
        for (int i = lo; i <= hi; ++i) (i <= t ? w1 : w2) += hist[i], (i <= t ? s1 : s2) += hist[i] * i;
        if (w1 == 0 || w2 == 0) continue;
        const double var = w1 * w2 * (s1 / w1 - s2 / w2) * (s1 / w1 - s2 / w2);
        if (var > best_var) {
            best_var = var;
            best = t;
        }
    }
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------- threshold

UTEST(libstb_threshold_tests, test_threshold_is_255_above_the_level_and_0_at_or_below_it) {
    const image8u a = gray_row({0, 100, 128, 129, 255});
    const mat8u m = stb::threshold(a, 128);
    EXPECT_TRUE((row_of(m) == std::vector<int>{0, 0, 0, 255, 255}));
    EXPECT_TRUE((row_of(stb::threshold(a, 128, true)) == std::vector<int>{255, 255, 255, 0, 0}));
    const mat32f f(4, 1, 1, std::vector<float>{0.0f, 0.5f, 0.51f, 1.0f});
    EXPECT_TRUE((row_of(stb::threshold(f, 0.5f)) == std::vector<int>{0, 0, 255, 255}));
}

UTEST(libstb_threshold_tests, test_a_nan_is_never_above_the_level_so_the_inverted_mask_has_it_at_255) {
    const mat32f f(2, 1, 1, std::vector<float>{std::numeric_limits<float>::quiet_NaN(), 0.9f});
    EXPECT_TRUE((row_of(stb::threshold(f, 0.5f)) == std::vector<int>{0, 255}));
    EXPECT_TRUE((row_of(stb::threshold(f, 0.5f, true)) == std::vector<int>{255, 0}));
}

UTEST(libstb_threshold_tests, test_threshold_takes_one_channel_and_an_empty_picture_gives_an_empty_mask) {
    ASSERT_THROWS(stb::threshold(patterned8(4, 4, stb::pixel_format::rgb, 1u), 10), std::invalid_argument);
    ASSERT_THROWS(stb::threshold(mat32f(4, 4, 2), 0.5f), std::invalid_argument);
    EXPECT_TRUE(stb::threshold(image8u(), 10).empty());
    const mat8u m = stb::threshold(patterned8(5, 3, stb::pixel_format::gray, 2u), 100);
    EXPECT_EQ(m.channels(), 1);
    EXPECT_EQ(m.width(), 5);
    EXPECT_EQ(m.height(), 3);
}

// ------------------------------------------------------------------------------------------------------- otsu

UTEST(libstb_threshold_tests, test_otsu_splits_two_clusters_at_the_first_best_level) {
    std::vector<std::uint8_t> v(20, 50);
    for (int i = 10; i < 20; ++i) v[std::size_t(i)] = 200;
    // every level from 50 to 199 splits the clusters equally well: the first of them wins, as in scikit-image
    EXPECT_EQ(int(stb::otsu_threshold(gray_row(v))), 50);
}

UTEST(libstb_threshold_tests, test_otsu_of_a_flat_picture_is_its_value_and_matches_the_definition_on_noise) {
    EXPECT_EQ(int(stb::otsu_threshold(gray_row({7, 7, 7}))), 7);
    EXPECT_EQ(int(stb::otsu_threshold(gray_row({255}))), 255);
    for (std::uint32_t seed : {3u, 4u, 5u}) {
        const image8u a = patterned8(40, 30, stb::pixel_format::gray, seed);
        EXPECT_EQ(int(stb::otsu_threshold(a)), reference_otsu(a));
    }
}

UTEST(libstb_threshold_tests, test_float_otsu_lies_between_two_clusters_and_ignores_what_is_not_finite) {
    std::vector<float> v;
    for (int i = 0; i < 50; ++i) v.push_back(0.2f + 0.001f * float(i % 5));
    for (int i = 0; i < 50; ++i) v.push_back(0.8f + 0.001f * float(i % 5));
    v.push_back(std::numeric_limits<float>::infinity());
    v.push_back(std::numeric_limits<float>::quiet_NaN());
    const float t = stb::otsu_threshold(mat32f(int(v.size()), 1, 1, v));
    EXPECT_GT(t, 0.2f);
    EXPECT_LT(t, 0.8f);
    EXPECT_EQ(stb::otsu_threshold(mat32f(3, 1, 1, std::vector<float>{0.25f, 0.25f, 0.25f})), 0.25f);
}

UTEST(libstb_threshold_tests, test_otsu_refuses_an_empty_picture_and_one_without_a_finite_value) {
    ASSERT_THROWS(stb::otsu_threshold(image8u()), std::invalid_argument);
    ASSERT_THROWS(stb::otsu_threshold(mat32f(2, 1, 1, std::vector<float>{std::numeric_limits<float>::quiet_NaN(),
                                                                         std::numeric_limits<float>::infinity()})),
                  std::invalid_argument);
    ASSERT_THROWS(stb::otsu_threshold(patterned8(4, 4, stb::pixel_format::rgb, 1u)), std::invalid_argument);
}

// ---------------------------------------------------------------------------------------------- adaptive

namespace {

// a bright background that grows from 100 to 200 from left to right, with a dark vertical line in the middle column
image8u uneven_page() {
    image8u m(41, 40, stb::pixel_format::gray);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 41; ++x) m(x, y, 0) = std::uint8_t(100 + (100 * x) / 40 - (x == 20 ? 60 : 0));
    return m;
}

}  // namespace

UTEST(libstb_threshold_tests, test_adaptive_threshold_finds_a_dark_line_on_an_uneven_background) {
    const image8u page = uneven_page();
    for (stb::adaptive_method method : {stb::adaptive_method::mean, stb::adaptive_method::gaussian}) {
        const mat8u mask = stb::adaptive_threshold(page, 5, 10.0f, method, true);
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 41; ++x) ASSERT_EQ(int(mask(x, y, 0)), x == 20 ? 255 : 0);
        // a plain threshold cannot do this: any level cuts the page itself
        const mat8u plain = stb::threshold(page, 150, true);
        EXPECT_NE(int(plain(2, 5, 0)), int(plain(38, 5, 0)));
    }
}

UTEST(libstb_threshold_tests, test_a_flat_picture_is_background_for_offset_0_and_inversion_swaps_the_mask) {
    const image8u flat(30, 20, stb::pixel_format::gray, std::vector<std::uint8_t>(600, 123));
    const mat32f flat_f(30, 20, 1, std::vector<float>(600, 0.37f));
    for (stb::adaptive_method method : {stb::adaptive_method::mean, stb::adaptive_method::gaussian}) {
        for (std::uint8_t v : stb::adaptive_threshold(flat, 4, 0.0f, method)) ASSERT_EQ(int(v), 0);
        for (std::uint8_t v : stb::adaptive_threshold(flat_f, 4, 0.0f, method)) ASSERT_EQ(int(v), 0);
        for (std::uint8_t v : stb::adaptive_threshold(flat, 4, 0.0f, method, true)) ASSERT_EQ(int(v), 255);
        // a positive offset lowers the level: the flat picture is then above it
        for (std::uint8_t v : stb::adaptive_threshold(flat, 4, 5.0f, method)) ASSERT_EQ(int(v), 255);
    }
}

UTEST(libstb_threshold_tests, test_adaptive_threshold_reads_floats_like_8_bits_and_refuses_bad_arguments) {
    const image8u a = patterned8(25, 20, stb::pixel_format::gray, 9u);
    // the same picture and the offset in values instead of levels
    EXPECT_TRUE(same_bytes(stb::adaptive_threshold(a, 3, 8.0f), stb::adaptive_threshold(stb::cast<float>(a), 3, 8.0f / 255.0f)));
    ASSERT_THROWS(stb::adaptive_threshold(a, 0), std::invalid_argument);
    ASSERT_THROWS(stb::adaptive_threshold(a, 1001), std::invalid_argument);
    ASSERT_THROWS(stb::adaptive_threshold(a, 3, std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
    ASSERT_THROWS(stb::adaptive_threshold(patterned8(8, 8, stb::pixel_format::rgb, 1u), 3), std::invalid_argument);
    EXPECT_TRUE(stb::adaptive_threshold(image8u(), 3).empty());
}

UTEST(libstb_threshold_tests, test_the_result_does_not_depend_on_the_number_of_threads) {
    const image8u a = patterned8(600, 400, stb::pixel_format::gray, 11u);
    mat8u t1, a1;
    {
        stb::thread_scope scope(1);
        t1 = stb::threshold(a, 100);
        a1 = stb::adaptive_threshold(a, 7, 3.0f);
    }
    stb::thread_scope scope(4);
    EXPECT_TRUE(same_bytes(stb::threshold(a, 100), t1));
    EXPECT_TRUE(same_bytes(stb::adaptive_threshold(a, 7, 3.0f), a1));
}
