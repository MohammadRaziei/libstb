#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

#include "stb.h"
#include "conftest.hpp"

// Canny. Pictures with a known edge: a vertical step, a step that is strong in the upper half and weak in the lower.

namespace {

using stb::image8u;
using stb::mat32f;
using stb::mat8u;

// a w x h picture whose columns from `edge` on are `high` (the rest `low`), as floats
mat32f step_picture(int w, int h, int edge, float low, float high) {
    mat32f m(w, h, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) m(x, y, 0) = x < edge ? low : high;
    return m;
}

bool same_bytes(const mat8u& a, const mat8u& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

int edges_in_row(const mat8u& m, int y) {
    int n = 0;
    for (int x = 0; x < m.width(); ++x) n += m(x, y, 0) == 255;
    return n;
}

int first_edge_in_row(const mat8u& m, int y) {
    for (int x = 0; x < m.width(); ++x)
        if (m(x, y, 0) == 255) return x;
    return -1;
}

int count_255(const mat8u& m) {
    int n = 0;
    for (std::uint8_t v : m) n += v == 255;
    return n;
}

}  // namespace

UTEST(libstb_canny_tests, test_a_step_gives_one_value_wide_line_at_the_step) {
    const mat8u edges = stb::canny(step_picture(40, 40, 20, 0.0f, 1.0f));
    EXPECT_EQ(edges.width(), 40);
    EXPECT_EQ(edges.height(), 40);
    EXPECT_EQ(edges.channels(), 1);
    const int x = first_edge_in_row(edges, 20);
    EXPECT_TRUE(x == 19 || x == 20);
    for (int y = 0; y < 40; ++y) {
        ASSERT_EQ(edges_in_row(edges, y), 1);
        ASSERT_EQ(first_edge_in_row(edges, y), x);
    }
}

UTEST(libstb_canny_tests, test_a_flat_picture_and_a_gentle_slope_have_no_edges) {
    EXPECT_EQ(count_255(stb::canny(mat32f(30, 20, 1, std::vector<float>(600, 0.4f)))), 0);
    mat32f ramp(30, 20, 1);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 30; ++x) ramp(x, y, 0) = 0.01f * float(x);  // a rise of 0.01 per value, far under the limits
    EXPECT_EQ(count_255(stb::canny(ramp)), 0);
}

UTEST(libstb_canny_tests, test_a_weak_edge_needs_a_strong_one_to_be_connected_to) {
    // one vertical line whose step is 1 down to row 19 (strong: its magnitude is about 0.62 after the blur of sigma 1), falls
    // to 0.25 by row 30 and stays there (about 0.16: between low = 0.1 and high = 0.2)
    mat32f both(40, 40, 1);
    for (int y = 0; y < 40; ++y) {
        const float level = y < 20 ? 1.0f : (y >= 30 ? 0.25f : 1.0f - 0.075f * float(y - 20));
        for (int x = 0; x < 40; ++x) both(x, y, 0) = x < 20 ? 0.0f : level;
    }
    const mat8u with_strong = stb::canny(both);
    for (int y = 32; y < 38; ++y) ASSERT_EQ(edges_in_row(with_strong, y), 1);
    // the weak part alone never reaches `high`: nothing at all
    EXPECT_EQ(count_255(stb::canny(step_picture(40, 40, 20, 0.0f, 0.25f))), 0);
    // lowering `high` to 0.1 lets it stand alone
    const mat8u alone = stb::canny(step_picture(40, 40, 20, 0.0f, 0.25f), 0.05f, 0.1f);
    for (int y = 5; y < 35; ++y) ASSERT_EQ(edges_in_row(alone, y), 1);
}

UTEST(libstb_canny_tests, test_8_bit_values_are_read_as_value_over_255_and_colour_as_luma) {
    image8u gray(40, 40, stb::pixel_format::gray);
    image8u rgb(40, 40, stb::pixel_format::rgb);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) {
            const std::uint8_t v = x < 20 ? 0 : 255;
            gray(x, y, 0) = v;
            for (int k = 0; k < 3; ++k) rgb(x, y, k) = v;
        }
    EXPECT_TRUE(same_bytes(stb::canny(gray), stb::canny(stb::cast<float>(gray))));
    EXPECT_TRUE(same_bytes(stb::canny(gray), stb::canny(rgb)));
    EXPECT_TRUE(same_bytes(stb::canny(gray), stb::canny(stb::cast<float>(rgb))));
    // an RGBA picture is read through its colour: the alpha channel does not count
    image8u rgba(40, 40, stb::pixel_format::rgba);
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x) {
            for (int k = 0; k < 3; ++k) rgba(x, y, k) = x < 20 ? 0 : 255;
            rgba(x, y, 3) = std::uint8_t((x * 7 + y * 3) % 256);
        }
    EXPECT_TRUE(same_bytes(stb::canny(gray), stb::canny(rgba)));
}

UTEST(libstb_canny_tests, test_sigma_0_skips_the_blur_and_bad_arguments_are_refused) {
    const mat32f step = step_picture(30, 30, 15, 0.0f, 1.0f);
    const mat8u sharp = stb::canny(step, 0.1f, 0.2f, 0.0f);
    for (int y = 0; y < 30; ++y) ASSERT_EQ(edges_in_row(sharp, y), 1);
    ASSERT_THROWS(stb::canny(step, 0.3f, 0.2f), std::invalid_argument);
    ASSERT_THROWS(stb::canny(step, -0.1f, 0.2f), std::invalid_argument);
    ASSERT_THROWS(stb::canny(step, 0.1f, 0.2f, -1.0f), std::invalid_argument);
    ASSERT_THROWS(stb::canny(step, 0.1f, 0.2f, std::numeric_limits<float>::infinity()), std::invalid_argument);
    ASSERT_THROWS(stb::canny(step, std::numeric_limits<float>::quiet_NaN(), 0.2f), std::invalid_argument);
    ASSERT_THROWS(stb::canny(mat32f(8, 8, 2)), std::invalid_argument);
    ASSERT_THROWS(stb::canny(mat8u(8, 8, 5)), std::invalid_argument);
    EXPECT_TRUE(stb::canny(image8u()).empty());
}

UTEST(libstb_canny_tests, test_a_diagonal_edge_is_thin_and_the_result_does_not_depend_on_the_number_of_threads) {
    mat32f diagonal(60, 60, 1);
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 60; ++x) diagonal(x, y, 0) = x > y ? 1.0f : 0.0f;
    const mat8u edges = stb::canny(diagonal);
    for (int y = 5; y < 55; ++y) {
        const int n = edges_in_row(edges, y);
        ASSERT_GE(n, 1);
        ASSERT_LE(n, 2);  // a diagonal line crosses each row once or twice at most
    }
    mat32f noisy(300, 200, 1);
    std::uint32_t s = 3u;
    for (float& v : noisy) {
        s = s * 1664525u + 1013904223u;
        v = float((s >> 8) & 0xFFFF) / 65535.0f;
    }
    mat8u one;
    {
        stb::thread_scope scope(1);
        one = stb::canny(noisy);
    }
    stb::thread_scope scope(4);
    EXPECT_TRUE(same_bytes(stb::canny(noisy), one));
}
