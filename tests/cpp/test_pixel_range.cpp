#include <cstdint>
#include <limits>

#include "stb/pixel_range.hpp"
#include "test_common.hpp"

// The range of a pixel type: "white" is the maximum of an integer type and 1 for a float, and a value computed in floating point is
// stored rounded to the nearest and clamped. The same code serves 8, 16 and 32 bits, signed or not.

// UTEST() declares `extern utest_state` itself: inside an anonymous namespace that would declare
// a different, internal-linkage utest_state and the link fails, so the tests stay at global scope.
using stb::pixel_range;

UTEST(libstb_pixel_range_tests, test_white_is_the_maximum_of_the_type_and_one_for_a_float) {
    EXPECT_EQ(int(pixel_range<std::uint8_t>::max), 255);
    EXPECT_EQ(int(pixel_range<std::uint16_t>::max), 65535);
    EXPECT_EQ(pixel_range<float>::max, 1.0f);
    EXPECT_TRUE(pixel_range<std::uint8_t>::is_integer);
    EXPECT_TRUE(pixel_range<std::uint16_t>::is_integer);
    EXPECT_FALSE(pixel_range<float>::is_integer);
}

UTEST(libstb_pixel_range_tests, test_an_integer_is_rounded_to_the_nearest_and_clamped) {
    EXPECT_EQ(int(pixel_range<std::uint8_t>::store(0.4f)), 0);
    EXPECT_EQ(int(pixel_range<std::uint8_t>::store(0.5f)), 1);
    EXPECT_EQ(int(pixel_range<std::uint8_t>::store(127.49f)), 127);
    EXPECT_EQ(int(pixel_range<std::uint8_t>::store(254.6f)), 255);
    EXPECT_EQ(int(pixel_range<std::uint8_t>::store(255.0f)), 255);
    EXPECT_EQ(int(pixel_range<std::uint8_t>::store(300.0f)), 255);
    EXPECT_EQ(int(pixel_range<std::uint8_t>::store(-3.0f)), 0);
    EXPECT_EQ(int(pixel_range<std::uint16_t>::store(1000.4f)), 1000);
    EXPECT_EQ(int(pixel_range<std::uint16_t>::store(1000.6f)), 1001);
    EXPECT_EQ(int(pixel_range<std::uint16_t>::store(65534.6f)), 65535);
    EXPECT_EQ(int(pixel_range<std::uint16_t>::store(70000.0f)), 65535);
    EXPECT_EQ(int(pixel_range<std::uint16_t>::store(-1.0f)), 0);
}

UTEST(libstb_pixel_range_tests, test_a_signed_integer_is_rounded_away_from_zero_and_clamped_at_both_ends) {
    EXPECT_EQ(32767, int(pixel_range<std::int16_t>::max));
    EXPECT_EQ(-32768, int(pixel_range<std::int16_t>::lowest));
    EXPECT_EQ(int(pixel_range<std::int16_t>::store(1.5f)), 2);
    EXPECT_EQ(int(pixel_range<std::int16_t>::store(-1.5f)), -2);
    EXPECT_EQ(int(pixel_range<std::int16_t>::store(-0.4f)), 0);
    EXPECT_EQ(int(pixel_range<std::int16_t>::store(40000.0f)), 32767);
    EXPECT_EQ(int(pixel_range<std::int16_t>::store(-40000.0f)), -32768);
    EXPECT_EQ(int(pixel_range<std::int16_t>::store(std::numeric_limits<float>::quiet_NaN())), 0);
    EXPECT_EQ(pixel_range<std::int32_t>::store(3.0e9), 2147483647);
    EXPECT_EQ(pixel_range<std::uint32_t>::store(5.0e9), 4294967295u);
    EXPECT_EQ(pixel_range<std::uint32_t>::store(-1.0), 0u);
}

UTEST(libstb_pixel_range_tests, test_a_float_is_not_changed) {
    EXPECT_EQ(pixel_range<float>::store(0.25f), 0.25f);
    EXPECT_EQ(pixel_range<float>::store(-2.0f), -2.0f);  // not clamped: a float may hold any value
    EXPECT_EQ(pixel_range<float>::store(3.5f), 3.5f);
}
