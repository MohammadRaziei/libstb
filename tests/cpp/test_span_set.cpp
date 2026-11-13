#include <climits>
#include <cstdint>
#include <vector>

#include "stb/raster.hpp"
#include "test_common.hpp"

namespace {

using coverage_map = std::vector<int>;  // width * height, 0 where nothing is covered

coverage_map paint(const stb::span_set& set, int w, int h) {
    coverage_map map(std::size_t(w) * std::size_t(h), 0);
    for (const stb::span& s : set.spans())
        for (int x = s.x; x < s.x + s.len; ++x) {
            if (x < 0 || x >= w || s.y < 0 || s.y >= h) continue;  // outside the map: left out (the tests compare maps)
            map[std::size_t(s.y) * std::size_t(w) + std::size_t(x)] = s.coverage;
        }
    return map;
}

// a set from a map of coverages: every run of equal non-zero values is a span
stb::span_set from_map(const coverage_map& map, int w, int h) {
    stb::span_set set;
    for (int y = 0; y < h; ++y) {
        int x = 0;
        while (x < w) {
            const int c = map[std::size_t(y) * std::size_t(w) + std::size_t(x)];
            if (c == 0) {
                ++x;
                continue;
            }
            int len = 1;
            while (x + len < w && map[std::size_t(y) * std::size_t(w) + std::size_t(x + len)] == c) ++len;
            const stb::span s{y, x, len, std::uint8_t(c)};
            set(stb::span_view{&s, 1});
            x += len;
        }
    }
    return set;
}

}  // namespace

UTEST(libstb_span_set_tests, test_a_rectangle_is_one_full_span_per_row) {
    const stb::span_set r = stb::span_set::from_rect(3, 5, 4, 2);
    ASSERT_EQ(2u, r.spans().size);
    ASSERT_EQ(5, r.spans().data[0].y);
    ASSERT_EQ(3, r.spans().data[0].x);
    ASSERT_EQ(4, r.spans().data[0].len);
    ASSERT_EQ(255, int(r.spans().data[0].coverage));
    ASSERT_EQ(6, r.spans().data[1].y);
    ASSERT_TRUE(stb::span_set::from_rect(0, 0, 0, 5).empty());
    ASSERT_TRUE(stb::span_set::from_rect(0, 0, 5, -1).empty());
    ASSERT_THROWS(stb::span_set::from_rect(INT_MAX, 0, 2, 1), std::invalid_argument);
}

UTEST(libstb_span_set_tests, test_extents_are_the_true_box) {
    ASSERT_FALSE(stb::span_set().extents().has_value());
    const auto e = stb::span_set::from_rect(3, 5, 4, 2).extents();
    ASSERT_TRUE(e.has_value());
    ASSERT_EQ(3, e->x);
    ASSERT_EQ(5, e->y);
    ASSERT_EQ(4, e->w);
    ASSERT_EQ(2, e->h);
    // entirely at negative x (the clip of a rasterizer can be anywhere): the width is right
    const auto n = stb::span_set::from_rect(-10, -3, 4, 1).extents();
    ASSERT_EQ(-10, n->x);
    ASSERT_EQ(-3, n->y);
    ASSERT_EQ(4, n->w);
    ASSERT_EQ(1, n->h);
    // a ragged set: the box covers every row's span
    stb::span_set s;
    const stb::span spans[] = {{0, 2, 3, 255}, {0, 9, 1, 10}, {2, -4, 2, 7}, {4, 5, 6, 255}};
    s(stb::span_view{spans, 4});
    const auto r = s.extents();
    ASSERT_EQ(-4, r->x);
    ASSERT_EQ(0, r->y);
    ASSERT_EQ(15, r->w);  // -4 .. 11
    ASSERT_EQ(5, r->h);
    s.clear();
    ASSERT_TRUE(s.empty());
    ASSERT_FALSE(s.extents().has_value());
}

UTEST(libstb_span_set_tests, test_contains_looks_at_the_edges_of_every_span) {
    stb::span_set s;
    const stb::span spans[] = {{1, 2, 3, 255}, {1, 7, 1, 40}, {3, 0, 2, 9}};
    s(stb::span_view{spans, 3});
    for (int y = -1; y < 5; ++y)
        for (int x = -2; x < 10; ++x) {
            bool want = false;
            for (const stb::span& sp : spans) want = want || (sp.y == y && x >= sp.x && x < sp.x + sp.len);
            ASSERT_EQ(want, s.contains(x, y));
        }
    ASSERT_FALSE(stb::span_set().contains(0, 0));
}

UTEST(libstb_span_set_tests, test_it_collects_what_a_rasterizer_reports_in_batches) {
    stb::outline circle;
    circle.add_ellipse(20.f, 16.f, 12.f, 9.f);
    const stb::clip_rect area{0, 0, 40, 32};
    stb::span_set set;
    stb::rasterize_fill(circle, stb::fill_rule::non_zero, {}, area, stb::span_sink(set));
    ASSERT_FALSE(set.empty());
    std::size_t pixels = 0;
    stb::rasterize_fill(circle, stb::fill_rule::non_zero, {}, area, [&](stb::span_view v) {
        for (const stb::span& s : v) pixels += std::size_t(s.len);
    });
    std::size_t collected = 0;
    for (const stb::span& s : set.spans()) collected += std::size_t(s.len);
    ASSERT_EQ(pixels, collected);
    const auto e = set.extents();
    ASSERT_TRUE(e->x >= 8 && e->x <= 9 && e->w >= 23 && e->w <= 26);
}

