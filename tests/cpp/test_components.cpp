#include <cstdint>
#include <stdexcept>
#include <vector>

#include "stb.h"
#include "test_common.hpp"

// Connected components. The numbering and the statistics of small masks are worked out by hand; random masks are
// compared with a flood fill written here.

namespace {

using stb::components;
using stb::connectivity;
using stb::mat32i;
using stb::mat8u;

mat8u mask_of(int w, int h, const std::vector<int>& v) {
    mat8u m(w, h, 1);
    for (std::size_t i = 0; i < v.size(); ++i) m.data()[i] = std::uint8_t(v[i] ? 255 : 0);
    return m;
}

mat32i map_of(int w, int h, const std::vector<std::int32_t>& v) {
    mat32i m(w, h, 1);
    for (std::size_t i = 0; i < v.size(); ++i) m.data()[i] = v[i];
    return m;
}

// The pieces by a flood fill, numbered in the order of their first value in row-major reading.
std::vector<int> flood(int w, int h, const std::vector<int>& group, bool eight, int& count) {
    std::vector<int> out(group.size(), 0);
    count = 0;
    for (int s = 0; s < w * h; ++s) {
        if (group[std::size_t(s)] < 0 || out[std::size_t(s)] != 0) continue;
        ++count;
        std::vector<int> stack{s};
        out[std::size_t(s)] = count;
        while (!stack.empty()) {
            const int p = stack.back();
            stack.pop_back();
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    if ((ox == 0 && oy == 0) || (!eight && ox != 0 && oy != 0)) continue;
                    const int x = p % w + ox, y = p / w + oy;
                    if (x < 0 || y < 0 || x >= w || y >= h) continue;
                    const int q = y * w + x;
                    if (group[std::size_t(q)] == group[std::size_t(p)] && out[std::size_t(q)] == 0) {
                        out[std::size_t(q)] = count;
                        stack.push_back(q);
                    }
                }
        }
    }
    return out;
}

std::vector<int> labels_of(const components& c) {
    std::vector<int> v;
    for (std::int32_t l : c.labels) v.push_back(int(l));
    return v;
}

// 5 x 4:   1 1 0 0 1
//          0 1 0 0 1
//          0 0 0 1 0
//          1 0 0 1 1
const std::vector<int> picture = {1, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1};

}  // namespace

UTEST(libstb_components_tests, test_the_pieces_are_numbered_by_their_first_value_in_reading_order) {
    const components c = stb::connected_components(mask_of(5, 4, picture));
    EXPECT_EQ(c.count, 4);
    EXPECT_TRUE((labels_of(c) == std::vector<int>{1, 1, 0, 0, 2, 0, 1, 0, 0, 2, 0, 0, 0, 3, 0, 4, 0, 0, 3, 3}));
    EXPECT_EQ(c.labels.width(), 5);
    EXPECT_EQ(c.labels.height(), 4);
    EXPECT_EQ(c.labels.channels(), 1);
}

UTEST(libstb_components_tests, test_the_statistics_of_each_piece) {
    const components c = stb::connected_components(mask_of(5, 4, picture));
    ASSERT_EQ(c.stats.size(), std::size_t(4));
    const stb::component_stat& a = c.stats[0];  // {(0,0), (1,0), (1,1)}
    EXPECT_EQ(a.area, 3);
    EXPECT_EQ(a.x, 0);
    EXPECT_EQ(a.y, 0);
    EXPECT_EQ(a.width, 2);
    EXPECT_EQ(a.height, 2);
    EXPECT_NEAR(a.cx, 2.0f / 3.0f, 1e-6f);
    EXPECT_NEAR(a.cy, 1.0f / 3.0f, 1e-6f);
    const stb::component_stat& c3 = c.stats[2];  // {(3,2), (3,3), (4,3)}
    EXPECT_EQ(c3.area, 3);
    EXPECT_EQ(c3.x, 3);
    EXPECT_EQ(c3.y, 2);
    EXPECT_EQ(c3.width, 2);
    EXPECT_EQ(c3.height, 2);
    EXPECT_NEAR(c3.cx, 10.0f / 3.0f, 1e-6f);
    EXPECT_NEAR(c3.cy, 8.0f / 3.0f, 1e-6f);
    EXPECT_EQ(c.stats[3].area, 1);  // {(0,3)}
    EXPECT_EQ(c.stats[3].width, 1);
}

