#include <climits>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "test_common.hpp"

using testutil::gradient;
using testutil::same;
using testutil::solid;

namespace {
stb::image make(int w, int h, int c, std::vector<std::uint8_t> px) { return stb::image(w, h, testutil::fmt(c), std::move(px)); }

// The red channel of one blended pixel: base (r, 100, 100) under overlay (s, s, s).
int blended(stb::blend_mode m, int base, int over, float opacity = 1.0f) {
    return stb::image(1, 1, stb::pixel_format::rgb, {std::uint8_t(base), 100, 100})
        .blend(make(1, 1, 3, {std::uint8_t(over), 0, 0}), 0, 0, m, opacity)(0, 0, 0);
}
}  // namespace

// ------------------------------------------------------------------ blend modes

UTEST(libstb_blend_tests, test_each_mode_gives_its_formula_on_one_channel) {
    using stb::blend_mode;
    ASSERT_EQ(100, blended(blend_mode::multiply, 200, 128));   // 200 * 128 / 255
    ASSERT_EQ(200, blended(blend_mode::multiply, 200, 255));   // white changes nothing
    ASSERT_EQ(0, blended(blend_mode::multiply, 200, 0));
    ASSERT_EQ(228, blended(blend_mode::screen, 200, 128));     // 255 - 55 * 127 / 255
    ASSERT_EQ(200, blended(blend_mode::screen, 200, 0));       // black changes nothing
    ASSERT_EQ(255, blended(blend_mode::screen, 200, 255));
    ASSERT_EQ(100, blended(blend_mode::darken, 200, 100));
    ASSERT_EQ(100, blended(blend_mode::darken, 100, 200));
    ASSERT_EQ(200, blended(blend_mode::lighten, 200, 100));
    ASSERT_EQ(200, blended(blend_mode::lighten, 100, 200));
    ASSERT_EQ(255, blended(blend_mode::add, 200, 100));        // clamped
    ASSERT_EQ(150, blended(blend_mode::add, 100, 50));
    ASSERT_EQ(100, blended(blend_mode::difference, 200, 100));
    ASSERT_EQ(100, blended(blend_mode::difference, 100, 200));
    ASSERT_EQ(77, blended(blend_mode::normal, 200, 77));
}

UTEST(libstb_blend_tests, test_overlay_is_multiply_on_dark_and_screen_on_light) {
    using stb::blend_mode;
    ASSERT_EQ(64, blended(blend_mode::overlay, 64, 128));    // 2 * (64/255) * (128/255) = 64/255 (rounded)
    ASSERT_EQ(191, blended(blend_mode::overlay, 191, 128));  // mirror image on the light side
    ASSERT_EQ(0, blended(blend_mode::overlay, 0, 200));
    ASSERT_EQ(255, blended(blend_mode::overlay, 255, 200));
}

UTEST(libstb_blend_tests, test_opacity_scales_the_overlay_alpha) {
    using stb::blend_mode;
    ASSERT_EQ(100, blended(blend_mode::normal, 100, 200, 0.0f));
    ASSERT_EQ(200, blended(blend_mode::normal, 100, 200, 1.0f));
    ASSERT_EQ(150, blended(blend_mode::normal, 100, 200, 0.5f));
    ASSERT_EQ(100, blended(blend_mode::multiply, 200, 128, 1.0f));
    ASSERT_EQ(150, blended(blend_mode::multiply, 200, 128, 0.5f));  // halfway to the multiplied value
}

UTEST(libstb_blend_tests, test_opacity_outside_0_to_1_and_an_empty_image_throw) {
    stb::image a = solid(2, 2, 3, 10);
    const stb::image o = solid(1, 1, 3, 20);
    ASSERT_THROWS(a.blend(o, 0, 0, stb::blend_mode::normal, -0.1f), std::invalid_argument);
    ASSERT_THROWS(a.blend(o, 0, 0, stb::blend_mode::normal, 1.1f), std::invalid_argument);
    ASSERT_THROWS(a.blend(o, 0, 0, stb::blend_mode::normal, std::nanf("")), std::invalid_argument);
    ASSERT_THROWS(stb::image().blend(o), std::invalid_argument);
    ASSERT_THROWS(a.blend(stb::image()), std::invalid_argument);
}

