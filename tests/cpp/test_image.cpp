#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "utest/utest.h"
#include "libstb.h"

// utest.h has no exception macro.
#define ASSERT_THROWS(expr, ex)          \
    do {                                 \
        bool caught_ = false;            \
        try {                            \
            (void)(expr);                \
        } catch (const ex&) {            \
            caught_ = true;              \
        }                                \
        ASSERT_TRUE(caught_);            \
    } while (0)

namespace {

// Binary PPM (P6), rows of RGB triplets.
std::string ppm(int w, int h, const std::vector<std::uint8_t>& rgb) {
    std::string s = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    s.append(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    return s;
}

// 1x2 image: top row red, bottom row green.
std::string red_over_green() { return ppm(1, 2, {255, 0, 0, 0, 255, 0}); }

}  // namespace

UTEST(libstb_image, info_reads_header) {
    const std::string d = red_over_green();
    libstb::image_info i = libstb::info(d.data(), d.size());
    ASSERT_EQ(1, i.width);
    ASSERT_EQ(2, i.height);
    ASSERT_EQ(3, i.channels);
}

UTEST(libstb_image, load_keeps_source_channels) {
    const std::string d = red_over_green();
    libstb::image img = libstb::load(d.data(), d.size());
    ASSERT_EQ(1, img.width);
    ASSERT_EQ(2, img.height);
    ASSERT_EQ(3, img.channels);
    ASSERT_EQ(6u, img.data.size());
    ASSERT_EQ(255, img.data[0]);  // top-left R
    ASSERT_EQ(255, img.data[4]);  // bottom G
}

UTEST(libstb_image, load_converts_to_rgba_with_opaque_alpha) {
    const std::string d = red_over_green();
    libstb::load_options o;
    o.channels = 4;
    libstb::image img = libstb::load(d.data(), d.size(), o);
    ASSERT_EQ(4, img.channels);
    ASSERT_EQ(8u, img.data.size());
    ASSERT_EQ(255, img.data[3]);
    ASSERT_EQ(255, img.data[7]);
}

UTEST(libstb_image, load_converts_to_gray) {
    const std::string d = red_over_green();
    libstb::load_options o;
    o.channels = 1;
    libstb::image img = libstb::load(d.data(), d.size(), o);
    ASSERT_EQ(1, img.channels);
    ASSERT_EQ(2u, img.data.size());
    ASSERT_TRUE(img.data[0] > 0 && img.data[0] < 255);
}

UTEST(libstb_image, flip_swaps_rows) {
    const std::string d = red_over_green();
    libstb::load_options o;
    o.flip = true;
    libstb::image img = libstb::load(d.data(), d.size(), o);
    ASSERT_EQ(0, img.data[0]);    // now green on top
    ASSERT_EQ(255, img.data[1]);
    ASSERT_EQ(255, img.data[3]);  // red at the bottom
}

UTEST(libstb_image, garbage_throws_runtime_error) {
    const std::string d = "definitely not an image";
    ASSERT_THROWS(libstb::load(d.data(), d.size()), std::runtime_error);
    ASSERT_THROWS(libstb::info(d.data(), d.size()), std::runtime_error);
}

UTEST(libstb_image, truncated_pixels_throw) {
    std::string d = red_over_green();
    d.resize(d.size() - 3);  // header promises 6 bytes, only 3 present
    ASSERT_THROWS(libstb::load(d.data(), d.size()), std::runtime_error);
}

UTEST(libstb_image, bad_arguments_throw_invalid_argument) {
    const std::string d = red_over_green();
    libstb::load_options o;
    o.channels = 5;
    ASSERT_THROWS(libstb::load(d.data(), d.size(), o), std::invalid_argument);
    o.channels = -1;
    ASSERT_THROWS(libstb::load(d.data(), d.size(), o), std::invalid_argument);
    ASSERT_THROWS(libstb::load(nullptr, 10), std::invalid_argument);
    ASSERT_THROWS(libstb::load(d.data(), 0), std::invalid_argument);
}

UTEST(libstb_image, max_bytes_is_enforced_from_header) {
    const std::string d = red_over_green();  // decodes to 6 bytes
    libstb::load_options o;
    o.max_bytes = 5;
    ASSERT_THROWS(libstb::load(d.data(), d.size(), o), std::runtime_error);
    o.max_bytes = 6;
    libstb::image img = libstb::load(d.data(), d.size(), o);
    ASSERT_EQ(6u, img.data.size());
}

UTEST(libstb_image, huge_declared_dimensions_rejected_without_allocating) {
    // Header claims 60000x60000 RGB (~10 GB) but carries no pixel data at all.
    const std::string d = "P6\n60000 60000\n255\n";
    ASSERT_THROWS(libstb::load(d.data(), d.size()), std::runtime_error);
}

UTEST(libstb_image, flip_is_per_call_and_thread_safe) {
    const std::string d = red_over_green();
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 8; ++t) {
        ts.emplace_back([&, t] {
            for (int i = 0; i < 2000; ++i) {
                libstb::load_options o;
                o.flip = ((i + t) % 2) == 1;
                libstb::image img = libstb::load(d.data(), d.size(), o);
                const bool top_is_red = img.data[0] == 255;
                if (top_is_red == o.flip) ++bad;  // flipped => green on top
            }
        });
    }
    for (auto& th : ts) th.join();
    ASSERT_EQ(0, bad.load());
}
