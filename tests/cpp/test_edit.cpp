#include <climits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "test_common.hpp"

using testutil::gradient;
using testutil::same;
using testutil::solid;

namespace {
stb::image make(int w, int h, int c, std::vector<std::uint8_t> px) { return stb::image(w, h, c, std::move(px)); }
}  // namespace

// ------------------------------------------------------------------- crop

UTEST(libstb_edit_tests, test_crop_returns_that_rectangle) {
    const stb::image src = gradient(7, 5, 3);
    const stb::image c = src.crop(2, 1, 4, 3);
    ASSERT_EQ(4, c.width());
    ASSERT_EQ(3, c.height());
    ASSERT_EQ(3, c.channels());
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x)
            for (int k = 0; k < 3; ++k) ASSERT_EQ(src(x + 2, y + 1, k), c(x, y, k));
}

UTEST(libstb_edit_tests, test_crop_of_the_whole_image_is_an_independent_copy) {
    const stb::image src = gradient(4, 3, 4);
    stb::image c = src.crop(0, 0, 4, 3);
    ASSERT_TRUE(same(src, c));
    ASSERT_TRUE(c.data() != src.data());
}

UTEST(libstb_edit_tests, test_crop_rejects_a_rectangle_outside_the_image) {
    const stb::image src = gradient(4, 3, 1);
    ASSERT_THROWS(src.crop(-1, 0, 2, 2), std::invalid_argument);
    ASSERT_THROWS(src.crop(0, -1, 2, 2), std::invalid_argument);
    ASSERT_THROWS(src.crop(3, 0, 2, 2), std::invalid_argument);
    ASSERT_THROWS(src.crop(0, 2, 2, 2), std::invalid_argument);
    ASSERT_THROWS(src.crop(0, 0, 0, 2), std::invalid_argument);
    ASSERT_THROWS(src.crop(0, 0, 2, 0), std::invalid_argument);
    ASSERT_THROWS(src.crop(INT_MAX, 0, INT_MAX, 1), std::invalid_argument);  // no int overflow
    ASSERT_THROWS(stb::image().crop(0, 0, 1, 1), std::invalid_argument);
}

// -------------------------------------------------- flips, turns, transpose

UTEST(libstb_edit_tests, test_flips_move_pixels_to_the_mirrored_position) {
    const stb::image src = gradient(5, 3, 3);
    const stb::image h = src.flip_horizontal(), v = src.flip_vertical();
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            for (int k = 0; k < 3; ++k) {
                ASSERT_EQ(src(4 - x, y, k), h(x, y, k));
                ASSERT_EQ(src(x, 2 - y, k), v(x, y, k));
            }
}

UTEST(libstb_edit_tests, test_rotate90_is_clockwise) {
    // 2 wide, 3 tall:  a b      clockwise ->  e c a
    //                  c d                    f d b
    //                  e f
    const stb::image src = make(2, 3, 1, {'a', 'b', 'c', 'd', 'e', 'f'});
    const stb::image r = src.rotate90();
    ASSERT_EQ(3, r.width());
    ASSERT_EQ(2, r.height());
    const char expect[] = {'e', 'c', 'a', 'f', 'd', 'b'};
    for (int i = 0; i < 6; ++i) ASSERT_EQ(expect[i], r.data()[i]);
}

UTEST(libstb_edit_tests, test_rotate90_turns_wrap_and_negative_means_counter_clockwise) {
    const stb::image src = gradient(5, 3, 4);
    ASSERT_TRUE(same(src, src.rotate90(0)));
    ASSERT_TRUE(same(src, src.rotate90(4)));
    ASSERT_TRUE(same(src, src.rotate90(-8)));
    ASSERT_TRUE(same(src.rotate90(3), src.rotate90(-1)));
    ASSERT_TRUE(same(src.rotate90(2), src.rotate90(-2)));
    ASSERT_TRUE(same(src.rotate90(1), src.rotate90(5)));
    ASSERT_TRUE(same(src, src.rotate90(1).rotate90(1).rotate90(1).rotate90(1)));
    ASSERT_TRUE(same(src.rotate90(2), src.flip_horizontal().flip_vertical()));
    ASSERT_EQ(3, src.rotate90(1).width());
    ASSERT_EQ(5, src.rotate90(1).height());
    ASSERT_EQ(5, src.rotate90(2).width());
}