UTEST(libstb_blend_tests, test_the_general_path_honours_alpha_below_and_above) {
    // Transparent overlay: nothing changes, whatever the mode.
    stb::image a = make(1, 1, 4, {10, 20, 30, 200});
    a.blend(make(1, 1, 4, {255, 255, 255, 0}), 0, 0, stb::blend_mode::difference);
    ASSERT_EQ(10, a(0, 0, 0));
    ASSERT_EQ(200, a(0, 0, 3));
    // Opaque overlay on a transparent pixel: the overlay shows as it is (no mode has a backdrop to mix with).
    stb::image b = make(1, 1, 4, {10, 20, 30, 0});
    b.blend(make(1, 1, 4, {90, 80, 70, 255}), 0, 0, stb::blend_mode::multiply);
    ASSERT_EQ(90, b(0, 0, 0));
    ASSERT_EQ(255, b(0, 0, 3));
    // Normal at opacity 0.999 on opaque rgba is within one level of the integer kernel.
    stb::image c = make(1, 1, 4, {100, 100, 100, 255});
    c.blend(make(1, 1, 4, {200, 200, 200, 255}), 0, 0, stb::blend_mode::normal, 0.999f);
    ASSERT_TRUE(c(0, 0, 0) >= 199 && c(0, 0, 0) <= 200);
}

UTEST(libstb_blend_tests, test_blend_mixes_channel_counts_in_every_mode) {
    const stb::blend_mode modes[] = {stb::blend_mode::multiply, stb::blend_mode::screen, stb::blend_mode::overlay,
                                     stb::blend_mode::darken,   stb::blend_mode::lighten, stb::blend_mode::add,
                                     stb::blend_mode::difference};
    for (const stb::blend_mode m : modes)
        for (int dc = 1; dc <= 4; ++dc)
            for (int sc = 1; sc <= 4; ++sc) {
                stb::image base = solid(4, 3, dc, 90);
                const stb::image over = solid(2, 2, sc, 170);
                base.blend(over, 3, 2, m, 0.7f);  // hangs over the right and bottom edges
                ASSERT_EQ(dc, base.channels());
                ASSERT_EQ(90, base(0, 0, 0));      // outside the overlay: untouched
            }
}

UTEST(libstb_blend_tests, test_blend_chains_and_returns_a_temporary_by_value) {
    const stb::image base = solid(4, 4, 3, 100);
    stb::image r = base.copy().blend(solid(2, 2, 3, 200), 0, 0).blend(solid(2, 2, 3, 50), 2, 2);
    ASSERT_EQ(200, r(0, 0, 0));
    ASSERT_EQ(50, r(3, 3, 0));
    ASSERT_EQ(100, base(0, 0, 0));
}

UTEST(libstb_blend_tests, test_blend_mode_names_round_trip) {
    for (const char* n : {"normal", "multiply", "screen", "overlay", "darken", "lighten", "add", "difference"})
        ASSERT_TRUE(stb::blend_mode_name(stb::blend_mode_from_name(n)) == n);
    ASSERT_THROWS(stb::blend_mode_from_name("Multiply"), std::invalid_argument);
    ASSERT_THROWS(stb::blend_mode_from_name(""), std::invalid_argument);
}

// ------------------------------------------------------------------------ fill

UTEST(libstb_blend_tests, test_fill_overwrites_the_whole_image_or_a_clipped_rectangle) {
    stb::image a = solid(4, 3, 3, 0);
    a.fill("red");
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x) ASSERT_TRUE(a(x, y, 0) == 255 && a(x, y, 1) == 0 && a(x, y, 2) == 0);
    stb::image b = solid(4, 3, 3, 0);
    b.fill({1, 2, 3}, 1, 1, 2, 5);  // taller than what is left: clipped
    ASSERT_EQ(0, b(0, 1, 0));
    ASSERT_EQ(1, b(1, 1, 0));
    ASSERT_EQ(3, b(2, 2, 2));
    ASSERT_EQ(0, b(3, 2, 0));
    ASSERT_EQ(0, b(1, 0, 0));
    stb::image c = solid(4, 3, 3, 9);
    c.fill("red", -2, -2, 3, 3);  // only (0, 0) lands inside
    ASSERT_EQ(255, c(0, 0, 0));
    ASSERT_EQ(9, c(1, 0, 0));
    ASSERT_EQ(9, c(0, 1, 0));
}

