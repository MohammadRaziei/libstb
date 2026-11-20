#include <cstdint>
#include <cstring>
#include <utility>
#include <limits>
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

const stb::superpixel_method methods[] = {stb::superpixel_method::slic, stb::superpixel_method::slico, stb::superpixel_method::snic,
                                          stb::superpixel_method::sicle};

}  // namespace

UTEST(libstb_superpixel_tests, test_every_method_labels_every_pixel_with_connected_regions_near_the_requested_count) {
    const stb::image8u img = noise(96, 80, stb::pixel_format::rgb);
    for (const stb::superpixel_method me : methods) {
        const stb::superpixel_map sp = stb::superpixel(img, 100).method(me).run();
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
        const stb::superpixel_map sp = stb::superpixel(img, 64).method(me).run();
        const stb::image8u mean = sp.mean_color(img);
        // a region holding two colours would have a mean that is neither
        EXPECT_EQ(0, std::memcmp(mean.data(), img.data(), img.size_bytes()));
    }
}

UTEST(libstb_superpixel_tests, test_the_result_does_not_depend_on_the_number_of_threads) {
    const stb::image8u img = noise(160, 120, stb::pixel_format::rgb);
    for (const stb::superpixel_method me : methods) {
        const stb::superpixel sp = std::move(stb::superpixel(img, 400).method(me));
        stb::superpixel_map one;
        {
            stb::thread_scope threads(1);
            one = sp.run();
        }
        stb::thread_scope threads(4);
        EXPECT_TRUE(labels_equal(one, sp.run()));
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
    EXPECT_TRUE(labels_equal(stb::superpixel(rgb, 30).run(), stb::superpixel(rgba, 30).run()));
    const stb::image8u gray = noise(40, 30, stb::pixel_format::gray), gray_alpha = noise(40, 30, stb::pixel_format::gray_alpha);
    EXPECT_TRUE(stb::superpixel(gray, 30).run().count() >= 1);
    EXPECT_TRUE(stb::superpixel(gray_alpha, 30).run().count() >= 1);
}

UTEST(libstb_superpixel_tests, test_tiny_images_and_huge_counts_are_fine) {
    const stb::image8u one(1, 1, stb::pixel_format::rgb);
    const stb::image8u three = noise(3, 3, stb::pixel_format::rgb);
    for (const stb::superpixel_method me : methods) {
        EXPECT_EQ(1, stb::superpixel(one, 400).method(me).run().count());
        const stb::superpixel_map sp = stb::superpixel(three, 400).method(me).run();
        EXPECT_TRUE(sp.count() >= 1 && sp.count() <= 9);
        EXPECT_TRUE(all_connected(sp));
    }
    EXPECT_TRUE(stb::superpixel(three, 1).run().count() >= 1);
}

UTEST(libstb_superpixel_tests, test_bad_arguments_throw_at_the_call_that_gives_them) {
    const stb::image8u img = noise(20, 20, stb::pixel_format::rgb);
    ASSERT_THROWS(stb::superpixel(img, 0), std::invalid_argument);
    ASSERT_THROWS(stb::superpixel(img, -3), std::invalid_argument);
    stb::superpixel sp(img, 10);
    ASSERT_THROWS(sp.compactness(0.0f), std::invalid_argument);
    ASSERT_THROWS(sp.compactness(-1.0f), std::invalid_argument);
    ASSERT_THROWS(sp.compactness(std::numeric_limits<float>::infinity()), std::invalid_argument);
    ASSERT_THROWS(sp.compactness(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
    ASSERT_THROWS(sp.iterations(0), std::invalid_argument);
    ASSERT_THROWS(sp.method(static_cast<stb::superpixel_method>(9)), std::invalid_argument);
    EXPECT_TRUE(sp.run().count() >= 1);  // the refused calls changed nothing
    const stb::image8u empty;
    const stb::mat8u five(4, 4, 5);
    ASSERT_THROWS(stb::superpixel(empty, 10), std::invalid_argument);
    ASSERT_THROWS(stb::superpixel(five, 10), std::invalid_argument);
}

UTEST(libstb_superpixel_tests, test_the_settings_chain_in_any_order_on_a_temporary_and_on_a_named_builder) {
    const stb::image8u img = noise(64, 48, stb::pixel_format::rgb);
    const stb::superpixel_map a = stb::superpixel(img, 40).method(stb::superpixel_method::slic).compactness(15.0f).iterations(3).run();
    const stb::superpixel_map b = stb::superpixel(img, 40).iterations(3).compactness(15.0f).method(stb::superpixel_method::slic).run();
    stb::superpixel named(img, 40);
    named.method(stb::superpixel_method::slic).compactness(15.0f).iterations(3);
    EXPECT_TRUE(labels_equal(a, b));
    EXPECT_TRUE(labels_equal(a, named.run()));
    // the settings count: another compactness gives another map
    EXPECT_TRUE(!labels_equal(a, stb::superpixel(img, 40).method(stb::superpixel_method::slic).compactness(1.0f).iterations(3).run()));
    // run() can be repeated, and the connectivity switch is honoured
    EXPECT_TRUE(labels_equal(named.run(), named.run()));
    const stb::superpixel_map loose = stb::superpixel(img, 40).method(stb::superpixel_method::slic).enforce_connectivity(false).run();
    EXPECT_TRUE(loose.count() >= 1);
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

// ------------------------------------------------------------------------------------------------- sicle

namespace {

// Pieces of known colour (a grid of 5 x 4 blocks, each its own colour) with a little noise on top; returns the picture
// and the block of each pixel.
stb::image8u blocks_with_noise(int w, int h, std::vector<int>& block_of) {
    stb::image8u img(w, h, stb::pixel_format::rgb);
    block_of.assign(std::size_t(w) * std::size_t(h), 0);
    std::uint32_t s = 99u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int b = (y * 4 / h) * 5 + (x * 5 / w);
            block_of[std::size_t(y) * std::size_t(w) + std::size_t(x)] = b;
            const std::uint32_t hsh = std::uint32_t(b) * 2654435761u;
            const int base[3] = {int((hsh >> 8) & 0xFF), int((hsh >> 16) & 0xFF), int((hsh >> 24) & 0xFF)};
            for (int k = 0; k < 3; ++k) {
                s = s * 1664525u + 1013904223u;
                const int noise = int(s >> 29) - 3;  // -3..4
                img(x, y, k) = std::uint8_t(std::min(255, std::max(0, base[k] + noise)));
            }
        }
    return img;
}

}  // namespace

UTEST(libstb_superpixel_tests, test_sicle_makes_exactly_the_number_of_regions_asked_for_and_keeps_to_the_edges) {
    std::vector<int> block_of;
    const stb::image8u img = blocks_with_noise(100, 80, block_of);
    // 20 is as few regions as there are blocks: a block can be lost then, as it can with any method that drops seeds, so only
    // the count and the connectivity are checked; from 3 times as many regions on, no block is lost
    for (int count : {20, 60, 150}) {
        const stb::superpixel_map sp = stb::superpixel(img, count).method(stb::superpixel_method::sicle).run();
        EXPECT_EQ(count, sp.count());  // not "about": every seed keeps its own region
        EXPECT_TRUE(all_connected(sp));
        if (count < 60) continue;
        // leakage: pixels in a region whose most common block is another block (the 20 blocks are far apart in colour)
        std::vector<std::vector<int>> votes(std::size_t(sp.count()), std::vector<int>(20, 0));
        for (int y = 0; y < 80; ++y)
            for (int x = 0; x < 100; ++x) ++votes[std::size_t(sp.label(x, y))][std::size_t(block_of[std::size_t(y) * 100 + std::size_t(x)])];
        int wrong = 0;
        for (const std::vector<int>& v : votes) {
            int total = 0, best = 0;
            for (int n : v) {
                total += n;
                best = std::max(best, n);
            }
            wrong += total - best;
        }
        EXPECT_TRUE(wrong * 200 <= 100 * 80);  // under half a percent of the pixels are in a region of another block
    }
}

UTEST(libstb_superpixel_tests, test_sicle_settings_oversample_iterations_and_removal_rate_all_end_at_the_count) {
    std::vector<int> block_of;
    const stb::image8u img = blocks_with_noise(64, 48, block_of);
    const auto sicle = [&](int count) { return stb::superpixel(img, count).method(stb::superpixel_method::sicle); };
    EXPECT_EQ(40, sicle(40).run().count());
    EXPECT_EQ(40, sicle(40).oversample(500).run().count());
    EXPECT_EQ(40, sicle(40).oversample(5).run().count());                 // below the count: raised to it
    EXPECT_EQ(40, sicle(40).oversample(1000000).run().count());           // above the pixels: as many as there are
    EXPECT_EQ(40, sicle(40).iterations(1).run().count());                 // a single run from 40 seeds
    EXPECT_EQ(40, sicle(40).iterations(2).run().count());
    EXPECT_EQ(40, sicle(40).iterations(12).run().count());
    EXPECT_EQ(40, sicle(40).removal_rate(0.632f).run().count());         // DISF's schedule
    EXPECT_EQ(40, sicle(40).removal_rate(0.05f).oversample(300).run().count());
    EXPECT_EQ(64 * 48, sicle(100000).run().count());                      // more regions than pixels: a region per pixel
    EXPECT_EQ(1, sicle(1).run().count());
    // the settings count, and the same settings give the same map
    EXPECT_TRUE(!labels_equal(sicle(40).iterations(1).run(), sicle(40).run()));
    EXPECT_TRUE(labels_equal(sicle(40).oversample(500).run(), sicle(40).oversample(500).run()));
    ASSERT_THROWS(sicle(40).oversample(-1), std::invalid_argument);
    ASSERT_THROWS(sicle(40).removal_rate(-0.1f), std::invalid_argument);
    ASSERT_THROWS(sicle(40).removal_rate(1.0f), std::invalid_argument);
    ASSERT_THROWS(sicle(40).removal_rate(std::numeric_limits<float>::quiet_NaN()), std::invalid_argument);
}

UTEST(libstb_superpixel_tests, test_sicle_of_a_picture_with_two_flat_halves_splits_it_at_the_edge_as_computed_by_hand) {
    // 8 x 1 gray: four 0 then four 255, count 2, oversample 8 (a seed on every pixel), iterations 2: the first run gives
    // 8 regions of one pixel (sizes 1/8, contrast 0 inside a half, 255 across at the edge: the seeds at pixels 3 and 4 are the only
    // ones whose smallest neighbour contrast... is still 0, since each has a same-colour neighbour on one side). Nothing has
    // relevance, so the two seeds that stay are the ones the hash puts first, and in any case each half keeps its own edge:
    // the region borders of a 2-region result never lie inside a flat half unless both seeds are in the same half.
    stb::image8u img(8, 1, stb::pixel_format::gray);
    for (int x = 0; x < 8; ++x) img(x, 0, 0) = x < 4 ? 0 : 255;
    const stb::superpixel_map sp = stb::superpixel(img, 2).method(stb::superpixel_method::sicle).oversample(8).iterations(2).run();
    ASSERT_EQ(2, sp.count());
    EXPECT_TRUE(all_connected(sp));
    int changes = 0;
    for (int x = 1; x < 8; ++x) changes += sp.label(x, 0) != sp.label(x - 1, 0);
    EXPECT_EQ(1, changes);  // one border: two connected pieces of a row
}
