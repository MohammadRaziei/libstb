#include <atomic>
#include <thread>

#include "conftest.hpp"

using testutil::gradient;
using testutil::same;
using testutil::solid;

namespace {

stb::resizer opts(stb::resize_filter f, bool srgb = true) { return stb::resizer(f).srgb(srgb); }

}  // namespace

UTEST(libstb_resizer_tests, test_size_and_channels_are_preserved) {
    stb::resizer r;
    for (int c = 1; c <= 4; ++c) {
        stb::image out = r.resize(gradient(10, 8, c), 5, 20);
        ASSERT_EQ(5, out.width());
        ASSERT_EQ(20, out.height());
        ASSERT_EQ(c, out.channels());
    }
}

UTEST(libstb_resizer_tests, test_solid_colour_survives_every_filter_and_edge) {
    using F = stb::resize_filter;
    using E = stb::resize_edge;
    for (F f : {F::automatic, F::box, F::triangle, F::cubic_bspline, F::catmull_rom, F::mitchell, F::point})
        for (E e : {E::clamp, E::reflect, E::wrap, E::zero}) {
            // ZERO edges legitimately darken the border when shrinking with
            // wide filters, so only assert the interior stays solid.
            stb::image out = stb::resizer(f).edge(e).resize(solid(16, 16, 3, 100), 8, 8);
            ASSERT_EQ(100, out(4, 4, 0));
            ASSERT_EQ(100, out(4, 4, 2));
        }
}

UTEST(libstb_resizer_tests, test_point_upscale_duplicates_pixels) {
    const stb::image src = gradient(2, 2, 3);
    stb::image out = opts(stb::resize_filter::point).resize(src, 4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 3; ++c) ASSERT_EQ(src(x / 2, y / 2, c), out(x, y, c));
}

UTEST(libstb_resizer_tests, test_box_downscale_averages) {
    const stb::image src(2, 1, stb::pixel_format::gray, std::vector<std::uint8_t>{0, 255});
    const int lin = opts(stb::resize_filter::box, false).resize(src, 1, 1)(0, 0);
    ASSERT_TRUE(lin >= 127 && lin <= 128);
}

UTEST(libstb_resizer_tests, test_srgb_blends_in_linear_light) {
    const stb::image src(2, 1, stb::pixel_format::gray, std::vector<std::uint8_t>{0, 255});
    // mid-way in *linear light* is ~188 in sRGB, not 127.
    const int v = opts(stb::resize_filter::box, true).resize(src, 1, 1)(0, 0);
    ASSERT_TRUE(v >= 186 && v <= 190);
}

UTEST(libstb_resizer_tests, test_alpha_weighting_stops_transparent_pixels_bleeding) {
    // opaque red next to fully transparent green
    const stb::image src(2, 1, stb::pixel_format::rgba, std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 0});
    const stb::image out = opts(stb::resize_filter::box).resize(src, 1, 1);
    ASSERT_TRUE(out(0, 0, 0) >= 250);  // stays red
    ASSERT_TRUE(out(0, 0, 1) <= 5);    // no green leaked in
    ASSERT_TRUE(out(0, 0, 3) >= 126 && out(0, 0, 3) <= 129);  // alpha averaged linearly
}

UTEST(libstb_resizer_tests, test_source_is_not_modified) {
    const stb::image src = gradient(6, 6, 3);
    const stb::image copy = src.copy();
    stb::resizer().resize(src, 3, 3);
    ASSERT_TRUE(same(src, copy));
}

UTEST(libstb_resizer_tests, test_errors) {
    stb::resizer r;
    ASSERT_THROWS(r.resize(stb::image(), 4, 4), std::invalid_argument);
    ASSERT_THROWS(r.resize(gradient(2, 2, 3), 0, 4), std::invalid_argument);
    ASSERT_THROWS(r.resize(gradient(2, 2, 3), 4, -1), std::invalid_argument);

    const stb::resizer tiny = stb::resizer().max_bytes(100);  // (not "small": <windows.h> #defines that as char)
    ASSERT_THROWS(tiny.resize(gradient(2, 2, 3), 10, 10), stb::limit_error);   // 300 bytes
    ASSERT_EQ(std::size_t(100), tiny.resize(gradient(2, 2, 1), 10, 10).size_bytes());          // exactly 100

    ASSERT_THROWS(stb::resizer(static_cast<stb::resize_filter>(99)), std::invalid_argument);
    stb::resizer r2;
    ASSERT_THROWS(r2.filter(static_cast<stb::resize_filter>(99)), std::invalid_argument);
    ASSERT_THROWS(r2.edge(static_cast<stb::resize_edge>(99)), std::invalid_argument);
    ASSERT_THROWS(r2.filter(std::string_view("nope")), std::invalid_argument);
    ASSERT_TRUE(r2.filter() == stb::resize_filter::automatic && r2.edge() == stb::resize_edge::clamp);  // refused calls change nothing
}

