#include <numeric>

#include "test_common.hpp"

namespace {

std::vector<std::uint8_t> bytes_of(const std::string& s) { return {s.begin(), s.end()}; }

// Deterministic data with some structure: runs, repeats and noise.
std::vector<std::uint8_t> sample(std::size_t n, unsigned seed) {
    std::vector<std::uint8_t> v(n);
    std::uint32_t x = seed * 2654435761u + 1;
    for (std::size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        v[i] = (i / 64) % 3 == 0 ? std::uint8_t(x >> 24) : std::uint8_t("abcabcabd"[i % 9]);
    }
    return v;
}

}  // namespace

UTEST(libstb_zlib_tests, test_round_trip_at_every_level_and_size) {
    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(257), std::size_t(70000),
                          std::size_t(1) << 20}) {
        const auto data = sample(n, unsigned(n));
        for (int level = -1; level <= 9; ++level) {
            const auto packed = stb::zlib_compress(data, level);
            ASSERT_EQ(0x78, int(packed[0]));                 // zlib header, deflate, 32 KiB window
            ASSERT_TRUE(stb::zlib_decompress(packed) == data);
        }
    }
}

UTEST(libstb_zlib_tests, test_decompresses_a_stream_made_by_zlib_itself) {
    // zlib.compress(b"hello world") from python's zlib.
    const std::vector<std::uint8_t> z = {0x78, 0x9C, 0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x57, 0x28, 0xCF,
                                         0x2F, 0xCA, 0x49, 0x01, 0x00, 0x1A, 0x0B, 0x04, 0x5D};
    ASSERT_TRUE(stb::zlib_decompress(z) == bytes_of("hello world"));
}

UTEST(libstb_zlib_tests, test_the_empty_stream_is_valid_both_ways) {
    const auto packed = stb::zlib_compress(nullptr, 0);
    ASSERT_EQ(8u, packed.size());
    ASSERT_TRUE(stb::zlib_decompress(packed).empty());
}

UTEST(libstb_zlib_tests, test_a_higher_level_is_not_larger_on_text_like_data) {
    const auto data = sample(1 << 20, 7);
    ASSERT_TRUE(stb::zlib_compress(data, 9).size() <= stb::zlib_compress(data, 1).size());
    ASSERT_TRUE(stb::zlib_compress(data, 1).size() < data.size());
    ASSERT_TRUE(stb::zlib_compress(data, -1) == stb::zlib_compress(data, stb::default_zlib_level));
}

UTEST(libstb_zlib_tests, test_level_0_stores_without_compressing) {
    // 2 header bytes + 5 per block (up to 65535 bytes each) + 4 Adler-32; the data itself, verbatim.
    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(65535), std::size_t(65536), std::size_t(200000)}) {
        const std::vector<std::uint8_t> data(n, 0);               // zeros: any compressor would shrink them
        const auto stored = stb::zlib_compress(data, 0);
        const std::size_t blocks = n == 0 ? 1 : (n + 65534) / 65535;
        ASSERT_EQ(n + 2 + 5 * blocks + 4, stored.size());
        ASSERT_TRUE(stb::zlib_decompress(stored) == data);
    }
}

UTEST(libstb_zlib_tests, test_corrupt_input_throws_decode_error) {
    const auto good = stb::zlib_compress(sample(5000, 3));
    ASSERT_THROWS(stb::zlib_decompress(nullptr, 0), stb::decode_error);          // empty
    ASSERT_THROWS(stb::zlib_decompress(bytes_of("not zlib at all")), stb::decode_error);
    {
        auto bad = good;
        bad[0] = 0x79;                                                           // header check bits wrong
        ASSERT_THROWS(stb::zlib_decompress(bad), stb::decode_error);
    }
    {
        auto bad = good;
        bad.resize(bad.size() / 2);                                              // truncated
        ASSERT_THROWS(stb::zlib_decompress(bad), stb::decode_error);
    }
    {
        auto bad = good;
        bad.back() ^= 0x01;                                                      // Adler-32 off by one bit
        ASSERT_THROWS(stb::zlib_decompress(bad), stb::decode_error);
    }
    {
        auto bad = good;
        bad.push_back(0);                                                        // data after the stream
        ASSERT_THROWS(stb::zlib_decompress(bad), stb::decode_error);
    }
    {
        auto bad = good;
        bad[bad.size() / 2] ^= 0xFF;                                             // damage in the middle
        ASSERT_THROWS(stb::zlib_decompress(bad), stb::decode_error);
    }
}

UTEST(libstb_zlib_tests, test_every_error_is_a_stb_error) {
    try {
        stb::zlib_decompress(bytes_of("garbage garbage"));
        ASSERT_TRUE(false);
    } catch (const stb::error&) {
    }
}

UTEST(libstb_zlib_tests, test_max_bytes_bounds_the_output_exactly) {
    const std::vector<std::uint8_t> zeros(1 << 20, 0);                           // 1 MiB of zeros
    const auto packed = stb::zlib_compress(zeros);
    ASSERT_TRUE(packed.size() < 2048);                                           // a 500:1 or better ratio
    ASSERT_EQ(zeros.size(), stb::zlib_decompress(packed, zeros.size()).size());  // exactly at the limit: fine
    ASSERT_THROWS(stb::zlib_decompress(packed, zeros.size() - 1), stb::limit_error);
    ASSERT_THROWS(stb::zlib_decompress(packed, 4096), stb::limit_error);
    ASSERT_THROWS(stb::zlib_decompress(packed, 0), stb::limit_error);
    ASSERT_TRUE(stb::zlib_decompress(stb::zlib_compress(nullptr, 0), 0).empty());   // empty fits in 0 bytes
}

UTEST(libstb_zlib_tests, test_a_limit_error_is_not_a_decode_error) {
    const auto packed = stb::zlib_compress(std::vector<std::uint8_t>(1 << 16, 7));
    try {
        stb::zlib_decompress(packed, 100);
        ASSERT_TRUE(false);
    } catch (const stb::decode_error&) {
        ASSERT_TRUE(false);          // a bomb is not "corrupt data"
    } catch (const stb::limit_error&) {
    }
}

UTEST(libstb_zlib_tests, test_a_stream_that_grows_in_steps_still_round_trips) {
    // 5 MB of data whose ratio is far from the first guess (4x the input): several retries.
    const std::vector<std::uint8_t> zeros(5 << 20, 0);
    const auto packed = stb::zlib_compress(zeros);
    const auto back = stb::zlib_decompress(packed);
    ASSERT_EQ(zeros.size(), back.size());
    ASSERT_TRUE(back == zeros);
}

UTEST(libstb_zlib_tests, test_argument_errors) {
    const std::vector<std::uint8_t> d = {1, 2, 3};
    ASSERT_THROWS(stb::zlib_compress(d, -2), std::invalid_argument);
    ASSERT_THROWS(stb::zlib_compress(d, 10), std::invalid_argument);
    ASSERT_THROWS(stb::zlib_compress(nullptr, 3), std::invalid_argument);
    ASSERT_THROWS(stb::zlib_decompress(nullptr, 3), std::invalid_argument);
}
