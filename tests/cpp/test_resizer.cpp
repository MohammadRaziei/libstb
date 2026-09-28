#include <atomic>
#include <thread>

#include "test_common.hpp"

using testutil::gradient;
using testutil::same;
using testutil::solid;

namespace {

libstb::resize_options opts(libstb::resize_filter f, bool srgb = true) {
    libstb::resize_options o;
    o.filter = f;
    o.srgb = srgb;
    return o;
}

}  // namespace

UTEST(libstb_resizer_tests, test_size_and_channels_are_preserved) {
    libstb::resizer r;
    for (int c = 1; c <= 4; ++c) {
        libstb::image out = r.resize(gradient(10, 8, c), 5, 20);
        ASSERT_EQ(5, out.width());
        ASSERT_EQ(20, out.height());
        ASSERT_EQ(c, out.channels());
    }
}

UTEST(libstb_resizer_tests, test_solid_colour_survives_every_filter_and_edge) {
    using F = libstb::resize_filter;
    using E = libstb::resize_edge;
    for (F f : {F::automatic, F::box, F::triangle, F::cubic_bspline, F::catmull_rom, F::mitchell, F::point})
        for (E e : {E::clamp, E::reflect, E::wrap, E::zero}) {
            libstb::resize_options o;
            o.filter = f;
            o.edge = e;
            // ZERO edges legitimately darken the border when shrinking with
            // wide filters, so only assert the interior stays solid.
            libstb::image out = libstb::resizer(o).resize(solid(16, 16, 3, 100), 8, 8);
            ASSERT_EQ(100, out(4, 4, 0));
            ASSERT_EQ(100, out(4, 4, 2));
        }
}

UTEST(libstb_resizer_tests, test_point_upscale_duplicates_pixels) {
    const libstb::image src = gradient(2, 2, 3);
    libstb::image out = libstb::resizer(opts(libstb::resize_filter::point)).resize(src, 4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 3; ++c) ASSERT_EQ(src(x / 2, y / 2, c), out(x, y, c));
}

UTEST(libstb_resizer_tests, test_box_downscale_averages) {
    const libstb::image src(2, 1, 1, std::vector<std::uint8_t>{0, 255});
    const int lin = libstb::resizer(opts(libstb::resize_filter::box, false)).resize(src, 1, 1)(0, 0);
    ASSERT_TRUE(lin >= 127 && lin <= 128);
}

UTEST(libstb_resizer_tests, test_srgb_blends_in_linear_light) {
    const libstb::image src(2, 1, 1, std::vector<std::uint8_t>{0, 255});
    // mid-way in *linear light* is ~188 in sRGB, not 127.
    const int v = libstb::resizer(opts(libstb::resize_filter::box, true)).resize(src, 1, 1)(0, 0);
    ASSERT_TRUE(v >= 186 && v <= 190);
}

UTEST(libstb_resizer_tests, test_alpha_weighting_stops_transparent_pixels_bleeding) {
    // opaque red next to fully transparent green
    const libstb::image src(2, 1, 4, std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 0});
    const libstb::image out = libstb::resizer(opts(libstb::resize_filter::box)).resize(src, 1, 1);
    ASSERT_TRUE(out(0, 0, 0) >= 250);  // stays red
    ASSERT_TRUE(out(0, 0, 1) <= 5);    // no green leaked in
    ASSERT_TRUE(out(0, 0, 3) >= 126 && out(0, 0, 3) <= 129);  // alpha averaged linearly
}

UTEST(libstb_resizer_tests, test_source_is_not_modified) {
    const libstb::image src = gradient(6, 6, 3);
    const libstb::image copy = src;
    libstb::resizer().resize(src, 3, 3);
    ASSERT_TRUE(same(src, copy));
}

UTEST(libstb_resizer_tests, test_errors) {
    libstb::resizer r;
    ASSERT_THROWS(r.resize(libstb::image(), 4, 4), std::invalid_argument);
    ASSERT_THROWS(r.resize(gradient(2, 2, 3), 0, 4), std::invalid_argument);
    ASSERT_THROWS(r.resize(gradient(2, 2, 3), 4, -1), std::invalid_argument);

    libstb::resize_options tiny;  // (not "small": <windows.h> #defines that as char)
    tiny.max_bytes = 100;
    ASSERT_THROWS(libstb::resizer(tiny).resize(gradient(2, 2, 3), 10, 10), libstb::limit_error);   // 300 bytes
    ASSERT_EQ(100, libstb::resizer(tiny).resize(gradient(2, 2, 1), 10, 10).size_bytes());          // exactly 100

    libstb::resize_options bad;
    bad.filter = static_cast<libstb::resize_filter>(99);
    ASSERT_THROWS(libstb::resizer{bad}, std::invalid_argument);
}

UTEST(libstb_resizer_tests, test_concurrent_resizes) {
    const libstb::image src = gradient(64, 48, 4);
    const libstb::resizer r;
    const libstb::image ref = r.resize(src, 20, 15);
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
