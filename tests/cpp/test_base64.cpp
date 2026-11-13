#include <cstdint>
#include <string>
#include <vector>

#include "test_common.hpp"

namespace {

std::string encode(const std::string& s) { return stb::base64_encode(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }

std::string decode(std::string_view text) {
    const std::vector<std::uint8_t> bytes = stb::base64_decode(text);
    return std::string(bytes.begin(), bytes.end());
}

bool rejects(std::string_view text) { return !stb::try_base64_decode(text).has_value(); }

}  // namespace

UTEST(libstb_base64_tests, test_the_rfc_4648_vectors_both_ways) {
    // RFC 4648 section 10
    const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* coded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; ++i) {
        ASSERT_STREQ(coded[i], encode(plain[i]).c_str());
        ASSERT_STREQ(plain[i], decode(coded[i]).c_str());
    }
}

UTEST(libstb_base64_tests, test_padding_is_optional_but_must_be_complete) {
    ASSERT_STREQ("f", decode("Zg").c_str());
    ASSERT_STREQ("fo", decode("Zm8").c_str());
    ASSERT_STREQ("foob", decode("Zm9vYg").c_str());
    ASSERT_TRUE(rejects("Zg="));     // one '=' where two are needed
    ASSERT_TRUE(rejects("Zm8=="));   // two where one is needed
    ASSERT_TRUE(rejects("Zm9v="));   // padding after a complete group
    ASSERT_TRUE(rejects("Zg==="));   // three
    ASSERT_TRUE(rejects("Zg==Zg=="));  // data after the padding
    ASSERT_TRUE(rejects("="));
}

UTEST(libstb_base64_tests, test_white_space_is_ignored_anywhere) {
    ASSERT_STREQ("foobar", decode(" Zm9v\r\nYmFy\t").c_str());
    ASSERT_STREQ("foobar", decode("Z m 9 v Y m F y").c_str());
    ASSERT_STREQ("fo", decode("Zm\n8 =").c_str());
    ASSERT_STREQ("f", decode("Zg = =").c_str());  // white space between the padding characters too
    ASSERT_TRUE(stb::base64_decode("").empty());
    ASSERT_TRUE(stb::base64_decode(" \n\t ").empty());
    ASSERT_TRUE(rejects("Zm9v\vYmFy"));  // a vertical tab is not white space here (Infra: tab, LF, FF, CR, space)
}

UTEST(libstb_base64_tests, test_malformed_text_is_rejected) {
    ASSERT_TRUE(rejects("Z"));        // one character is not a byte
    ASSERT_TRUE(rejects("Zm9vY"));    // 1 more than a multiple of 4
    ASSERT_TRUE(rejects("Zm9v*mFy"));
    ASSERT_TRUE(rejects("Zm9v-mFy")); // the URL-safe alphabet is another encoding
    ASSERT_TRUE(rejects("Zm9v_mFy"));
    ASSERT_TRUE(rejects(std::string_view("Zm9v\0mFy", 8)));
    ASSERT_TRUE(rejects("Zm9v\xC3\xA9mFy"));
    ASSERT_THROWS(stb::base64_decode("Z"), std::invalid_argument);
    ASSERT_THROWS(stb::base64_encode(nullptr, 3), std::invalid_argument);
    ASSERT_TRUE(stb::base64_encode(nullptr, 0).empty());
}

UTEST(libstb_base64_tests, test_every_length_and_byte_value_round_trips) {
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 256; ++i) bytes.push_back(std::uint8_t(i));
    for (std::size_t n = 0; n <= bytes.size(); ++n) {
        const std::string text = stb::base64_encode(bytes.data(), n);
        ASSERT_EQ((n + 2) / 3 * 4, text.size());
        ASSERT_TRUE(stb::base64_decode(text) == std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + std::ptrdiff_t(n)));
    }
    // the unused bits of the last character are ignored: "Zh==" is "f" like "Zg=="
    ASSERT_STREQ("f", decode("Zh==").c_str());
}

UTEST(libstb_base64_tests, test_a_large_buffer_round_trips) {
    std::vector<std::uint8_t> bytes(1 << 20);
    std::uint32_t state = 1;
    for (auto& b : bytes) {
        state = state * 1664525u + 1013904223u;
        b = std::uint8_t(state >> 24);
    }
    const std::string text = stb::base64_encode(bytes.data(), bytes.size());
    ASSERT_TRUE(stb::base64_decode(text) == bytes);
    // with a line break every 76 characters, as MIME writes it
    std::string wrapped;
    for (std::size_t i = 0; i < text.size(); i += 76) wrapped += text.substr(i, 76) + "\r\n";
    ASSERT_TRUE(stb::base64_decode(wrapped) == bytes);
}