UTEST(libstb_edit_tests, test_transpose_swaps_x_and_y_and_is_its_own_inverse) {
    const stb::image src = gradient(5, 3, 3);
    const stb::image t = src.transpose();
    ASSERT_EQ(3, t.width());
    ASSERT_EQ(5, t.height());
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            for (int k = 0; k < 3; ++k) ASSERT_EQ(src(x, y, k), t(y, x, k));
    ASSERT_TRUE(same(src, t.transpose()));
}

UTEST(libstb_edit_tests, test_turns_work_on_images_larger_than_one_tile_for_every_channel_count) {
    for (int c = 1; c <= 4; ++c) {
        const stb::image src = gradient(77, 45, c);  // not a multiple of the 32-pixel tile
        const stb::image r = src.rotate90();
        ASSERT_EQ(45, r.width());
        ASSERT_EQ(77, r.height());
        for (int y = 0; y < r.height(); y += 7)
            for (int x = 0; x < r.width(); x += 5)
                for (int k = 0; k < c; ++k) ASSERT_EQ(src(y, 44 - x, k), r(x, y, k));
        ASSERT_TRUE(same(src, src.rotate90(3).rotate90(1)));
    }
}

UTEST(libstb_edit_tests, test_one_pixel_images_survive_every_transform) {
    const stb::image px = make(1, 1, 3, {1, 2, 3});
    ASSERT_TRUE(same(px, px.flip_horizontal()));
    ASSERT_TRUE(same(px, px.rotate90()));
    ASSERT_TRUE(same(px, px.transpose()));
    for (int o = 1; o <= 8; ++o) ASSERT_TRUE(same(px, px.orient(o)));
}

UTEST(libstb_edit_tests, test_orient_is_the_transform_that_makes_the_image_upright) {
    const stb::image s = gradient(5, 3, 3);
    ASSERT_TRUE(same(s, s.orient(1)));
    ASSERT_TRUE(same(s.flip_horizontal(), s.orient(2)));
    ASSERT_TRUE(same(s.rotate90(2), s.orient(3)));
    ASSERT_TRUE(same(s.flip_vertical(), s.orient(4)));
    ASSERT_TRUE(same(s.transpose(), s.orient(5)));
    ASSERT_TRUE(same(s.rotate90(1), s.orient(6)));
    ASSERT_TRUE(same(s.transpose().rotate90(2), s.orient(7)));  // across the anti-diagonal
    ASSERT_TRUE(same(s.rotate90(3), s.orient(8)));
    ASSERT_THROWS(s.orient(0), std::invalid_argument);
    ASSERT_THROWS(s.orient(9), std::invalid_argument);
    ASSERT_THROWS(s.orient(-1), std::invalid_argument);
}

UTEST(libstb_edit_tests, test_geometry_does_not_modify_the_source) {
    const stb::image src = gradient(5, 3, 3);
    const stb::image keep = src.copy();
    (void)src.flip_horizontal();
    (void)src.rotate90();
    (void)src.crop(1, 1, 2, 2);
    (void)src.pad(1, 1, 1, 1);
    ASSERT_TRUE(same(src, keep));
}

// -------------------------------------------------------------------- pad

