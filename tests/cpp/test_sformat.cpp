#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "test_common.hpp"

// stb::sformat / vsformat / sformat_to: printf for std::string. The expected text comes from
// snprintf itself wherever a result is checked against it.

// utest cannot print a std::string, so strings are compared with ==.
#define ASSERT_SAME(a, b) ASSERT_TRUE(std::string(a) == std::string(b))

namespace {

STB_FORMAT_CHECK(1, 2)
std::string through_a_wrapper(const char* fmt, ...) {  // what a user's own variadic function does with vsformat
    std::va_list args;
    va_start(args, fmt);
    std::string out = stb::vsformat(fmt, args);
    va_end(args);
    return out;
}

// The same without the compiler's format check, for the cases it would (rightly) complain about.
std::string unchecked(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    std::string out = stb::vsformat(fmt, args);
    va_end(args);
    return out;
}

}  // namespace

UTEST(libstb_sformat_tests, test_the_common_conversions_match_snprintf) {
    char want[128];
    std::snprintf(want, sizeof want, "%d|%5d|%-5d|%05d|%x|%X|%o|%u|%c|%%|%s|%10s|%-10s|%.2s", -42, 42, 42, 42, 255, 255, 8, 4000000000u, 'z', "text", "right", "left", "cut");
    ASSERT_STREQ(want, stb::sformat("%d|%5d|%-5d|%05d|%x|%X|%o|%u|%c|%%|%s|%10s|%-10s|%.2s", -42, 42, 42, 42, 255, 255, 8, 4000000000u, 'z', "text", "right", "left", "cut").c_str());
    std::snprintf(want, sizeof want, "%f|%.3f|%8.2f|%e|%g", 3.14159, 3.14159, 2.5, 12345.678, 0.0001);
    ASSERT_STREQ(want, stb::sformat("%f|%.3f|%8.2f|%e|%g", 3.14159, 3.14159, 2.5, 12345.678, 0.0001).c_str());
}

UTEST(libstb_sformat_tests, test_sized_integers) {
    char want[32];
    std::snprintf(want, sizeof want, "%zu", SIZE_MAX);
    ASSERT_SAME(std::string(want), stb::sformat("%zu", SIZE_MAX));
    ASSERT_SAME(std::string("-9223372036854775808"), stb::sformat("%lld", LLONG_MIN));
    ASSERT_SAME(std::string("18446744073709551615"), stb::sformat("%llu", ULLONG_MAX));
    ASSERT_SAME(std::string("-2147483648"), stb::sformat("%d", INT_MIN));
    ASSERT_SAME(std::string("65535"), stb::sformat("%hu", std::uint16_t(65535)));
}

UTEST(libstb_sformat_tests, test_empty_and_literal_formats) {
    ASSERT_SAME(std::string(), unchecked(""));
    ASSERT_SAME(std::string("no conversions"), stb::sformat("no conversions"));
    ASSERT_SAME(std::string("100%"), stb::sformat("100%%"));
}

UTEST(libstb_sformat_tests, test_a_string_view_goes_through_dot_star) {
    const std::string_view name = "rgba+more";
    ASSERT_SAME(std::string("format=rgba"), stb::sformat("format=%.*s", 4, name.data()));
    ASSERT_SAME(std::string("[rgba+more]"), stb::sformat("[%.*s]", int(name.size()), name.data()));
}

UTEST(libstb_sformat_tests, test_every_length_around_the_stack_buffer_and_far_beyond_it) {
    // Results of exactly n characters: n = 255 fits the 256 byte buffer with its terminator, 256 does not.
    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(254), std::size_t(255), std::size_t(256),
                          std::size_t(257), std::size_t(511), std::size_t(512), std::size_t(4096), std::size_t(1) << 20}) {
        const std::string body(n, 'x');
        const std::string got = stb::sformat("%s", body.c_str());
        ASSERT_EQ(n, got.size());
        ASSERT_TRUE(got == body);
        const std::string framed = stb::sformat("<%s>%d", body.c_str(), 7);  // text before and after the long part
        ASSERT_SAME("<" + body + ">7", framed);
    }
}

UTEST(libstb_sformat_tests, test_a_nul_character_stays_in_the_string) {
    const std::string got = stb::sformat("a%cb", 0);
    ASSERT_EQ(std::size_t(3), got.size());
    ASSERT_EQ('a', got[0]);
    ASSERT_EQ('\0', got[1]);
    ASSERT_EQ('b', got[2]);
    const std::string long_one = stb::sformat("%s%c%s", std::string(300, 'p').c_str(), 0, std::string(300, 'q').c_str());
    ASSERT_EQ(std::size_t(601), long_one.size());
    ASSERT_EQ('\0', long_one[300]);
}

UTEST(libstb_sformat_tests, test_sformat_to_appends_and_keeps_what_is_there) {
    std::string out = "image: ";
    stb::sformat_to(out, "%dx%d", 640, 480);
    stb::sformat_to(out, ", %s", "rgba");
    ASSERT_SAME(std::string("image: 640x480, rgba"), out);

    stb::sformat_to(out, "|%s|", std::string(1000, 'z').c_str());  // a long part appended to a non-empty string
    ASSERT_EQ(std::size_t(20 + 1002), out.size());
    ASSERT_SAME("image: 640x480, rgba", out.substr(0, 20));
    ASSERT_EQ('|', out.back());

    std::string empty;
    stb::sformat_to(empty, "%s", "");
    ASSERT_TRUE(empty.empty());
}

UTEST(libstb_sformat_tests, test_vsformat_serves_a_functions_of_your_own) {
    ASSERT_SAME(std::string("7 and seven"), through_a_wrapper("%d and %s", 7, "seven"));
    ASSERT_SAME(std::string(300, 'k') + "!", through_a_wrapper("%s!", std::string(300, 'k').c_str()));
    // the va_list is used twice inside (measure, then write): the same text each time
    for (int i = 0; i < 3; ++i) ASSERT_SAME(std::string(500, 'k') + "1", through_a_wrapper("%s%d", std::string(500, 'k').c_str(), 1));
}

UTEST(libstb_sformat_tests, test_many_calls_in_a_row_give_independent_results) {
    for (int i = 0; i < 2000; ++i) {
        const std::size_t tail = std::size_t(i) % 700;  // (no % inside an assertion: utest prints the expression as a format)
        const std::string got = stb::sformat("%d:%s", i, std::string(tail, 'a').c_str());
        char head[16];
        const int h = std::snprintf(head, sizeof head, "%d:", i);
        ASSERT_EQ(std::size_t(h) + tail, got.size());
        ASSERT_TRUE(got.compare(0, std::size_t(h), head) == 0);
    }
}
