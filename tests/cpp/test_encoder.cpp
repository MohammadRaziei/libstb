#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>

#include "test_common.hpp"

using testutil::gradient;
using testutil::same;
using testutil::solid;

namespace {

libstb::image roundtrip(const libstb::encoder& enc, const libstb::image& img) {
    const auto bytes = img.encode(enc);
    return libstb::image::decode(bytes.data(), bytes.size());
}

}  // namespace

// ---- lossless formats ----------------------------------------------------

UTEST(libstb_encoder_tests, test_png_roundtrip_is_lossless_for_every_channel_count) {
    libstb::png_encoder png;
    for (int c = 1; c <= 4; ++c) {
        const libstb::image src = gradient(13, 7, c);
        ASSERT_TRUE(same(src, roundtrip(png, src)));
    }
}

UTEST(libstb_encoder_tests, test_png_output_has_png_signature) {
    const auto bytes = gradient(4, 4, 3).encode(libstb::png_encoder());
    ASSERT_TRUE(bytes.size() > 8);
    ASSERT_EQ(0x89, bytes[0]);
    ASSERT_EQ('P', bytes[1]);
    ASSERT_EQ('N', bytes[2]);
    ASSERT_EQ('G', bytes[3]);
}

UTEST(libstb_encoder_tests, test_png_compression_levels_all_roundtrip) {
    const libstb::image src = gradient(32, 32, 3);
    for (int level = 1; level <= 9; ++level) {
        ASSERT_TRUE(same(src, roundtrip(libstb::png_encoder(level), src)));
    }
}

UTEST(libstb_encoder_tests, test_bmp_roundtrip_is_lossless_rgb) {
    const libstb::image src = gradient(9, 5, 3);
    ASSERT_TRUE(same(src, roundtrip(libstb::bmp_encoder(), src)));
}

UTEST(libstb_encoder_tests, test_tga_roundtrip_with_and_without_rle) {
    for (int c : {3, 4}) {
        const libstb::image src = gradient(11, 6, c);
        ASSERT_TRUE(same(src, roundtrip(libstb::tga_encoder(true), src)));
        ASSERT_TRUE(same(src, roundtrip(libstb::tga_encoder(false), src)));
    }
}

UTEST(libstb_encoder_tests, test_tga_rle_shrinks_flat_images) {
    const libstb::image flat = solid(64, 64, 3, 200);
    ASSERT_TRUE(flat.encode(libstb::tga_encoder(true)).size() <
                flat.encode(libstb::tga_encoder(false)).size());
}

// ---- lossy ---------------------------------------------------------------

UTEST(libstb_encoder_tests, test_jpeg_roundtrip_is_close) {
    const libstb::image src = solid(16, 16, 3, 128);
    const libstb::image out = roundtrip(libstb::jpeg_encoder(95), src);
    ASSERT_EQ(16, out.width());
    ASSERT_EQ(16, out.height());
    ASSERT_EQ(3, out.channels());
    for (std::size_t i = 0; i < src.size_bytes(); ++i) {
        const int d = int(src.data()[i]) - int(out.data()[i]);
        ASSERT_TRUE(d >= -4 && d <= 4);
    }
}

UTEST(libstb_encoder_tests, test_jpeg_quality_trades_size) {
    const libstb::image src = gradient(64, 64, 3);
    ASSERT_TRUE(src.encode(libstb::jpeg_encoder(10)).size() < src.encode(libstb::jpeg_encoder(95)).size());
}

// ---- the encoder class hierarchy ----------------------------------------

UTEST(libstb_encoder_tests, test_accessors_and_extensions) {
    ASSERT_EQ(8, libstb::png_encoder().compression());
    ASSERT_EQ(90, libstb::jpeg_encoder().quality());
    ASSERT_TRUE(libstb::tga_encoder().rle());
    ASSERT_STREQ("png", libstb::png_encoder().extension());
    ASSERT_STREQ("jpg", libstb::jpeg_encoder().extension());
    ASSERT_STREQ("bmp", libstb::bmp_encoder().extension());
    ASSERT_STREQ("tga", libstb::tga_encoder().extension());
}