UTEST(libstb_blend_tests, test_fill_outside_the_image_or_with_no_area_changes_nothing) {
    const stb::image src = gradient(5, 4, 3);
    stb::image a = src.copy();
    a.fill("blue", 5, 0, 3, 3).fill("blue", 0, 4, 3, 3).fill("blue", -9, 0, 9, 9).fill("blue", 1, 1, 0, 2);
    a.fill("blue", INT_MAX, INT_MAX, INT_MAX, INT_MAX).fill("blue", INT_MIN, INT_MIN, 1, 1);
    ASSERT_TRUE(same(src, a));
}

UTEST(libstb_blend_tests, test_fill_stores_the_colour_in_the_format_of_the_image) {
    stb::image g = solid(1, 1, 1, 0).fill({255, 0, 0});  // gray takes the luma: 0.299 * 255
    ASSERT_EQ(76, g(0, 0, 0));
    stb::image ga = solid(1, 1, 2, 0).fill({255, 0, 0, 40});
    ASSERT_EQ(76, ga(0, 0, 0));
    ASSERT_EQ(40, ga(0, 0, 1));
    stb::image rgb = solid(1, 1, 3, 0).fill({1, 2, 3, 40});  // the alpha is dropped
    ASSERT_EQ(3, rgb.channels());
    ASSERT_EQ(3, rgb(0, 0, 2));
    stb::image rgba = solid(1, 1, 4, 0).fill("#0a141e80");
    ASSERT_TRUE(rgba(0, 0, 0) == 10 && rgba(0, 0, 1) == 20 && rgba(0, 0, 2) == 30 && rgba(0, 0, 3) == 128);
}

UTEST(libstb_blend_tests, test_fill_errors_and_chaining) {
    stb::image a = solid(2, 2, 3, 0);
    ASSERT_THROWS(a.fill("red", 0, 0, -1, 2), std::invalid_argument);
    ASSERT_THROWS(a.fill("red", 0, 0, 2, -1), std::invalid_argument);
    ASSERT_THROWS(stb::image().fill("red"), std::invalid_argument);
    ASSERT_THROWS(a.fill("not a colour"), std::invalid_argument);
    const stb::image base = solid(2, 2, 3, 7);
    stb::image b = base.copy().fill("white");  // a temporary is handed back by value
    ASSERT_EQ(255, b(1, 1, 1));
    ASSERT_EQ(7, base(1, 1, 1));
}

// ------------------------------------------------------------------- alpha masks

UTEST(libstb_blend_tests, test_alpha_returns_the_alpha_channel_as_a_gray_image) {
    const stb::image rgba = make(2, 1, 4, {1, 2, 3, 10, 4, 5, 6, 20});
    const stb::image m = rgba.alpha();
    ASSERT_EQ(1, m.channels());
    ASSERT_EQ(10, m(0, 0));
    ASSERT_EQ(20, m(1, 0));
    ASSERT_EQ(5, make(1, 1, 2, {9, 5}).alpha()(0, 0));
    ASSERT_THROWS(solid(2, 2, 3, 0).alpha(), std::invalid_argument);
    ASSERT_THROWS(solid(2, 2, 1, 0).alpha(), std::invalid_argument);
    ASSERT_THROWS(stb::image().alpha(), std::invalid_argument);
}

UTEST(libstb_blend_tests, test_with_alpha_adds_or_replaces_the_alpha_channel) {
    const stb::image mask = make(2, 1, 1, {33, 44});
    const stb::image from_rgb = make(2, 1, 3, {1, 2, 3, 4, 5, 6}).with_alpha(mask);
    ASSERT_EQ(4, from_rgb.channels());
    ASSERT_TRUE(from_rgb(0, 0, 2) == 3 && from_rgb(0, 0, 3) == 33 && from_rgb(1, 0, 0) == 4 && from_rgb(1, 0, 3) == 44);
    const stb::image from_gray = make(2, 1, 1, {7, 8}).with_alpha(mask);
    ASSERT_EQ(2, from_gray.channels());
    ASSERT_TRUE(from_gray(1, 0, 0) == 8 && from_gray(1, 0, 1) == 44);
    const stb::image replaced = make(2, 1, 4, {1, 2, 3, 200, 4, 5, 6, 201}).with_alpha(mask);
    ASSERT_EQ(4, replaced.channels());
    ASSERT_EQ(33, replaced(0, 0, 3));
    ASSERT_EQ(6, replaced(1, 0, 2));
    ASSERT_TRUE(same(mask, replaced.alpha()));
}