UTEST(libstb_edit_tests, test_pad_puts_the_image_in_the_middle_of_a_border) {
    const stb::image src = solid(2, 2, 3, 200);
    const stb::image p = src.pad(1, 2, 3, 4, {10, 20, 30, 40});
    ASSERT_EQ(6, p.width());
    ASSERT_EQ(8, p.height());
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 6; ++x) {
            const bool inside = x >= 1 && x < 3 && y >= 2 && y < 4;
            ASSERT_EQ(inside ? 200 : 10, p(x, y, 0));
            ASSERT_EQ(inside ? 200 : 20, p(x, y, 1));
            ASSERT_EQ(inside ? 200 : 30, p(x, y, 2));
        }
}

UTEST(libstb_edit_tests, test_pad_fill_is_per_channel_and_defaults_to_zero) {
    const stb::image rgba = solid(1, 1, 4, 255).pad(1, 0, 0, 0);
    ASSERT_EQ(0, rgba(0, 0, 3));  // transparent black
    ASSERT_EQ(255, rgba(1, 0, 3));
    const stb::image ga = solid(1, 1, 2, 9).pad(0, 1, 0, 0, {77, 88, 99, 99});
    ASSERT_EQ(77, ga(0, 0, 0));
    ASSERT_EQ(88, ga(0, 0, 1));
    ASSERT_EQ(9, ga(0, 1, 0));
}

UTEST(libstb_edit_tests, test_pad_by_nothing_is_a_copy_and_negative_is_an_error) {
    const stb::image src = gradient(3, 3, 3);
    stb::image p = src.pad(0, 0, 0, 0);
    ASSERT_TRUE(same(src, p));
    ASSERT_TRUE(p.data() != src.data());
    ASSERT_THROWS(src.pad(-1, 0, 0, 0), std::invalid_argument);
    ASSERT_THROWS(src.pad(0, 0, 0, -1), std::invalid_argument);
}

UTEST(libstb_edit_tests, test_pad_beyond_two_gib_is_a_limit_error_and_overflow_is_safe) {
    const stb::image src = gradient(2, 2, 3);
    ASSERT_THROWS(src.pad(INT_MAX, 0, 0, 0), stb::limit_error);
    ASSERT_THROWS(src.pad(INT_MAX, 0, INT_MAX, 0), stb::limit_error);
    ASSERT_THROWS(src.pad(30000, 30000, 30000, 30000), stb::limit_error);
}

// -------------------------------------------------------------- thumbnail

UTEST(libstb_edit_tests, test_thumbnail_keeps_the_aspect_ratio_and_touches_the_limiting_side) {
    const stb::image tall = gradient(50, 100, 3), wide = gradient(100, 50, 3);
    stb::image a = tall.thumbnail(20, 20);
    ASSERT_EQ(10, a.width());
    ASSERT_EQ(20, a.height());
    stb::image b = wide.thumbnail(20, 20);
    ASSERT_EQ(20, b.width());
    ASSERT_EQ(10, b.height());
    stb::image c = wide.thumbnail(40, 10);  // the height limits
    ASSERT_EQ(20, c.width());
    ASSERT_EQ(10, c.height());
    stb::image d = wide.thumbnail(1000, 25);  // only the height is exceeded
    ASSERT_EQ(50, d.width());
    ASSERT_EQ(25, d.height());
}

UTEST(libstb_edit_tests, test_thumbnail_never_enlarges_and_never_makes_a_side_zero) {
    const stb::image small = gradient(10, 5, 3);
    stb::image same_size = small.thumbnail(100, 100);
    ASSERT_TRUE(same(small, same_size));
    ASSERT_TRUE(same_size.data() != small.data());
    stb::image exact = small.thumbnail(10, 5);
    ASSERT_TRUE(same(small, exact));
    stb::image line = gradient(1000, 3, 1).thumbnail(10, 10);
    ASSERT_EQ(10, line.width());
    ASSERT_EQ(1, line.height());
}

