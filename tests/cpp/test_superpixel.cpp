#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "stb.h"
#include "test_common.hpp"

namespace {

// Four flat colours in the four quadrants: regions must not cross the borders between them.
stb::image8u quadrants(int w, int h) {
    stb::image8u img(w, h, stb::pixel_format::rgb);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const bool right = x >= w / 2, low = y >= h / 2;
            const std::uint8_t c[4][3] = {{200, 30, 30}, {30, 200, 30}, {30, 30, 200}, {220, 220, 40}};
            const std::uint8_t* p = c[(low ? 2 : 0) + (right ? 1 : 0)];
            for (int k = 0; k < 3; ++k) img(x, y, k) = p[k];
        }
    return img;
}

stb::image8u noise(int w, int h, stb::pixel_format f) {
    stb::image8u m(w, h, f);
    std::uint32_t s = 4242u;
    for (std::uint8_t& v : m) {
        s = s * 1664525u + 1013904223u;
        v = std::uint8_t(s >> 24);
    }
    return m;
}

// every label is one 4-connected piece
bool all_connected(const stb::superpixel_map& m) {
    const int w = m.width(), h = m.height();
    std::vector<char> seen(std::size_t(w) * std::size_t(h), 0);
    std::vector<int> pieces_of(std::size_t(m.count()), 0);
    std::vector<std::pair<int, int>> stack;
    for (int y0 = 0; y0 < h; ++y0)
        for (int x0 = 0; x0 < w; ++x0) {
            if (seen[std::size_t(y0) * std::size_t(w) + std::size_t(x0)]) continue;
            const std::int32_t l = m.label(x0, y0);
            if (++pieces_of[std::size_t(l)] > 1) return false;
            seen[std::size_t(y0) * std::size_t(w) + std::size_t(x0)] = 1;
            stack.push_back({x0, y0});
            while (!stack.empty()) {
                const auto [x, y] = stack.back();
                stack.pop_back();
                const int nx[4] = {x - 1, x + 1, x, x}, ny[4] = {y, y, y - 1, y + 1};
                for (int i = 0; i < 4; ++i) {
                    if (nx[i] < 0 || ny[i] < 0 || nx[i] >= w || ny[i] >= h) continue;
                    const std::size_t q = std::size_t(ny[i]) * std::size_t(w) + std::size_t(nx[i]);
                    if (!seen[q] && m.label(nx[i], ny[i]) == l) {
                        seen[q] = 1;
                        stack.push_back({nx[i], ny[i]});
                    }
                }
            }
        }
    return true;
}

bool labels_equal(const stb::superpixel_map& a, const stb::superpixel_map& b) {
    return a.width() == b.width() && a.height() == b.height() && a.count() == b.count() &&
           std::memcmp(a.labels().data(), b.labels().data(), a.labels().size_bytes()) == 0;
}

const stb::superpixel_method methods[] = {stb::superpixel_method::slic, stb::superpixel_method::slico, stb::superpixel_method::snic};

}  // namespace

UTEST(libstb_superpixel_tests, test_every_method_labels_every_pixel_with_connected_regions_near_the_requested_count) {
    const stb::image8u img = noise(96, 80, stb::pixel_format::rgb);
    for (const stb::superpixel_method me : methods) {
        stb::superpixel_options opt;
        opt.method = me;
        opt.count = 100;
        const stb::superpixel_map sp = stb::superpixels(img, opt);
        ASSERT_EQ(96, sp.width());
        ASSERT_EQ(80, sp.height());
        EXPECT_TRUE(sp.count() >= 50 && sp.count() <= 200);
        for (const std::int32_t l : sp.labels()) EXPECT_TRUE(l >= 0 && l < sp.count());
        EXPECT_TRUE(all_connected(sp));
    }
}

UTEST(libstb_superpixel_tests, test_regions_do_not_cross_the_borders_between_flat_colours) {
    const stb::image8u img = quadrants(80, 80);
    for (const stb::superpixel_method me : methods) {
        stb::superpixel_options opt;
        opt.method = me;
        opt.count = 64;
        const stb::superpixel_map sp = stb::superpixels(img, opt);
        const stb::image8u mean = sp.mean_color(img);
        // a region holding two colours would have a mean that is neither
        EXPECT_EQ(0, std::memcmp(mean.data(), img.data(), img.size_bytes()));
    }
}

UTEST(libstb_superpixel_tests, test_the_result_does_not_depend_on_the_number_of_threads) {
    const stb::image8u img = noise(160, 120, stb::pixel_format::rgb);
    for (const stb::superpixel_method me : methods) {
        stb::superpixel_options opt;
        opt.method = me;
        stb::superpixel_map one;
        {
            stb::thread_scope threads(1);
            one = stb::superpixels(img, opt);
        }
        stb::thread_scope threads(4);
        EXPECT_TRUE(labels_equal(one, stb::superpixels(img, opt)));
    }
}