UTEST(libstb_span_set_tests, test_spans_out_of_order_or_empty_are_refused) {
    stb::span_set s;
    const stb::span first{2, 5, 3, 255};
    s(stb::span_view{&first, 1});
    for (const stb::span& bad : {stb::span{2, 6, 2, 255},      // overlaps the last span
                                 stb::span{1, 0, 1, 255},      // a row above
                                 stb::span{2, 0, 1, 255},      // left of the last span
                                 stb::span{3, 0, 0, 255},      // empty
                                 stb::span{3, 0, -1, 255},
                                 stb::span{3, 0, 1, 0},        // no coverage
                                 stb::span{3, INT_MAX, 2, 1}}) {  // the end does not fit
        ASSERT_THROWS(s(stb::span_view{&bad, 1}), std::invalid_argument);
    }
    ASSERT_EQ(1u, s.spans().size);  // nothing of a refused span stays
    const stb::span touching{2, 8, 1, 3};  // starts where the last one ends: fine
    s(stb::span_view{&touching, 1});
    ASSERT_EQ(2u, s.spans().size);
}

UTEST(libstb_span_set_tests, test_intersect_multiplies_the_coverages) {
    stb::span_set a, b;
    const stb::span sa[] = {{0, 0, 10, 255}, {1, 2, 4, 128}, {2, 0, 3, 255}, {3, 0, 4, 2}};
    const stb::span sb[] = {{0, 4, 10, 128}, {1, 0, 4, 128}, {2, 3, 3, 255}, {3, 1, 2, 1}, {5, 0, 5, 255}};
    a(stb::span_view{sa, 4});
    b(stb::span_view{sb, 5});
    const stb::span_set c = stb::span_set::intersect(a, b);
    const coverage_map got = paint(c, 16, 6);
    ASSERT_EQ(128, got[0 * 16 + 4]);   // 255 * 128 / 255
    ASSERT_EQ(128, got[0 * 16 + 9]);
    ASSERT_EQ(0, got[0 * 16 + 10]);
    ASSERT_EQ(64, got[1 * 16 + 2]);    // 128 * 128 / 255 = 64.25 rounded down
    ASSERT_EQ(64, got[1 * 16 + 3]);
    ASSERT_EQ(0, got[1 * 16 + 4]);
    ASSERT_EQ(0, got[2 * 16 + 2]);     // touching only: a ends at 3, b starts at 3
    ASSERT_EQ(0, got[3 * 16 + 1]);     // 2 * 1 / 255 = 0: left out
    ASSERT_TRUE(stb::span_set::intersect(a, stb::span_set()).empty());
    ASSERT_TRUE(stb::span_set::intersect(stb::span_set(), b).empty());
    for (const stb::span& s : c.spans()) ASSERT_TRUE(s.len >= 1 && s.coverage >= 1);
}

UTEST(libstb_span_set_tests, test_intersect_equals_the_pixelwise_product_on_random_sets) {
    const int w = 23, h = 17;
    std::uint32_t state = 99;
    const auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    for (int round = 0; round < 60; ++round) {
        coverage_map ma(std::size_t(w * h)), mb(std::size_t(w * h));
        for (auto* m : {&ma, &mb})
            for (int& c : *m) {
                const std::uint32_t r = next();
                c = r % 3 == 0 ? 0 : (r % 5 == 0 ? 255 : int(1 + (r >> 4) % 255));
                // runs: repeat the previous value now and then
            }
        for (std::size_t i = 1; i < ma.size(); ++i) {
            if (next() % 3 == 0) ma[i] = ma[i - 1];
            if (next() % 3 == 0) mb[i] = mb[i - 1];
        }
        const stb::span_set a = from_map(ma, w, h), b = from_map(mb, w, h);
        const coverage_map got = paint(stb::span_set::intersect(a, b), w, h);
        for (std::size_t i = 0; i < got.size(); ++i) ASSERT_EQ((ma[i] * mb[i]) / 255, got[i]);
        const coverage_map back = paint(stb::span_set::intersect(b, a), w, h);  // symmetric
        ASSERT_TRUE(back == got);
    }
}

UTEST(libstb_span_set_tests, test_a_shape_clipped_by_a_rectangle_is_the_shape_filled_inside_that_rectangle) {
    stb::outline blob;
    blob.add_ellipse(30.f, 20.f, 22.f, 14.f);
    const stb::clip_rect all{0, 0, 64, 48};
    const stb::clip_rect box{12, 9, 27, 19};
    stb::span_set whole, clipped;
    stb::rasterize_fill(blob, stb::fill_rule::non_zero, {}, all, stb::span_sink(whole));
    stb::rasterize_fill(blob, stb::fill_rule::non_zero, {}, box, stb::span_sink(clipped));
    const stb::span_set got = stb::span_set::intersect(whole, stb::span_set::from_rect(box.x, box.y, box.w, box.h));
    ASSERT_TRUE(paint(got, 64, 48) == paint(clipped, 64, 48));
}