UTEST(libstb_edit_tests, test_thumbnail_takes_the_same_filter_arguments_as_resize) {
    const stb::image src = gradient(40, 20, 3);
    ASSERT_TRUE(same(src.resize(20, 10, "linear"), src.thumbnail(20, 20, "linear")));
    ASSERT_TRUE(same(src.resize(20, 10, stb::resize_filter::point), src.thumbnail(20, 20, stb::resize_filter::point)));
    ASSERT_TRUE(same(src.resize(20, 10), src.thumbnail(20, 20)));
    ASSERT_THROWS(src.thumbnail(0, 10), std::invalid_argument);
    ASSERT_THROWS(src.thumbnail(10, -1), std::invalid_argument);
    ASSERT_THROWS(src.thumbnail(10, 10, "lanczos"), std::invalid_argument);
}

// ---------------------------------------------------------------- convert

UTEST(libstb_edit_tests, test_convert_to_gray_uses_the_pillow_luma_formula) {
    const stb::image rgb = make(4, 1, 3, {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255});
    const stb::image g = rgb.convert(1);
    ASSERT_EQ(76, g(0, 0, 0));    // 0.299 * 255
    ASSERT_EQ(150, g(1, 0, 0));   // 0.587 * 255
    ASSERT_EQ(29, g(2, 0, 0));    // 0.114 * 255
    ASSERT_EQ(255, g(3, 0, 0));
}

UTEST(libstb_edit_tests, test_convert_between_every_pair_of_channel_counts) {
    const stb::image gray = make(1, 1, 1, {100});
    const stb::image ga = make(1, 1, 2, {100, 50});
    const stb::image rgb = make(1, 1, 3, {10, 20, 30});
    const stb::image rgba = make(1, 1, 4, {10, 20, 30, 40});

    stb::image x = gray.convert(2);
    ASSERT_EQ(100, x(0, 0, 0));
    ASSERT_EQ(255, x(0, 0, 1));
    x = gray.convert(3);
    ASSERT_TRUE(x(0, 0, 0) == 100 && x(0, 0, 1) == 100 && x(0, 0, 2) == 100);
    x = gray.convert(4);
    ASSERT_TRUE(x(0, 0, 0) == 100 && x(0, 0, 2) == 100 && x(0, 0, 3) == 255);

    x = ga.convert(1);
    ASSERT_EQ(100, x(0, 0, 0));
    x = ga.convert(3);
    ASSERT_TRUE(x(0, 0, 0) == 100 && x(0, 0, 2) == 100);
    x = ga.convert(4);
    ASSERT_TRUE(x(0, 0, 1) == 100 && x(0, 0, 3) == 50);  // alpha kept

    x = rgb.convert(2);
    ASSERT_EQ(255, x(0, 0, 1));
    x = rgb.convert(4);
    ASSERT_TRUE(x(0, 0, 0) == 10 && x(0, 0, 2) == 30 && x(0, 0, 3) == 255);

    x = rgba.convert(3);
    ASSERT_TRUE(x(0, 0, 0) == 10 && x(0, 0, 1) == 20 && x(0, 0, 2) == 30);  // alpha dropped
    x = rgba.convert(2);
    ASSERT_EQ(40, x(0, 0, 1));  // alpha kept
    ASSERT_EQ(rgb.convert(1)(0, 0, 0), x(0, 0, 0));
    x = rgba.convert(1);
    ASSERT_EQ(rgb.convert(1)(0, 0, 0), x(0, 0, 0));
}

UTEST(libstb_edit_tests, test_convert_to_the_same_count_is_a_copy_and_round_trips_are_lossless) {
    const stb::image src = gradient(6, 4, 3);
    stb::image same_count = src.convert(3);
    ASSERT_TRUE(same(src, same_count));
    ASSERT_TRUE(same_count.data() != src.data());
    ASSERT_TRUE(same(src, src.convert(4).convert(3)));
    const stb::image g = gradient(6, 4, 1);
    ASSERT_TRUE(same(g, g.convert(3).convert(1)));
    ASSERT_TRUE(same(g, g.convert(4).convert(1)));
}