UTEST(libstb_blend_tests, test_with_alpha_rejects_a_mask_that_is_not_gray_or_not_the_same_size) {
    const stb::image img = solid(2, 2, 3, 0);
    ASSERT_THROWS(img.with_alpha(solid(2, 2, 3, 0)), std::invalid_argument);
    ASSERT_THROWS(img.with_alpha(solid(2, 2, 2, 0)), std::invalid_argument);
    ASSERT_THROWS(img.with_alpha(solid(3, 2, 1, 0)), std::invalid_argument);
    ASSERT_THROWS(img.with_alpha(solid(2, 3, 1, 0)), std::invalid_argument);
    ASSERT_THROWS(img.with_alpha(stb::image()), std::invalid_argument);
    ASSERT_THROWS(stb::image().with_alpha(solid(1, 1, 1, 0)), std::invalid_argument);
}

UTEST(libstb_blend_tests, test_a_mask_shows_the_overlay_through_blend) {
    stb::image base = solid(3, 1, 3, 0);
    const stb::image over = make(3, 1, 3, {200, 200, 200, 200, 200, 200, 200, 200, 200});
    const stb::image masked = over.with_alpha(make(3, 1, 1, {0, 128, 255}));
    base.blend(masked);
    ASSERT_EQ(0, base(0, 0, 0));
    ASSERT_TRUE(base(1, 0, 0) >= 99 && base(1, 0, 0) <= 101);
    ASSERT_EQ(200, base(2, 0, 0));
}

// ---------------------------------------------------------------------- stacks

UTEST(libstb_blend_tests, test_hstack_puts_images_side_by_side_with_a_gap_and_a_background) {
    const stb::image a = solid(2, 3, 3, 10), b = solid(3, 1, 3, 20);
    const stb::image r = stb::image::hstack({&a, &b}, 2, "white");
    ASSERT_EQ(2 + 2 + 3, r.width());
    ASSERT_EQ(3, r.height());
    ASSERT_EQ(10, r(1, 2, 0));
    ASSERT_EQ(255, r(2, 0, 0));  // gap
    ASSERT_EQ(255, r(3, 1, 0));
    ASSERT_EQ(20, r(4, 0, 0));   // b at the top (align::start)
    ASSERT_EQ(255, r(4, 1, 0));  // below b: background
}

UTEST(libstb_blend_tests, test_stack_alignment_moves_the_shorter_images_across_the_stack) {
    const stb::image tall = solid(1, 5, 1, 10), shorter = solid(1, 2, 1, 99);
    const std::vector<stb::image> v = [&] {
        std::vector<stb::image> x;
        x.push_back(tall.copy());
        x.push_back(shorter.copy());
        return x;
    }();
    const stb::image s = stb::image::hstack(v, 0, "black", stb::align::start);
    const stb::image c = stb::image::hstack(v, 0, "black", stb::align::center);
    const stb::image e = stb::image::hstack(v, 0, "black", stb::align::end);
    ASSERT_TRUE(s(1, 0) == 99 && s(1, 1) == 99 && s(1, 2) == 0);
    ASSERT_TRUE(c(1, 0) == 0 && c(1, 1) == 99 && c(1, 2) == 99 && c(1, 3) == 0);  // (5 - 2) / 2 = 1
    ASSERT_TRUE(e(1, 2) == 0 && e(1, 3) == 99 && e(1, 4) == 99);
}

UTEST(libstb_blend_tests, test_vstack_stacks_top_to_bottom) {
    const stb::image a = solid(2, 1, 1, 10), b = solid(4, 2, 1, 20);
    const stb::image r = stb::image::vstack({&a, &b}, 1, {0, 0, 0}, stb::align::end);
    ASSERT_EQ(4, r.width());
    ASSERT_EQ(1 + 1 + 2, r.height());
    ASSERT_TRUE(r(2, 0) == 10 && r(3, 0) == 10 && r(0, 0) == 0);  // a at the right edge
    ASSERT_EQ(0, r(0, 1));                                         // gap
    ASSERT_EQ(20, r(0, 2));
    ASSERT_EQ(20, r(3, 3));
}