UTEST(libstb_components_tests, test_with_eight_neighbours_a_corner_joins_pieces) {
    // (4,1) and (3,2) touch only by a corner: the second and third piece become one
    const components c = stb::connected_components(mask_of(5, 4, picture), connectivity::eight);
    EXPECT_EQ(c.count, 3);
    EXPECT_TRUE((labels_of(c) == std::vector<int>{1, 1, 0, 0, 2, 0, 1, 0, 0, 2, 0, 0, 0, 2, 0, 3, 0, 0, 2, 2}));
    EXPECT_EQ(c.stats[1].area, 5);
    EXPECT_EQ(c.stats[1].x, 3);
    EXPECT_EQ(c.stats[1].y, 0);
    EXPECT_EQ(c.stats[1].width, 2);
    EXPECT_EQ(c.stats[1].height, 4);
}

UTEST(libstb_components_tests, test_random_masks_match_a_flood_fill_for_both_connectivities) {
    std::uint32_t s = 77u;
    for (int round = 0; round < 12; ++round) {
        const int w = 3 + round * 3, h = 4 + round * 2;
        std::vector<int> v(std::size_t(w * h));
        for (int& x : v) {
            s = s * 1664525u + 1013904223u;
            x = ((s >> 24) % 100) < 52 ? 0 : 1;
        }
        std::vector<int> group(v.size());
        for (std::size_t i = 0; i < v.size(); ++i) group[i] = v[i] ? 0 : -1;
        for (bool eight : {false, true}) {
            int count = 0;
            const std::vector<int> want = flood(w, h, group, eight, count);
            const components c = stb::connected_components(mask_of(w, h, v), eight ? connectivity::eight : connectivity::four);
            ASSERT_EQ(c.count, count);
            ASSERT_TRUE(labels_of(c) == want);
            ASSERT_EQ(int(c.stats.size()), count);
            int total = 0;
            for (const stb::component_stat& st : c.stats) total += st.area;
            int foreground = 0;
            for (int x : v) foreground += x;
            ASSERT_EQ(total, foreground);
        }
    }
}

UTEST(libstb_components_tests, test_a_map_of_labels_falls_apart_into_pieces_of_equal_label) {
    //  1 1 2 1
    //  1 2 2 1
    const components c = stb::connected_components(map_of(4, 2, {1, 1, 2, 1, 1, 2, 2, 1}));
    EXPECT_EQ(c.count, 3);
    EXPECT_TRUE((labels_of(c) == std::vector<int>{1, 1, 2, 3, 1, 2, 2, 3}));
    EXPECT_EQ(c.stats[0].area, 3);
    EXPECT_EQ(c.stats[1].area, 3);
    EXPECT_EQ(c.stats[2].area, 2);
}

UTEST(libstb_components_tests, test_a_negative_label_is_the_background) {
    //  -1  5  5
    //   5 -1  5      with four neighbours the value at (0, 1) stands alone
    const components c = stb::connected_components(map_of(3, 2, {-1, 5, 5, 5, -1, 5}));
    EXPECT_EQ(c.count, 2);
    EXPECT_TRUE((labels_of(c) == std::vector<int>{0, 1, 1, 2, 0, 1}));
    EXPECT_EQ(stb::connected_components(map_of(3, 2, {-1, 5, 5, 5, -1, 5}), connectivity::eight).count, 1);
}

UTEST(libstb_components_tests, test_map_pieces_match_a_flood_fill_on_random_maps) {
    std::uint32_t s = 5u;
    for (int round = 0; round < 8; ++round) {
        const int w = 6 + round * 4, h = 5 + round * 3;
        std::vector<std::int32_t> v(std::size_t(w * h));
        std::vector<int> group(v.size());
        for (std::size_t i = 0; i < v.size(); ++i) {
            s = s * 1664525u + 1013904223u;
            v[i] = std::int32_t((s >> 24) % 4) - 1;  // -1, 0, 1, 2
            group[i] = int(v[i]);
        }
        for (bool eight : {false, true}) {
            int count = 0;
            const std::vector<int> want = flood(w, h, group, eight, count);
            const components c = stb::connected_components(map_of(w, h, v), eight ? connectivity::eight : connectivity::four);
            ASSERT_EQ(c.count, count);
            ASSERT_TRUE(labels_of(c) == want);
        }
    }
}

UTEST(libstb_components_tests, test_one_channel_is_required_and_an_empty_input_has_no_pieces) {
    ASSERT_THROWS(stb::connected_components(mat8u(3, 3, 3)), std::invalid_argument);
    ASSERT_THROWS(stb::connected_components(mat32i(3, 3, 2)), std::invalid_argument);
    const components none = stb::connected_components(mat8u());
    EXPECT_EQ(none.count, 0);
    EXPECT_TRUE(none.labels.empty());
    const components blank = stb::connected_components(mask_of(4, 3, std::vector<int>(12, 0)));
    EXPECT_EQ(blank.count, 0);
    EXPECT_TRUE(blank.stats.empty());
    const components full = stb::connected_components(mask_of(4, 3, std::vector<int>(12, 1)));
    EXPECT_EQ(full.count, 1);
    EXPECT_EQ(full.stats[0].area, 12);
}