UTEST(libstb_edit_tests, test_convert_rejects_a_bad_channel_count) {
    const stb::image src = gradient(2, 2, 3);
    ASSERT_THROWS(src.convert(0), std::invalid_argument);
    ASSERT_THROWS(src.convert(5), std::invalid_argument);
    ASSERT_THROWS(stb::image().convert(3), std::invalid_argument);
}

// ------------------------------------------------------------ split/merge

UTEST(libstb_edit_tests, test_split_then_merge_is_the_identity_for_every_channel_count) {
    for (int c = 1; c <= 4; ++c) {
        const stb::image src = gradient(7, 5, c);
        const std::vector<stb::image> planes = src.split();
        ASSERT_EQ(std::size_t(c), planes.size());
        for (int k = 0; k < c; ++k) {
            ASSERT_EQ(1, planes[k].channels());
            ASSERT_EQ(src(3, 2, k), planes[k](3, 2, 0));
        }
        ASSERT_TRUE(same(src, stb::image::merge(planes)));
    }
}

UTEST(libstb_edit_tests, test_merge_rejects_wrong_input) {
    const stb::image a = gradient(3, 3, 1), b = gradient(4, 3, 1), rgb = gradient(3, 3, 3);
    ASSERT_THROWS(stb::image::merge(std::vector<const stb::image*>{}), std::invalid_argument);
    ASSERT_THROWS(stb::image::merge(std::vector<const stb::image*>{&a, &b}), std::invalid_argument);  // sizes
    ASSERT_THROWS(stb::image::merge(std::vector<const stb::image*>{&a, &rgb}), std::invalid_argument);  // channels
    ASSERT_THROWS(stb::image::merge(std::vector<const stb::image*>{&a, &a, &a, &a, &a}), std::invalid_argument);
    ASSERT_THROWS(stb::image::merge(std::vector<const stb::image*>{nullptr}), std::invalid_argument);
}

// -------------------------------------------------------------- composite

UTEST(libstb_edit_tests, test_composite_with_an_opaque_overlay_replaces_the_pixels) {
    const stb::image base = solid(4, 4, 3, 10);
    const stb::image over = solid(2, 2, 3, 200);
    const stb::image r = base.composite(over, 1, 1);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) ASSERT_EQ((x >= 1 && x < 3 && y >= 1 && y < 3) ? 200 : 10, r(x, y, 1));
}

UTEST(libstb_edit_tests, test_composite_blends_by_the_overlay_alpha) {
    const stb::image base = solid(1, 1, 3, 100);
    ASSERT_EQ(100, base.composite(make(1, 1, 4, {200, 200, 200, 0}))(0, 0, 0));    // transparent
    ASSERT_EQ(200, base.composite(make(1, 1, 4, {200, 200, 200, 255}))(0, 0, 0));  // opaque
    ASSERT_EQ(150, base.composite(make(1, 1, 4, {200, 200, 200, 128}))(0, 0, 0));  // about half
    ASSERT_EQ(3, base.composite(make(1, 1, 4, {200, 200, 200, 128})).channels());  // keeps this image's channels
}

UTEST(libstb_edit_tests, test_composite_onto_alpha_uses_the_over_operator) {
    const stb::image base = make(1, 1, 4, {0, 0, 255, 128});
    const stb::image r = base.composite(make(1, 1, 4, {255, 0, 0, 128}));
    ASSERT_EQ(192, r(0, 0, 3));  // 128 + 128 * (1 - 128/255)
    ASSERT_TRUE(r(0, 0, 0) > 130 && r(0, 0, 0) < 190);  // red and blue mixed, red the stronger
    ASSERT_TRUE(r(0, 0, 2) > 60 && r(0, 0, 2) < 120);
    // Over a fully transparent pixel the overlay's colour comes through untouched.
    const stb::image clear = make(1, 1, 4, {9, 9, 9, 0}).composite(make(1, 1, 4, {40, 50, 60, 77}));
    ASSERT_TRUE(clear(0, 0, 0) == 40 && clear(0, 0, 1) == 50 && clear(0, 0, 2) == 60 && clear(0, 0, 3) == 77);
}