UTEST(libstb_blend_tests, test_stack_result_has_colour_and_alpha_if_any_input_has) {
    const stb::image g = solid(1, 1, 1, 50), ga = solid(1, 1, 2, 60), rgb = solid(1, 1, 3, 70);
    ASSERT_EQ(1, stb::image::hstack({&g, &g}).channels());
    ASSERT_EQ(2, stb::image::hstack({&g, &ga}).channels());
    ASSERT_EQ(3, stb::image::hstack({&g, &rgb}).channels());
    const stb::image r = stb::image::hstack({&g, &ga, &rgb});
    ASSERT_EQ(4, r.channels());
    ASSERT_TRUE(r(0, 0, 0) == 50 && r(0, 0, 3) == 255);  // gray -> rgb with alpha 255
    ASSERT_TRUE(r(1, 0, 2) == 60 && r(1, 0, 3) == 60);   // gray_alpha keeps its alpha
    ASSERT_TRUE(r(2, 0, 1) == 70 && r(2, 0, 3) == 255);
}

UTEST(libstb_blend_tests, test_stack_errors) {
    const stb::image a = solid(1, 1, 1, 1);
    ASSERT_THROWS(stb::image::hstack(std::vector<const stb::image*>{}), std::invalid_argument);
    ASSERT_THROWS(stb::image::hstack({&a, nullptr}), std::invalid_argument);
    const stb::image empty;
    ASSERT_THROWS(stb::image::vstack({&a, &empty}), std::invalid_argument);
    ASSERT_THROWS(stb::image::vstack({&a, &a}, -1), std::invalid_argument);
    // Three images that claim to be 1 GiB wide (wrapped around one byte that is never read: the
    // size is refused before any pixel is touched).
    std::uint8_t byte = 0;
    const stb::image wide = stb::image::wrap(INT_MAX / 2, 1, stb::pixel_format::gray, &byte);
    ASSERT_THROWS(stb::image::hstack({&wide, &wide, &wide}), stb::limit_error);  // does not fit in 2 GiB
}

UTEST(libstb_blend_tests, test_stack_of_one_image_is_a_copy) {
    const stb::image a = gradient(5, 4, 3);
    const stb::image r = stb::image::hstack({&a});
    ASSERT_TRUE(same(a, r));
    ASSERT_TRUE(a.data() != r.data());
}

// ---------------------------------------------------------------- canvas images

UTEST(libstb_blend_tests, test_canvas_add_image_blends_in_order_with_mode_and_opacity) {
    const stb::image base = solid(4, 4, 3, 200);
    const stb::image logo = solid(2, 2, 3, 128);
    stb::canvas c(base);
    c.add_image(logo, 0, 0).blend(stb::blend_mode::multiply);
    c.add_image(logo, 2, 2).opacity(0.5f);
    c.add_image(solid(2, 2, 3, 255), -1, -1);  // hangs over the corner: only (0, 0) is covered
    const stb::image out = c.finish();
    ASSERT_EQ(100, out(1, 1, 0));   // multiply: 200 * 128 / 255
    ASSERT_EQ(164, out(3, 3, 0));   // halfway between 200 and 128
    ASSERT_EQ(255, out(0, 0, 0));   // the last item is on top
    ASSERT_EQ(200, out(3, 0, 0));
    ASSERT_EQ(200, base(1, 1, 0));  // the source is never changed
}

UTEST(libstb_blend_tests, test_canvas_keeps_its_own_copy_of_the_image_and_checks_its_arguments) {
    stb::canvas c(solid(3, 3, 3, 0));
    stb::canvas::image_item* item = nullptr;
    {
        stb::image temp = solid(1, 1, 3, 77);
        item = &c.add_image(temp, 1, 1);  // copied in
        temp.fill("black");                // changing it afterwards does not matter
    }
    ASSERT_THROWS(item->opacity(2.0f), std::invalid_argument);
    ASSERT_THROWS(c.add_image(stb::image(), 0, 0), std::invalid_argument);
    const stb::image out = c.finish();
    ASSERT_EQ(77, out(1, 1, 0));
    ASSERT_THROWS(item->blend(stb::blend_mode::screen), std::invalid_argument);
    ASSERT_THROWS(c.add_image(solid(1, 1, 3, 1), 0, 0), std::invalid_argument);
}
