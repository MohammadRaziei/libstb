#include "test_common.hpp"

UTEST(libstb_utf8_tests, test_decodes_every_sequence_length) {
    const std::u32string cps = libstb::utf8_decode("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");  // A é € 😀
    ASSERT_EQ(4u, cps.size());
    ASSERT_EQ(U'A', cps[0]);
    ASSERT_EQ(0xE9u, cps[1]);
    ASSERT_EQ(0x20ACu, cps[2]);
    ASSERT_EQ(0x1F600u, cps[3]);
}

UTEST(libstb_utf8_tests, test_empty_is_empty) { ASSERT_TRUE(libstb::utf8_decode("").empty()); }

UTEST(libstb_utf8_tests, test_rejects_malformed_input) {
    for (const char* bad : {"\x80",              // stray continuation byte
                            "\xFF",              // invalid lead byte
                            "\xE2\x82",          // truncated
                            "\xC3\x28",          // bad continuation byte
                            "\xC0\x80",          // overlong NUL
                            "\xE0\x80\xAF",      // overlong '/'
                            "\xED\xA0\x80",      // surrogate U+D800
                            "\xF4\x90\x80\x80"}) // > U+10FFFF
        ASSERT_THROWS(libstb::utf8_decode(bad), std::invalid_argument);
}