UTEST(libstb_edit_tests, test_composite_clips_an_overlay_that_hangs_over_the_edges) {
    const stb::image base = solid(3, 3, 1, 0);
    const stb::image over = solid(3, 3, 1, 255);
    const stb::image r = base.composite(over, -2, 2);  // only its top-right pixel row of 1 lands inside
    int lit = 0;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) lit += r(x, y, 0) == 255;
    ASSERT_EQ(1, lit);
    ASSERT_EQ(255, r(0, 2, 0));
    ASSERT_TRUE(same(base, base.composite(over, 3, 0)));   // entirely right of the image
    ASSERT_TRUE(same(base, base.composite(over, 0, -3)));  // entirely above it
    ASSERT_TRUE(same(base, base.composite(over, INT_MAX, INT_MAX)));
    ASSERT_TRUE(same(base, base.composite(over, INT_MIN, INT_MIN)));
}

UTEST(libstb_edit_tests, test_composite_mixes_channel_counts) {
    const stb::image rgb = solid(1, 1, 3, 50);
    ASSERT_EQ(77, rgb.composite(make(1, 1, 1, {77}))(0, 0, 2));  // gray overlay is opaque gray
    const stb::image gray = solid(1, 1, 1, 50);
    ASSERT_EQ(1, gray.composite(make(1, 1, 3, {90, 90, 90})).channels());
    ASSERT_EQ(90, gray.composite(make(1, 1, 3, {90, 90, 90}))(0, 0, 0));
    ASSERT_EQ(70, gray.composite(make(1, 1, 2, {90, 128}))(0, 0, 0));  // 50 -> 90 by about 128/255
}

UTEST(libstb_edit_tests, test_composite_does_not_modify_either_input) {
    const stb::image base = gradient(4, 4, 4), over = gradient(2, 2, 4);
    const stb::image base_copy = base.copy(), over_copy = over.copy();
    (void)base.composite(over, 1, 1);
    ASSERT_TRUE(same(base, base_copy));
    ASSERT_TRUE(same(over, over_copy));
}

UTEST(libstb_edit_tests, test_flatten_blends_alpha_onto_a_background) {
    const stb::image rgba = make(2, 1, 4, {255, 0, 0, 255, 255, 0, 0, 0});
    const stb::image r = rgba.flatten({0, 0, 255});
    ASSERT_EQ(3, r.channels());
    ASSERT_TRUE(r(0, 0, 0) == 255 && r(0, 0, 2) == 0);    // opaque pixel kept
    ASSERT_TRUE(r(1, 0, 0) == 0 && r(1, 0, 2) == 255);    // transparent pixel shows the background
    const stb::image ga = make(1, 1, 2, {100, 0}).flatten({200, 200, 200});
    ASSERT_EQ(1, ga.channels());
    ASSERT_EQ(200, ga(0, 0, 0));
    const stb::image no_alpha = gradient(3, 3, 3);
    ASSERT_TRUE(same(no_alpha, no_alpha.flatten()));
    ASSERT_EQ(255, make(1, 1, 4, {5, 5, 5, 0}).flatten()(0, 0, 0));  // default background is white
}

