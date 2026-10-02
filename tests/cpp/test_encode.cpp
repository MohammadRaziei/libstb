#include <atomic>
#include <filesystem>
#include <thread>
#include <tuple>

#include "test_common.hpp"

using testutil::gradient;
using testutil::same;
using testutil::solid;

namespace {

stb::image roundtrip(const std::vector<std::uint8_t>& bytes) {
    return stb::image::decode(bytes.data(), bytes.size());
}

std::filesystem::path tmp(const char* name) { return std::filesystem::temp_directory_path() / name; }

}  // namespace

// ---- lossless formats ----------------------------------------------------

UTEST(libstb_encode_tests, test_png_roundtrip_is_lossless_for_every_channel_count) {
    for (int c = 1; c <= 4; ++c) {
        const stb::image src = gradient(13, 7, c);
        ASSERT_TRUE(same(src, roundtrip(src.to_png())));
    }
}

namespace {

// Deterministic pseudo-random bytes (an LCG), identical on every platform.
stb::image content(int kind, int w, int h, int c) {
    stb::image img(w, h, c);
    std::uint32_t s = 12345u + static_cast<std::uint32_t>(kind);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < c; ++k) {
                s = s * 1103515245u + 12345u;
                const auto noise = static_cast<std::uint8_t>(s >> 16);
                std::uint8_t v = 0;
                switch (kind) {
                    case 0: v = noise; break;                                           // incompressible: stored blocks
                    case 1: v = 77; break;                                              // flat: long runs
                    case 2: v = static_cast<std::uint8_t>(x * 3 + y * 2 + k * 40); break;  // smooth
                    case 3: v = static_cast<std::uint8_t>(((x / 16 + y / 16) & 3) * 60); break;  // tiles
                    default: v = static_cast<std::uint8_t>(128 + (x + y) / 8 + (noise & 7)); break;  // photo-like
                }
                img(x, y, k) = v;
            }
    return img;
}

}  // namespace

// Exact, not approximate: PNG is lossless. Every kind of content exercises a different deflate
// path (stored / fixed / dynamic Huffman blocks, long runs, long distances).
UTEST(libstb_encode_tests, test_png_roundtrip_is_exact_for_every_kind_of_content) {
    for (int kind = 0; kind < 5; ++kind)
        for (int c = 1; c <= 4; ++c)
            for (int level : {1, 5, 9}) {
                const stb::image src = content(kind, 97, 61, c);
                ASSERT_TRUE(same(src, roundtrip(src.to_png(level))));
            }
}

// Millions of tokens: well past one 64K-token deflate block.
UTEST(libstb_encode_tests, test_png_roundtrip_is_exact_across_many_deflate_blocks) {
    for (int kind : {0, 3, 4}) {
        const stb::image src = content(kind, 900, 700, 3);
        ASSERT_TRUE(same(src, roundtrip(src.to_png())));
    }
}

UTEST(libstb_encode_tests, test_png_roundtrip_is_exact_for_degenerate_sizes) {
    for (auto [w, h, c] : {std::tuple<int, int, int>{1, 1, 1}, {1, 2, 3}, {2, 1, 4}, {3000, 1, 3}, {1, 3000, 3}}) {
        const stb::image src = content(4, w, h, c);
        ASSERT_TRUE(same(src, roundtrip(src.to_png())));
    }
}

UTEST(libstb_encode_tests, test_png_output_has_png_signature) {
    const auto bytes = gradient(4, 4, 3).to_png();
    ASSERT_TRUE(bytes.size() > 8);
    ASSERT_EQ(0x89, bytes[0]);
    ASSERT_EQ('P', bytes[1]);
    ASSERT_EQ('N', bytes[2]);
    ASSERT_EQ('G', bytes[3]);
}

UTEST(libstb_encode_tests, test_png_compression_levels_all_roundtrip) {
    const stb::image src = gradient(32, 32, 3);
    for (int level = 1; level <= 9; ++level) {
        ASSERT_TRUE(same(src, roundtrip(src.to_png(level))));
    }
}

UTEST(libstb_encode_tests, test_bmp_roundtrip_is_lossless_rgb) {
    const stb::image src = gradient(9, 5, 3);
    ASSERT_TRUE(same(src, roundtrip(src.to_bmp())));
}

UTEST(libstb_encode_tests, test_tga_roundtrip_with_and_without_rle) {
    for (int c : {3, 4}) {
        const stb::image src = gradient(11, 6, c);
        ASSERT_TRUE(same(src, roundtrip(src.to_tga(true))));
        ASSERT_TRUE(same(src, roundtrip(src.to_tga(false))));
    }
}

UTEST(libstb_encode_tests, test_tga_rle_shrinks_flat_images) {
    const stb::image flat = solid(64, 64, 3, 200);
    ASSERT_TRUE(flat.to_tga(true).size() < flat.to_tga(false).size());
}

// ---- lossy ---------------------------------------------------------------

UTEST(libstb_encode_tests, test_jpg_roundtrip_is_close) {
    const stb::image src = solid(16, 16, 3, 128);
    const stb::image out = roundtrip(src.to_jpg(95));
    ASSERT_EQ(16, out.width());
    ASSERT_EQ(16, out.height());
    ASSERT_EQ(3, out.channels());
    for (std::size_t i = 0; i < src.size_bytes(); ++i) {
        const int d = int(src.data()[i]) - int(out.data()[i]);
        ASSERT_TRUE(d >= -4 && d <= 4);
    }
}