UTEST(libstb_encoder_tests, test_constructors_validate) {
    ASSERT_THROWS(libstb::png_encoder(0), std::invalid_argument);
    ASSERT_THROWS(libstb::png_encoder(10), std::invalid_argument);
    ASSERT_THROWS(libstb::jpeg_encoder(0), std::invalid_argument);
    ASSERT_THROWS(libstb::jpeg_encoder(101), std::invalid_argument);
}

UTEST(libstb_encoder_tests, test_polymorphic_use_through_the_base_class) {
    std::vector<std::unique_ptr<libstb::encoder>> encoders;
    encoders.push_back(std::make_unique<libstb::png_encoder>());
    encoders.push_back(std::make_unique<libstb::jpeg_encoder>(80));
    encoders.push_back(std::make_unique<libstb::bmp_encoder>());
    encoders.push_back(std::make_unique<libstb::tga_encoder>());
    const libstb::image src = gradient(8, 8, 3);
    for (const auto& e : encoders) {
        const auto bytes = e->encode(src);  // virtual dispatch via the base
        ASSERT_FALSE(bytes.empty());
        const libstb::image back = libstb::image::decode(bytes.data(), bytes.size());
        ASSERT_EQ(8, back.width());
    }
}

UTEST(libstb_encoder_tests, test_for_path_picks_encoder_by_extension_case_insensitively) {
    ASSERT_STREQ("png", libstb::encoder::for_path("a/b.png")->extension());
    ASSERT_STREQ("png", libstb::encoder::for_path("A.PNG")->extension());
    ASSERT_STREQ("jpg", libstb::encoder::for_path("a.jpg")->extension());
    ASSERT_STREQ("jpg", libstb::encoder::for_path("a.JPEG")->extension());
    ASSERT_STREQ("bmp", libstb::encoder::for_path("a.bmp")->extension());
    ASSERT_STREQ("tga", libstb::encoder::for_path("a.tga")->extension());
    ASSERT_THROWS(libstb::encoder::for_path("a.gif"), std::invalid_argument);
    ASSERT_THROWS(libstb::encoder::for_path("noext"), std::invalid_argument);
}

UTEST(libstb_encoder_tests, test_encoding_an_empty_image_is_an_argument_error) {
    ASSERT_THROWS(libstb::image().encode(libstb::png_encoder()), std::invalid_argument);
}

// ---- save ----------------------------------------------------------------

UTEST(libstb_encoder_tests, test_save_by_extension_and_open_back) {
    const auto path = std::filesystem::temp_directory_path() / "libstb_test_save.png";
    const libstb::image src = gradient(10, 10, 4);
    src.save(path);
    ASSERT_TRUE(same(src, libstb::image::open(path)));
    std::filesystem::remove(path);
}

UTEST(libstb_encoder_tests, test_save_with_explicit_encoder_ignores_extension) {
    const auto path = std::filesystem::temp_directory_path() / "libstb_test_save.dat";
    gradient(6, 6, 3).save(path, libstb::bmp_encoder());
    ASSERT_EQ(3, libstb::image_info::read_file(path).channels);
    std::filesystem::remove(path);
}

UTEST(libstb_encoder_tests, test_save_with_unknown_extension_writes_nothing) {
    const auto path = std::filesystem::temp_directory_path() / "libstb_test_save.gif";
    std::filesystem::remove(path);
    ASSERT_THROWS(gradient(4, 4, 3).save(path), std::invalid_argument);
    ASSERT_FALSE(std::filesystem::exists(path));
}

UTEST(libstb_encoder_tests, test_save_to_unwritable_path_throws_io_error) {
    const auto path = std::filesystem::temp_directory_path() / "libstb_no_such_dir" / "x.png";
    ASSERT_THROWS(gradient(4, 4, 3).save(path), libstb::io_error);
}

// ---- threads -------------------------------------------------------------

UTEST(libstb_encoder_tests, test_concurrent_encoders_with_different_settings) {
    const libstb::image src = gradient(24, 24, 3);
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&, t] {
            libstb::png_encoder png(t % 2 ? 1 : 9);
            libstb::tga_encoder tga(t % 2 == 0);
            for (int i = 0; i < 200; ++i) {
                if (!same(src, roundtrip(png, src))) ++bad;
                if (!same(src, roundtrip(tga, src))) ++bad;
            }
        });
    }
    for (auto& th : ts) th.join();
    ASSERT_EQ(0, bad.load());
}