UTEST(libstb_edit_tests, test_composite_of_one_colour_over_itself_keeps_that_colour_for_every_pair_of_alphas) {
    // Regression: dividing by an already-rounded alpha made the colour overshoot (255 -> 260
    // wrapped to 4) for e.g. overlay alpha 8 over destination alpha 16.
    for (int c : {255, 100, 1}) {
        stb::image base(256, 255, 4), over(256, 255, 4);
        for (int y = 0; y < 255; ++y)
            for (int x = 0; x < 256; ++x) {
                std::uint8_t* b = base.data() + (std::size_t(y) * 256 + std::size_t(x)) * 4;
                std::uint8_t* o = over.data() + (std::size_t(y) * 256 + std::size_t(x)) * 4;
                b[0] = b[1] = b[2] = o[0] = o[1] = o[2] = std::uint8_t(c);
                b[3] = std::uint8_t(x);      // destination alpha 0..255
                o[3] = std::uint8_t(y + 1);  // overlay alpha 1..255
            }
        const stb::image r = base.composite(over);
        for (int y = 0; y < 255; ++y)
            for (int x = 0; x < 256; ++x) {
                for (int k = 0; k < 3; ++k) ASSERT_EQ(c, r(x, y, k));
                const int sa = y + 1, expect = (sa * 255 + x * (255 - sa) + 127) / 255;
                ASSERT_EQ(expect, r(x, y, 3));
            }
    }
    const stb::image r = make(1, 1, 4, {255, 255, 255, 16}).composite(make(1, 1, 4, {255, 255, 255, 8}));
    ASSERT_TRUE(r(0, 0, 0) == 255 && r(0, 0, 1) == 255 && r(0, 0, 2) == 255);
    ASSERT_EQ(23, r(0, 0, 3));
}

// ------------------------------------------------- SIMD kernels == portable