UTEST(libstb_superpixel_tests, test_gray_and_alpha_images_work_and_alpha_is_ignored) {
    const stb::image8u rgb = noise(40, 30, stb::pixel_format::rgb);
    stb::image8u rgba(40, 30, stb::pixel_format::rgba);
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 40; ++x) {
            for (int k = 0; k < 3; ++k) rgba(x, y, k) = rgb(x, y, k);
            rgba(x, y, 3) = std::uint8_t(x * 6);  // changes with x; must not matter
        }
    stb::superpixel_options opt;
    opt.count = 30;
    EXPECT_TRUE(labels_equal(stb::superpixels(rgb, opt), stb::superpixels(rgba, opt)));
    EXPECT_TRUE(stb::superpixels(noise(40, 30, stb::pixel_format::gray), opt).count() >= 1);
    EXPECT_TRUE(stb::superpixels(noise(40, 30, stb::pixel_format::gray_alpha), opt).count() >= 1);
}

UTEST(libstb_superpixel_tests, test_tiny_images_and_huge_counts_are_fine) {
    const stb::image8u one(1, 1, stb::pixel_format::rgb);
    const stb::image8u three = noise(3, 3, stb::pixel_format::rgb);
    for (const stb::superpixel_method me : methods) {
        stb::superpixel_options opt;
        opt.method = me;
        opt.count = 400;
        EXPECT_EQ(1, stb::superpixels(one, opt).count());
        const stb::superpixel_map sp = stb::superpixels(three, opt);
        EXPECT_TRUE(sp.count() >= 1 && sp.count() <= 9);
        EXPECT_TRUE(all_connected(sp));
    }
    stb::superpixel_options single;
    single.count = 1;
    EXPECT_TRUE(stb::superpixels(three, single).count() >= 1);
}

UTEST(libstb_superpixel_tests, test_bad_arguments_throw) {
    const stb::image8u img = noise(20, 20, stb::pixel_format::rgb);
    stb::superpixel_options bad;
    bad.count = 0;
    ASSERT_THROWS(stb::superpixels(img, bad), std::invalid_argument);
    bad = {};
    bad.compactness = 0.0f;
    ASSERT_THROWS(stb::superpixels(img, bad), std::invalid_argument);
    bad = {};
    bad.iterations = 0;
    ASSERT_THROWS(stb::superpixels(img, bad), std::invalid_argument);
    ASSERT_THROWS(stb::superpixels(stb::image8u()), std::invalid_argument);
    ASSERT_THROWS(stb::superpixels(stb::mat8u(4, 4, 5)), std::invalid_argument);
}

UTEST(libstb_superpixel_tests, test_the_map_checks_its_labels_and_copies_explicitly) {
    stb::mat32i labels(2, 2, 1, std::vector<std::int32_t>{0, 1, 1, 2});
    const stb::superpixel_map ok(labels.copy(), 3);
    EXPECT_EQ(3, ok.count());
    EXPECT_EQ(2, ok.label(1, 1));
    ASSERT_THROWS(stb::superpixel_map(labels.copy(), 2), std::invalid_argument);  // label 2 is not below 2
    ASSERT_THROWS(stb::superpixel_map(labels.copy(), 0), std::invalid_argument);
    ASSERT_THROWS(stb::superpixel_map(stb::mat32i(2, 2, 3), 3), std::invalid_argument);  // three channels
    const stb::superpixel_map c = ok.copy();
    EXPECT_TRUE(c.labels().data() != ok.labels().data());
    EXPECT_TRUE(labels_equal(c, ok));
}

UTEST(libstb_superpixel_tests, test_boundaries_overlay_and_mean_color) {
    // two regions: the left column and the right column of a 2x2 image
    const stb::superpixel_map sp(stb::mat32i(2, 2, 1, std::vector<std::int32_t>{0, 1, 0, 1}), 2);
    const stb::image8u b = sp.boundaries(stb::color(10, 20, 30, 255));
    ASSERT_EQ(2, b.width());
    EXPECT_TRUE(b.format() == stb::pixel_format::rgba);
    EXPECT_EQ(10, int(b(0, 0, 0)));  // the left column ends a region
    EXPECT_EQ(255, int(b(0, 1, 3)));
    EXPECT_EQ(0, int(b(1, 0, 3)));   // the right column ends the image, not a region: transparent

    const stb::image8u gray(2, 2, stb::pixel_format::gray, std::vector<std::uint8_t>{10, 100, 30, 200});
    const stb::image8u ov = sp.overlay(gray, stb::color(255, 0, 0));
    EXPECT_TRUE(ov.format() == stb::pixel_format::rgb);  // gray becomes RGB so that the colour shows
    EXPECT_EQ(255, int(ov(0, 0, 0)));
    EXPECT_EQ(0, int(ov(0, 0, 1)));
    EXPECT_EQ(100, int(ov(1, 0, 0)));  // untouched pixels are the gray value

    const stb::image8u mean = sp.mean_color(gray);
    EXPECT_EQ(20, int(mean(0, 0)));  // (10 + 30) / 2
    EXPECT_EQ(20, int(mean(0, 1)));
    EXPECT_EQ(150, int(mean(1, 0)));  // (100 + 200) / 2
    ASSERT_THROWS(sp.mean_color(stb::image8u(3, 2, stb::pixel_format::gray)), std::invalid_argument);
    ASSERT_THROWS(sp.overlay(stb::image8u(2, 3, stb::pixel_format::gray)), std::invalid_argument);
}
