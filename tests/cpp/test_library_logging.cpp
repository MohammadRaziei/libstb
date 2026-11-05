#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "test_common.hpp"

// What libstb itself logs (docs/adr/0003-logging-policy.md): nothing at the default level; at DEBUG one
// record per call for decode, encode, resize, zlib, SIMD and (when glyphs are missing) font rendering; WARNING
// only when libstb did something different from what was asked; and never a record for a failure that is
// also an exception.

namespace {

namespace lg = stb::logging;

using entry = testutil::log_capture::entry;
using capture = testutil::log_capture;

stb::image small_image() {
    stb::image img(8, 6, stb::pixel_format::rgb);
    img.fill(stb::color(10, 20, 30));
    return img;
}

stb::outline square(float x, float y, float size) {
    stb::outline p;
    p.move_to(x, y).line_to(x + size, y).line_to(x + size, y + size).line_to(x, y + size).close();
    return p;
}

}  // namespace

UTEST(libstb_library_logging_tests, test_the_library_is_silent_at_the_default_level) {
    capture c(lg::level::warning);
    const stb::image img = small_image();
    const std::vector<std::uint8_t> png = img.to_png();
    const stb::image back = stb::image::decode(png.data(), png.size());
    (void)back.resize(4, 3, "cubic");
    const std::vector<std::uint8_t> z = stb::zlib_compress(png);
    (void)stb::zlib_decompress(z);
    ASSERT_EQ(int(c.records.size()), 0);
}

UTEST(libstb_library_logging_tests, test_debug_has_one_record_per_call_under_its_area) {
    capture c(lg::level::debug);
    const stb::image img = small_image();
    const std::vector<std::uint8_t> png = img.to_png();
    ASSERT_EQ(c.count("libstb.encode"), 1);
    ASSERT_TRUE(c.first("libstb.encode")->message.find("encoded png: 8x6, 3 channel(s)") == 0);

    const stb::image back = stb::image::decode(png.data(), png.size());
    ASSERT_EQ(c.count("libstb.decode"), 1);
    ASSERT_TRUE(c.first("libstb.decode")->message.find("decoded 8x6, 3 channel(s)") == 0);

    (void)back.resize(4, 3, "cubic");
    ASSERT_EQ(c.count("libstb.resize"), 1);
    ASSERT_TRUE(c.first("libstb.resize")->message.find("resize 8x6 -> 4x3") == 0);

    const std::vector<std::uint8_t> z = stb::zlib_compress(png, 6);
    (void)stb::zlib_decompress(z);
    ASSERT_EQ(c.count("libstb.zlib"), 2);

    for (const entry& e : c.records) ASSERT_TRUE(e.level == lg::level::debug);
}

UTEST(libstb_library_logging_tests, test_a_failure_is_an_exception_and_not_also_a_record) {
    capture c(lg::level::debug);
    const std::vector<std::uint8_t> junk(64, 0x5A);
    ASSERT_THROWS(stb::image::decode(junk.data(), junk.size()), stb::decode_error);
    ASSERT_THROWS(stb::zlib_decompress(junk), stb::decode_error);
    stb::load_options opt;
    opt.max_bytes = 4;
    const std::vector<std::uint8_t> png = small_image().to_png();
    ASSERT_THROWS(stb::image::decode(png.data(), png.size(), opt), stb::limit_error);
    for (const entry& e : c.records) ASSERT_TRUE(int(e.level) < int(lg::level::error));
    ASSERT_EQ(c.count("libstb.decode"), 0);  // nothing was decoded
}

UTEST(libstb_library_logging_tests, test_the_simd_backend_choice_is_said_at_debug) {
    const std::string before = stb::simd_name();
    capture c(lg::level::debug);
    ASSERT_TRUE(stb::set_simd("scalar"));
    ASSERT_EQ(c.count("libstb.simd"), 1);
    ASSERT_TRUE(c.first("libstb.simd")->message == "backend set to scalar for the process");
    ASSERT_TRUE(stb::set_simd(before));
}

UTEST(libstb_library_logging_tests, test_input_libstb_had_to_change_is_a_single_warning) {
    stb::image img = small_image();
    const stb::color white(255, 255, 255);

    {  // a NaN coordinate: nothing is drawn, and it is said
        capture c(lg::level::warning);
        stb::fill(img, square(std::numeric_limits<float>::quiet_NaN(), 0.f, 4.f), white);
        ASSERT_EQ(c.count("libstb.raster"), 1);
        ASSERT_TRUE(c.first("libstb.raster")->level == lg::level::warning);
    }
    {  // a coordinate far outside: it is moved to the limit, and it is said
        capture c(lg::level::warning);
        stb::fill(img, square(0.f, 0.f, 1.0e7f), white);
        ASSERT_EQ(c.count("libstb.raster"), 1);
    }
    {  // a transformation that gives NaN: nothing is drawn, and it is said
        capture c(lg::level::warning);
        stb::draw_options options;
        options.transform.a = std::numeric_limits<float>::quiet_NaN();
        stb::fill(img, square(1.f, 1.f, 4.f), white, stb::fill_rule::non_zero, options);
        ASSERT_EQ(c.count("libstb.raster"), 1);
    }
    {  // an unusable dash pattern: the line is solid, and it is said
        capture c(lg::level::warning);
        stb::stroke_style style;
        style.width = 2.f;
        style.dashes = {-1.f, 2.f};
        stb::stroke(img, square(1.f, 1.f, 4.f), white, style);
        ASSERT_EQ(c.count("libstb.raster"), 1);
    }
    {  // ordinary input says nothing: a solid line, a good dash pattern, a zero width
        capture c(lg::level::warning);
        stb::stroke_style solid;
        solid.width = 2.f;
        stb::stroke(img, square(1.f, 1.f, 4.f), white, solid);
        stb::stroke_style dashed = solid;
        dashed.dashes = {3.f, 2.f};
        stb::stroke(img, square(1.f, 1.f, 4.f), white, dashed);
        stb::stroke_style none;
        none.width = 0.f;
        stb::stroke(img, square(1.f, 1.f, 4.f), white, none);
        ASSERT_EQ(int(c.records.size()), 0);
    }
}

UTEST(libstb_library_logging_tests, test_missing_glyphs_are_counted_at_debug) {
    const stb::font f = stb::font::open(std::string(LIBSTB_TEST_DATA_DIR) + "/libstb-test.ttf");
    capture c(lg::level::debug);
    (void)f.render("A", 16.f);
    ASSERT_EQ(c.count("libstb.font"), 0);  // every glyph is there: nothing to say
    (void)f.render("A\xE4\xB8\xAD", 16.f);  // U+4E2D is not in the test font
    ASSERT_EQ(c.count("libstb.font"), 1);
    ASSERT_TRUE(c.first("libstb.font")->message.find("render: 1 of 2 glyph(s) are not in the font") == 0);
}
