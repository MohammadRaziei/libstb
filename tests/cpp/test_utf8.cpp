#include "conftest.hpp"

UTEST(libstb_utf8_tests, test_decodes_every_sequence_length) {
    const std::u32string cps = stb::utf8_decode("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");  // A é € 😀
    ASSERT_EQ(4u, cps.size());
    ASSERT_EQ(U'A', cps[0]);
    ASSERT_EQ(0xE9u, cps[1]);
    ASSERT_EQ(0x20ACu, cps[2]);
    ASSERT_EQ(0x1F600u, cps[3]);
}

UTEST(libstb_utf8_tests, test_empty_is_empty) { ASSERT_TRUE(stb::utf8_decode("").empty()); }

UTEST(libstb_utf8_tests, test_rejects_malformed_input) {
    for (const char* bad : {"\x80",              // stray continuation byte
                            "\xFF",              // invalid lead byte
                            "\xE2\x82",          // truncated
                            "\xC3\x28",          // bad continuation byte
                            "\xC0\x80",          // overlong NUL
                            "\xE0\x80\xAF",      // overlong '/'
                            "\xED\xA0\x80",      // surrogate U+D800
                            "\xF4\x90\x80\x80"}) // > U+10FFFF
        ASSERT_THROWS(stb::utf8_decode(bad), std::invalid_argument);
}

UTEST(libstb_utf8_tests, test_lossy_decode_matches_strict_on_good_text) {
    const std::string good = "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    ASSERT_TRUE(stb::utf8_decode_lossy(good) == stb::utf8_decode(good));
    ASSERT_TRUE(stb::utf8_decode_lossy("").empty());
}

UTEST(libstb_utf8_tests, test_lossy_decode_replaces_each_bad_byte) {
    const std::u32string r = U"\uFFFD";
    ASSERT_TRUE(stb::utf8_decode_lossy("\x80") == r);                                // stray continuation byte
    ASSERT_TRUE(stb::utf8_decode_lossy("\xFF") == r);                                // invalid lead byte
    ASSERT_TRUE(stb::utf8_decode_lossy("\xE2\x82") == std::u32string(U"\uFFFD\uFFFD"));  // truncated: one per byte
    ASSERT_TRUE(stb::utf8_decode_lossy("\xC3\x28") == std::u32string(U"\uFFFD(")); // bad continuation, '(' survives
    ASSERT_TRUE(stb::utf8_decode_lossy("\xC0\x80") == std::u32string(U"\uFFFD\uFFFD"));  // overlong NUL
    ASSERT_TRUE(stb::utf8_decode_lossy("\xED\xA0\x80") == std::u32string(U"\uFFFD\uFFFD\uFFFD"));  // surrogate
    ASSERT_TRUE(stb::utf8_decode_lossy("a\xFF" "b") == std::u32string(U"a\uFFFD" "b"));  // text around it is kept
}

UTEST(libstb_utf8_tests, test_encode_round_trips_and_replaces_non_scalars) {
    const std::u32string cps = U"A\u00E9\u20AC\U0001F600";
    const std::string utf8 = stb::utf8_encode(cps);
    ASSERT_TRUE(utf8 == "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
    ASSERT_TRUE(stb::utf8_decode(utf8) == cps);
    ASSERT_TRUE(stb::utf8_encode(std::u32string(1, char32_t(0xD800))) == "\xEF\xBF\xBD");   // surrogate
    ASSERT_TRUE(stb::utf8_encode(std::u32string(1, char32_t(0x110000))) == "\xEF\xBF\xBD"); // above U+10FFFF
    ASSERT_TRUE(stb::utf8_encode(U"").empty());
}
