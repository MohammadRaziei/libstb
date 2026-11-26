#include <cstdint>
#include <limits>

#include "image/pixel_range.hpp"
#include "test_common.hpp"

// The range of a pixel type (an internal header): "white" is the maximum of an unsigned integer type and 1 for a float, and a value
// computed in floats is stored rounded to the nearest and clamped. The same code serves 8 and 16 bits.

namespace {

using stb::detail_pixel::range;

UTEST(libstb_pixel_range_tests, test_white_is_the_maximum_of_the_type_and_one_for_a_float) {
    EXPECT_EQ(int(range<std::uint8_t>::max), 255);
    EXPECT_EQ(int(range<std::uint16_t>::max), 65535);
    EXPECT_EQ(range<float>::max, 1.0f);
    EXPECT_TRUE(range<std::uint8_t>::is_integer);
    EXPECT_TRUE(range<std::uint16_t>::is_integer);
    EXPECT_FALSE(range<float>::is_integer);
}

UTEST(libstb_pixel_range_tests, test_an_integer_is_rounded_to_the_nearest_and_clamped) {
    EXPECT_EQ(int(range<std::uint8_t>::store(0.4f)), 0);
    EXPECT_EQ(int(range<std::uint8_t>::store(0.5f)), 1);
    EXPECT_EQ(int(range<std::uint8_t>::store(127.49f)), 127);
    EXPECT_EQ(int(range<std::uint8_t>::store(254.6f)), 255);
    EXPECT_EQ(int(range<std::uint8_t>::store(255.0f)), 255);
    EXPECT_EQ(int(range<std::uint8_t>::store(300.0f)), 255);
    EXPECT_EQ(int(range<std::uint8_t>::store(-3.0f)), 0);
    EXPECT_EQ(int(range<std::uint16_t>::store(1000.4f)), 1000);
    EXPECT_EQ(int(range<std::uint16_t>::store(1000.6f)), 1001);
    EXPECT_EQ(int(range<std::uint16_t>::store(65534.6f)), 65535);
    EXPECT_EQ(int(range<std::uint16_t>::store(70000.0f)), 65535);
    EXPECT_EQ(int(range<std::uint16_t>::store(-1.0f)), 0);
}

UTEST(libstb_pixel_range_tests, test_a_float_is_not_changed) {
    EXPECT_EQ(range<float>::store(0.25f), 0.25f);
    EXPECT_EQ(range<float>::store(-2.0f), -2.0f);  // not clamped: a float may hold any value
    EXPECT_EQ(range<float>::store(3.5f), 3.5f);
}

}  // namespace