UTEST(libstb_encode_tests, test_jpg_quality_trades_size) {
    const stb::image src = gradient(64, 64, 3);
    ASSERT_TRUE(src.to_jpg(10).size() < src.to_jpg(95).size());
}

// ---- arguments -----------------------------------------------------------

UTEST(libstb_encode_tests, test_default_arguments_are_the_documented_ones) {
    ASSERT_EQ(8, stb::default_png_compression);
    ASSERT_EQ(90, stb::default_jpg_quality);
    const stb::image src = gradient(20, 20, 3);
    ASSERT_TRUE(src.to_png() == src.to_png(stb::default_png_compression));
    ASSERT_TRUE(src.to_jpg() == src.to_jpg(stb::default_jpg_quality));
    ASSERT_TRUE(src.to_tga() == src.to_tga(true));
}

UTEST(libstb_encode_tests, test_arguments_are_validated) {
    const stb::image src = gradient(4, 4, 3);
    ASSERT_THROWS(src.to_png(0), std::invalid_argument);
    ASSERT_THROWS(src.to_png(10), std::invalid_argument);
    ASSERT_THROWS(src.to_jpg(0), std::invalid_argument);
    ASSERT_THROWS(src.to_jpg(101), std::invalid_argument);
}

UTEST(libstb_encode_tests, test_encoding_an_empty_image_is_an_argument_error) {
    const stb::image empty;
    ASSERT_THROWS(empty.to_png(), std::invalid_argument);
    ASSERT_THROWS(empty.to_jpg(), std::invalid_argument);
    ASSERT_THROWS(empty.to_bmp(), std::invalid_argument);
    ASSERT_THROWS(empty.to_tga(), std::invalid_argument);
}

// ---- write_* -------------------------------------------------------------

UTEST(libstb_encode_tests, test_write_matches_to_and_ignores_the_extension) {
    const stb::image src = gradient(6, 6, 3);
    const auto path = tmp("libstb_test_write.dat");
    src.write_bmp(path);
    ASSERT_EQ(3, stb::image_info::read_file(path).channels);
    ASSERT_TRUE(same(src, stb::image::open(path)));
    src.write_png(path, 9);
    ASSERT_TRUE(same(src, stb::image::open(path)));
    src.write_tga(path, false);
    ASSERT_TRUE(same(src, stb::image::open(path)));
    src.write_jpg(path, 95);
    ASSERT_EQ(6, stb::image::open(path).width());
    std::filesystem::remove(path);
}

UTEST(libstb_encode_tests, test_write_with_a_bad_argument_writes_nothing) {
    const auto path = tmp("libstb_test_write_bad.jpg");
    std::filesystem::remove(path);
    ASSERT_THROWS(gradient(4, 4, 3).write_jpg(path, 0), std::invalid_argument);
    ASSERT_FALSE(std::filesystem::exists(path));
}

// ---- write ----------------------------------------------------------------

UTEST(libstb_encode_tests, test_write_by_extension_and_open_back) {
    const auto path = tmp("libstb_test_save.png");
    const stb::image src = gradient(10, 10, 4);
    src.write(path);
    ASSERT_TRUE(same(src, stb::image::open(path)));
    std::filesystem::remove(path);
}

UTEST(libstb_encode_tests, test_write_picks_the_format_case_insensitively) {
    const stb::image src = gradient(8, 8, 3);
    for (const char* name : {"libstb_test_a.PNG", "libstb_test_a.bmp", "libstb_test_a.Tga"}) {
        const auto path = tmp(name);
        src.write(path);
        ASSERT_TRUE(same(src, stb::image::open(path)));  // lossless => the right format was chosen
        std::filesystem::remove(path);
    }
    for (const char* name : {"libstb_test_a.jpg", "libstb_test_a.JPEG"}) {
        const auto path = tmp(name);
        src.write(path);
        ASSERT_EQ(8, stb::image::open(path).width());
        ASSERT_FALSE(same(stb::image::open(path), src));  // lossy => it really is a JPEG
        std::filesystem::remove(path);
    }
}

UTEST(libstb_encode_tests, test_write_with_unknown_extension_writes_nothing) {
    const auto path = tmp("libstb_test_save.gif");
    std::filesystem::remove(path);
    ASSERT_THROWS(gradient(4, 4, 3).write(path), std::invalid_argument);
    ASSERT_FALSE(std::filesystem::exists(path));
    ASSERT_THROWS(gradient(4, 4, 3).write(tmp("libstb_noext")), std::invalid_argument);
}

UTEST(libstb_encode_tests, test_write_to_unwritable_path_throws_io_error) {
    const auto path = tmp("libstb_no_such_dir") / "x.png";
    ASSERT_THROWS(gradient(4, 4, 3).write(path), stb::io_error);
    ASSERT_THROWS(gradient(4, 4, 3).write_png(path), stb::io_error);
}

// ---- threads -------------------------------------------------------------

UTEST(libstb_encode_tests, test_concurrent_encoding_with_different_settings) {
    const stb::image src = gradient(24, 24, 3);
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < 200; ++i) {
                if (!same(src, roundtrip(src.to_png(t % 2 ? 1 : 9)))) ++bad;
                if (!same(src, roundtrip(src.to_tga(t % 2 == 0)))) ++bad;
            }
        });
    }
    for (auto& th : ts) th.join();
    ASSERT_EQ(0, bad.load());
}