UTEST(libstb_resizer_tests, test_concurrent_resizes) {
    const stb::image src = gradient(64, 48, 4);
    const stb::resizer r;
    const stb::image ref = r.resize(src, 20, 15);
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t)
        ts.emplace_back([&] {
            for (int i = 0; i < 100; ++i)
                if (!same(ref, r.resize(src, 20, 15))) ++bad;
        });
    for (auto& th : ts) th.join();
    ASSERT_EQ(0, bad.load());
}

UTEST(libstb_resizer_tests, test_filter_names_map_to_filters) {
    using F = stb::resize_filter;
    ASSERT_TRUE(stb::resize_filter_from_name("auto") == F::automatic);
    ASSERT_TRUE(stb::resize_filter_from_name("default") == F::automatic);
    ASSERT_TRUE(stb::resize_filter_from_name("nearest") == F::point);
    ASSERT_TRUE(stb::resize_filter_from_name("linear") == F::triangle);
    ASSERT_TRUE(stb::resize_filter_from_name("bilinear") == F::triangle);
    ASSERT_TRUE(stb::resize_filter_from_name("cubic") == F::catmull_rom);
    ASSERT_TRUE(stb::resize_filter_from_name("bicubic") == F::catmull_rom);
    ASSERT_TRUE(stb::resize_filter_from_name("bspline") == F::cubic_bspline);
    ASSERT_TRUE(stb::resize_filter_from_name("mitchell") == F::mitchell);
    ASSERT_TRUE(stb::resize_filter_from_name("box") == F::box);
    ASSERT_TRUE(stb::resize_filter_from_name("area") == F::box);
    // case and separators are forgiving
    ASSERT_TRUE(stb::resize_filter_from_name("Catmull-Rom") == F::catmull_rom);
    ASSERT_TRUE(stb::resize_filter_from_name("CUBIC BSPLINE") == F::cubic_bspline);
}

UTEST(libstb_resizer_tests, test_unknown_filter_name_throws_and_lists_valid_names) {
    ASSERT_THROWS(stb::resize_filter_from_name("lanczos"), std::invalid_argument);
    ASSERT_THROWS(stb::resize_filter_from_name(""), std::invalid_argument);
    ASSERT_THROWS(stb::resizer("nope"), std::invalid_argument);
    try {
        stb::resize_filter_from_name("lanczos");
    } catch (const std::invalid_argument& e) {
        const std::string msg = e.what();
        ASSERT_TRUE(msg.find("lanczos") != std::string::npos);
        ASSERT_TRUE(msg.find("cubic") != std::string::npos);
    }
}

UTEST(libstb_resizer_tests, test_resizer_from_name_or_filter_keeps_other_defaults) {
    const stb::resizer by_name("cubic");
    ASSERT_TRUE(by_name.filter() == stb::resize_filter::catmull_rom);
    ASSERT_TRUE(by_name.edge() == stb::resize_edge::clamp);
    ASSERT_TRUE(by_name.srgb());
    ASSERT_EQ(std::size_t(1) << 29, by_name.max_bytes());

    const stb::resizer by_enum(stb::resize_filter::mitchell);
    ASSERT_TRUE(by_enum.filter() == stb::resize_filter::mitchell);
    const stb::resizer chained = stb::resizer(stb::resize_filter::box).edge(stb::resize_edge::wrap).srgb(false).max_bytes(7).filter("mitchell");
    ASSERT_TRUE(chained.filter() == stb::resize_filter::mitchell && chained.edge() == stb::resize_edge::wrap && !chained.srgb() && chained.max_bytes() == 7);
}

UTEST(libstb_resizer_tests, test_image_resize_default_pointer_name_and_enum_agree) {
    const stb::image src = gradient(16, 12, 3);

    // nullptr and the no-argument form both mean a default resizer
    const stb::image ref = stb::resizer{}.resize(src, 8, 6);
    ASSERT_TRUE(same(ref, src.resize(8, 6)));
    ASSERT_TRUE(same(ref, src.resize(8, 6, nullptr)));

    // an explicit resizer
    const stb::resizer mitchell(stb::resize_filter::mitchell);
    ASSERT_TRUE(same(mitchell.resize(src, 8, 6), src.resize(8, 6, &mitchell)));

    // by name and by enum
    ASSERT_TRUE(same(stb::resizer("cubic").resize(src, 8, 6), src.resize(8, 6, "cubic")));
    ASSERT_TRUE(same(mitchell.resize(src, 8, 6), src.resize(8, 6, stb::resize_filter::mitchell)));

    // "nearest" really is nearest-neighbour
    const stb::image tiny = gradient(2, 2, 3);
    const stb::image big = tiny.resize(4, 4, "nearest");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 3; ++c) ASSERT_EQ(tiny(x / 2, y / 2, c), big(x, y, c));
}

UTEST(libstb_resizer_tests, test_image_resize_errors) {
    const stb::image src = gradient(4, 4, 3);
    ASSERT_THROWS(src.resize(0, 4), std::invalid_argument);
    ASSERT_THROWS(src.resize(4, 4, "nope"), std::invalid_argument);
    ASSERT_THROWS(stb::image().resize(4, 4), std::invalid_argument);

    const stb::resizer r = stb::resizer().max_bytes(100);
    ASSERT_THROWS(src.resize(10, 10, &r), stb::limit_error);
}
