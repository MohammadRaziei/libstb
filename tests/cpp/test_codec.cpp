// White-box tests of src/codec: every encoder against the decoder, and the compressor against the
// inflater, on the sizes where the code has edges (a row that needs BMP padding, a run longer than
// one TGA packet, a block boundary of the compressor). They use only the codec's own functions,
// so they keep holding however the code behind them is organised.

#include "codec/codec.hpp"
#include "test_common.hpp"

namespace {

using stb::codec::bytes;
using stb::codec::raster;

// Deterministic pixels with a smooth part, a flat part (long runs) and noise.
bytes pixels(int w, int h, int c, unsigned seed) {
    bytes v(std::size_t(w) * h * c);
    std::uint32_t x = seed * 2654435761u + 1;
    for (std::size_t i = 0; i < v.size(); ++i) {
        x = x * 1664525u + 1013904223u;
        const std::size_t px = i / std::size_t(c);
        v[i] = px % 61 < 20 ? std::uint8_t(7) : px % 61 < 40 ? std::uint8_t(px * 3 + i % std::size_t(c)) : std::uint8_t(x >> 24);
    }
    return v;
}

struct size_case {
    int w, h;
};
const size_case sizes[] = {{1, 1}, {2, 3}, {5, 7}, {33, 17}, {130, 4}, {257, 2}, {64, 64}};

bool decodes_exactly(const bytes& file, const bytes& want, int w, int h, int c) {
    const auto d = stb::codec::decode(file.data(), file.size(), c, false);
    return d.width == w && d.height == h && d.channels == c &&
           std::equal(want.begin(), want.end(), d.pixels.get());
}

}  // namespace

UTEST(libstb_codec_tests, test_lossless_encoders_round_trip_exactly) {
    for (const size_case s : sizes) {
        for (int c = 1; c <= 4; ++c) {
            const bytes px = pixels(s.w, s.h, c, unsigned(s.w * 31 + c));
            const raster img{px.data(), s.w, s.h, c};
            ASSERT_TRUE(decodes_exactly(stb::codec::encode_png(img, 6), px, s.w, s.h, c));
            ASSERT_TRUE(decodes_exactly(stb::codec::encode_tga(img, false), px, s.w, s.h, c));
            ASSERT_TRUE(decodes_exactly(stb::codec::encode_tga(img, true), px, s.w, s.h, c));
            if (c != 2) ASSERT_TRUE(decodes_exactly(stb::codec::encode_bmp(img), px, s.w, s.h, c));  // BMP has no gray + alpha
        }
    }
}

UTEST(libstb_codec_tests, test_png_round_trips_at_every_compression) {
    const bytes px = pixels(97, 61, 3, 5);
    for (int level = 1; level <= 9; ++level)
        ASSERT_TRUE(decodes_exactly(stb::codec::encode_png({px.data(), 97, 61, 3}, level), px, 97, 61, 3));
}

UTEST(libstb_codec_tests, test_tga_rle_handles_runs_longer_than_a_packet_and_alternating_pixels) {
    for (int c = 1; c <= 4; ++c) {
        // 300 identical pixels (three packets), then alternating ones, then identical again.
        const int w = 700;
        bytes px(std::size_t(w) * c, 9);
        for (int x = 300; x < 500; ++x)
            for (int k = 0; k < c; ++k) px[std::size_t(x) * c + k] = std::uint8_t((x & 1) ? 200 + k : 20 + k);
        const raster img{px.data(), w, 1, c};
        ASSERT_TRUE(decodes_exactly(stb::codec::encode_tga(img, true), px, w, 1, c));
        ASSERT_TRUE(stb::codec::encode_tga(img, true).size() < px.size() + 64);  // the runs did compress
    }
}

UTEST(libstb_codec_tests, test_jpeg_has_markers_the_right_size_and_stays_close) {
    const int w = 50, h = 37;  // neither a multiple of 8 nor of 16
    bytes ramp(std::size_t(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < 3; ++k) ramp[(std::size_t(y) * w + x) * 3 + k] = std::uint8_t(x * 3 + y * 2 + k * 20);
    for (int quality : {30, 90, 91, 100}) {
        const bytes jpg = stb::codec::encode_jpeg({ramp.data(), w, h, 3}, quality);
        ASSERT_EQ(0xFF, int(jpg[0]));
        ASSERT_EQ(0xD8, int(jpg[1]));  // SOI
        ASSERT_EQ(0xFF, int(jpg[jpg.size() - 2]));
        ASSERT_EQ(0xD9, int(jpg.back()));  // EOI
        const auto d = stb::codec::decode(jpg.data(), jpg.size(), 3, false);
        ASSERT_EQ(w, d.width);
        ASSERT_EQ(h, d.height);
        long sum = 0;
        for (std::size_t i = 0; i < ramp.size(); ++i) sum += std::abs(int(d.pixels.get()[i]) - int(ramp[i]));
        ASSERT_TRUE(double(sum) / double(ramp.size()) < (quality < 50 ? 12.0 : 6.0));
    }
}

UTEST(libstb_codec_tests, test_read_header_reports_the_encoded_size_and_channels) {
    const bytes px = pixels(11, 29, 4, 1);
    const bytes png = stb::codec::encode_png({px.data(), 11, 29, 4}, 6);
    const auto h = stb::codec::read_header(png.data(), png.size());
    ASSERT_EQ(11, h.width);
    ASSERT_EQ(29, h.height);
    ASSERT_EQ(4, h.channels);
    ASSERT_THROWS(stb::codec::read_header(px.data(), 3), stb::decode_error);
}

UTEST(libstb_codec_tests, test_deflate_round_trips_at_every_effort_and_block_edge) {
    for (std::size_t n : {std::size_t(0), std::size_t(1), std::size_t(3), std::size_t(4), std::size_t(258), std::size_t(65535),
                          std::size_t(65536), std::size_t(65537), std::size_t(200000)}) {
        const bytes data = pixels(int(n), 1, 1, unsigned(n));  // n bytes
        for (int effort = 0; effort <= 10; ++effort) {
            const bytes z = effort == 0 ? stb::codec::zlib_store(data.data(), data.size())
                                        : stb::codec::zlib_deflate(data.data(), data.size(), effort);
            ASSERT_EQ(0x78, int(z[0]));
            const auto r = stb::codec::zlib_inflate(z.data(), z.size(), 1 << 16, std::size_t(1) << 30);
            ASSERT_TRUE(r.status == stb::codec::inflate_status::ok);
            ASSERT_TRUE(r.data == data);
            // The trailer is the Adler-32 of the data, big endian.
            const std::uint32_t sum = stb::codec::adler32(data.data(), data.size());
            ASSERT_EQ(int(sum >> 24), int(z[z.size() - 4]));
            ASSERT_EQ(int(sum & 0xff), int(z[z.size() - 1]));
        }
    }
}

UTEST(libstb_codec_tests, test_inflate_stops_at_its_ceiling_and_reports_corrupt_input) {
    const bytes data(100000, 'a');
    const bytes z = stb::codec::zlib_deflate(data.data(), data.size(), 6);
    ASSERT_TRUE(stb::codec::zlib_inflate(z.data(), z.size(), 1024, 4096).status == stb::codec::inflate_status::too_large);
    const bytes junk = {0x78, 0x9c, 0x01, 0x02, 0x03};
    ASSERT_TRUE(stb::codec::zlib_inflate(junk.data(), junk.size(), 1024, 4096).status == stb::codec::inflate_status::corrupt);
}