namespace {

// RAII: force one kernel set, put the previous choice back.
struct simd_mode {
    bool was_avx2;
    explicit simd_mode(bool enable) : was_avx2(std::string(stb::simd_name()) == "avx2") { stb::set_simd_enabled(enable); }
    ~simd_mode() { stb::set_simd_enabled(was_avx2); }
};

stb::image random_image(int w, int h, int c, unsigned seed) {
    stb::image img(w, h, c);
    std::uint32_t s = seed * 2654435761u + 1;
    for (std::size_t i = 0; i < std::size_t(w) * h * c; ++i) {
        s = s * 1664525u + 1013904223u;
        img.data()[i] = std::uint8_t(s >> 24);
    }
    return img;
}

std::uint8_t luma_ref(int r, int g, int b) { return std::uint8_t((r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16); }

// The plain integer formulas, one pixel at a time: what the vector kernels must equal.
stb::image composite_reference(const stb::image& base, const stb::image& over, int ox, int oy) {
    stb::image out = base.copy();
    const int dc = base.channels(), sc = over.channels(), nc = dc <= 2 ? 1 : 3;
    const bool dst_alpha = dc == 2 || dc == 4, src_alpha = sc == 2 || sc == 4;
    for (int y = 0; y < base.height(); ++y)
        for (int x = 0; x < base.width(); ++x) {
            const int sx = x - ox, sy = y - oy;
            if (sx < 0 || sy < 0 || sx >= over.width() || sy >= over.height()) continue;
            const std::uint8_t* s = &over(sx, sy, 0);
            std::uint8_t* d = &out(x, y, 0);
            int col[3];  // overlay colour in the destination's colour model
            if (nc == 1) col[0] = sc <= 2 ? s[0] : luma_ref(s[0], s[1], s[2]);
            else for (int k = 0; k < 3; ++k) col[k] = sc <= 2 ? s[0] : s[k];
            const int sa = src_alpha ? s[sc - 1] : 255;
            if (!src_alpha) {  // opaque: replace, with alpha 255 where the destination has it
                for (int k = 0; k < nc; ++k) d[k] = std::uint8_t(col[k]);
                if (dst_alpha) d[dc - 1] = 255;
                continue;
            }
            if (!dst_alpha) {
                for (int k = 0; k < nc; ++k) d[k] = std::uint8_t((col[k] * sa + d[k] * (255 - sa) + 127) / 255);
                continue;
            }
            const int da = d[dc - 1], ws = sa * 255, wd = da * (255 - sa), den = ws + wd;
            if (den == 0) continue;  // both transparent: unchanged
            for (int k = 0; k < nc; ++k) d[k] = std::uint8_t((col[k] * ws + d[k] * wd + den / 2) / den);
            d[dc - 1] = std::uint8_t((den + 127) / 255);
        }
    return out;
}

}  // namespace

UTEST(libstb_simd_tests, test_simd_name_is_known_and_can_be_switched) {
    simd_mode keep(true);
    const std::string n = stb::simd_name();
    ASSERT_TRUE(n == "avx2" || n == "baseline");
    stb::set_simd_enabled(false);
    ASSERT_TRUE(std::string(stb::simd_name()) == "baseline");
    stb::set_simd_enabled(true);
    ASSERT_TRUE(std::string(stb::simd_name()) == n);  // back to what the CPU allows
}

UTEST(libstb_simd_tests, test_convert_is_identical_with_and_without_avx2_for_every_pair) {
    for (int sc = 1; sc <= 4; ++sc)
        for (int dc = 1; dc <= 4; ++dc) {
            const stb::image src = random_image(1003, 7, sc, unsigned(sc * 10 + dc));  // 1003: not a multiple of 8
            stb::image fast = [&] { simd_mode m(true); return src.convert(dc); }();
            stb::image slow = [&] { simd_mode m(false); return src.convert(dc); }();
            ASSERT_TRUE(same(fast, slow));
        }
}

UTEST(libstb_simd_tests, test_composite_equals_the_integer_formula_for_every_channel_combination) {
    for (int dc = 1; dc <= 4; ++dc)
        for (int sc = 1; sc <= 4; ++sc) {
            const stb::image base = random_image(317, 11, dc, unsigned(dc * 7 + sc));
            const stb::image over = random_image(211, 9, sc, unsigned(dc * 13 + sc + 100));
            for (const auto& off : {std::pair<int, int>{0, 0}, {5, 2}, {-17, -3}, {200, 8}}) {
                const stb::image want = composite_reference(base, over, off.first, off.second);
                for (bool avx : {true, false}) {
                    simd_mode m(avx);
                    ASSERT_TRUE(same(want, base.composite(over, off.first, off.second)));
                }
            }
        }
}

UTEST(libstb_simd_tests, test_composite_equals_the_integer_formula_for_every_pair_of_alphas_and_colours) {
    // Exhaustive in (source alpha, destination alpha); the colours span the extremes and the middle.
    for (bool avx : {true, false}) {
        simd_mode m(avx);
        for (int sca : {0, 1, 77, 128, 255}) {
            stb::image base(256, 256, 4), over(256, 256, 4);
            for (int sa = 0; sa < 256; ++sa)
                for (int da = 0; da < 256; ++da) {
                    std::uint8_t* b = &base(da, sa, 0);
                    std::uint8_t* o = &over(da, sa, 0);
                    b[0] = std::uint8_t(da * 3), b[1] = std::uint8_t(255 - da), b[2] = std::uint8_t(sca);
                    b[3] = std::uint8_t(da);
                    o[0] = std::uint8_t(sa), o[1] = std::uint8_t(sca), o[2] = std::uint8_t(255 - sca);
                    o[3] = std::uint8_t(sa);
                }
            ASSERT_TRUE(same(composite_reference(base, over, 0, 0), base.composite(over)));
        }
    }
}

UTEST(libstb_simd_tests, test_flatten_is_identical_with_and_without_avx2) {
    const stb::image rgba = random_image(999, 13, 4, 5), ga = random_image(999, 13, 2, 6);
    for (const stb::image* src : {&rgba, &ga}) {
        stb::image fast = [&] { simd_mode m(true); return src->flatten({20, 40, 160}); }();
        stb::image slow = [&] { simd_mode m(false); return src->flatten({20, 40, 160}); }();
        ASSERT_TRUE(same(fast, slow));
    }
}
