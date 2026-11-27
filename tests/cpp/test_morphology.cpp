#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "stb.h"
#include "conftest.hpp"

// Morphology: the structuring elements, erode, dilate, open, close and the three differences. The window of every value
// is also taken by a plain loop and compared.

namespace {

using stb::edge_mode;
using stb::image8u;
using stb::mat32f;
using stb::mat8u;
using stb::morph_shape;
using testutil::edge_index;

const edge_mode all_modes[] = {edge_mode::clamp, edge_mode::reflect, edge_mode::mirror, edge_mode::wrap, edge_mode::zero};

image8u patterned8(int w, int h, stb::pixel_format f, std::uint32_t seed) {
    image8u m(w, h, f);
    std::uint32_t s = seed;
    for (std::uint8_t& v : m) {
        s = s * 1664525u + 1013904223u;
        v = std::uint8_t(s >> 24);
    }
    return m;
}

mat32f patterned_f(int w, int h, int c, std::uint32_t seed) {
    mat32f m(w, h, c);
    std::uint32_t s = seed;
    for (float& v : m) {
        s = s * 1664525u + 1013904223u;
        v = float((s >> 8) & 0xFFFF) / 65535.0f;
    }
    return m;
}

bool same_bytes(const mat8u& a, const mat8u& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}
bool same_bits(const mat32f& a, const mat32f& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::memcmp(a.data(), b.data(), a.size_bytes()) == 0;
}

int members_of(const mat8u& se) {
    int n = 0;
    for (std::uint8_t v : se) n += v != 0;
    return n;
}

// The smallest or largest value under the members, by a loop over the window.
template <class M>
auto window(const M& m, const mat8u& se, int x, int y, int k, edge_mode mode, bool take_max) {
    using T = typename M::value_type;
    bool any = false;
    T best = T(0);
    for (int j = 0; j < se.height(); ++j)
        for (int i = 0; i < se.width(); ++i) {
            if (se(i, j, 0) == 0) continue;
            const int sx = edge_index(x + i - se.width() / 2, m.width(), mode);
            const int sy = edge_index(y + j - se.height() / 2, m.height(), mode);
            const T v = (sx < 0 || sy < 0) ? T(0) : m(sx, sy, k);
            if (!any || (take_max ? v > best : v < best)) best = v;
            any = true;
        }
    return best;
}

// a custom element: a 5 x 3 rectangle with two holes
mat8u holey() {
    mat8u se = stb::structuring_element(morph_shape::rect, 5, 3);
    se(1, 1, 0) = 0;
    se(3, 0, 0) = 0;
    return se;
}

// a 9 x 9 black picture with the 5 x 5 square of 255 at (2, 2)
image8u square_picture() {
    image8u m(9, 9, stb::pixel_format::gray, std::vector<std::uint8_t>(81, 0));
    for (int y = 2; y < 7; ++y)
        for (int x = 2; x < 7; ++x) m(x, y, 0) = 255;
    return m;
}

int count_255(const mat8u& m) {
    int n = 0;
    for (std::uint8_t v : m) n += v == 255;
    return n;
}

}  // namespace

// ------------------------------------------------------------------------------------------ the elements

UTEST(libstb_morphology_tests, test_the_shapes_of_the_structuring_elements) {
    const mat8u rect = stb::structuring_element(morph_shape::rect, 3);
    EXPECT_EQ(rect.width(), 3);
    EXPECT_EQ(rect.height(), 3);
    EXPECT_EQ(members_of(rect), 9);
    const mat8u wide = stb::structuring_element(morph_shape::rect, 5, 3);
    EXPECT_EQ(wide.width(), 5);
    EXPECT_EQ(wide.height(), 3);
    EXPECT_EQ(members_of(stb::structuring_element(morph_shape::cross, 5)), 9);
    EXPECT_EQ(members_of(stb::structuring_element(morph_shape::cross, 5, 3)), 7);
    // the ellipse through the middle of the sides: the 3 x 3 one is the cross, the 5 x 5 one has 13 members
    EXPECT_TRUE(same_bytes(stb::structuring_element(morph_shape::ellipse, 3), stb::structuring_element(morph_shape::cross, 3)));
    const mat8u ellipse = stb::structuring_element(morph_shape::ellipse, 5);
    EXPECT_EQ(members_of(ellipse), 13);
    EXPECT_EQ(int(ellipse(0, 0, 0)), 0);  // the corner is out
    EXPECT_EQ(int(ellipse(2, 0, 0)), 255);
    EXPECT_EQ(members_of(stb::structuring_element(morph_shape::rect, 1)), 1);
}

UTEST(libstb_morphology_tests, test_a_bad_size_of_an_element_is_refused) {
    ASSERT_THROWS(stb::structuring_element(morph_shape::rect, 4), std::invalid_argument);
    ASSERT_THROWS(stb::structuring_element(morph_shape::rect, 3, 2), std::invalid_argument);
    ASSERT_THROWS(stb::structuring_element(morph_shape::rect, -3), std::invalid_argument);
}

// -------------------------------------------------------------------------------- erode and dilate by a loop

UTEST(libstb_morphology_tests, test_erode_and_dilate_match_a_loop_over_the_window_for_every_edge_mode_and_shape) {
    const image8u u = patterned8(17, 13, stb::pixel_format::rgb, 5u);
    const mat32f f = patterned_f(15, 11, 2, 6u);
    const mat8u elements[] = {stb::structuring_element(morph_shape::rect, 3), stb::structuring_element(morph_shape::rect, 5, 3),
                              stb::structuring_element(morph_shape::cross, 5), stb::structuring_element(morph_shape::ellipse, 5),
                              stb::structuring_element(morph_shape::rect, 1), holey()};
    for (const mat8u& se : elements)
        for (edge_mode mode : all_modes) {
            const image8u eu = stb::erode(u, se, mode), du = stb::dilate(u, se, mode);
            const mat32f ef = stb::erode(f, se, mode), df = stb::dilate(f, se, mode);
            for (int y = 0; y < u.height(); ++y)
                for (int x = 0; x < u.width(); ++x)
                    for (int k = 0; k < 3; ++k) {
                        ASSERT_EQ(int(eu(x, y, k)), int(window(u, se, x, y, k, mode, false)));
                        ASSERT_EQ(int(du(x, y, k)), int(window(u, se, x, y, k, mode, true)));
                    }
            for (int y = 0; y < f.height(); ++y)
                for (int x = 0; x < f.width(); ++x)
                    for (int k = 0; k < 2; ++k) {
                        ASSERT_EQ(ef(x, y, k), window(f, se, x, y, k, mode, false));
                        ASSERT_EQ(df(x, y, k), window(f, se, x, y, k, mode, true));
                    }
        }
}

UTEST(libstb_morphology_tests, test_the_window_is_not_larger_than_the_picture) {
    const image8u u = patterned8(4, 3, stb::pixel_format::gray, 7u);
    const mat8u se = stb::structuring_element(morph_shape::rect, 9);
    for (edge_mode mode : all_modes) {
        const image8u e = stb::erode(u, se, mode);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 4; ++x) ASSERT_EQ(int(e(x, y, 0)), int(window(u, se, x, y, 0, mode, false)));
    }
}

UTEST(libstb_morphology_tests, test_with_edge_zero_an_erosion_eats_the_border_and_with_clamp_it_does_not) {
    const image8u white(7, 7, stb::pixel_format::gray, std::vector<std::uint8_t>(49, 255));
    const mat8u se = stb::structuring_element(morph_shape::rect, 3);
    EXPECT_EQ(count_255(stb::erode(white, se)), 49);
    EXPECT_EQ(count_255(stb::erode(white, se, edge_mode::zero)), 25);
}

// ------------------------------------------------------------------------------------------- the derived ones

UTEST(libstb_morphology_tests, test_open_removes_a_speck_and_close_fills_a_hole) {
    const mat8u se = stb::structuring_element(morph_shape::rect, 3);
    image8u speck = square_picture();
    speck(0, 8, 0) = 255;  // a speck of one value
    const image8u opened = stb::morph_open(speck, se);
    EXPECT_EQ(int(opened(0, 8, 0)), 0);
    EXPECT_EQ(count_255(opened), 25);  // the square is whole
    image8u holed = square_picture();
    holed(4, 4, 0) = 0;  // a hole of one value
    const image8u closed = stb::morph_close(holed, se);
    EXPECT_EQ(int(closed(4, 4, 0)), 255);
    EXPECT_EQ(count_255(closed), 25);
}

UTEST(libstb_morphology_tests, test_the_gradient_is_the_outline_and_the_hats_show_the_small_parts) {
    const mat8u se = stb::structuring_element(morph_shape::rect, 3);
    // the 5 x 5 square grows to 7 x 7 and shrinks to 3 x 3: the outline has 49 - 9 values
    EXPECT_EQ(count_255(stb::morph_gradient(square_picture(), se)), 40);
    // a bright speck of 200 on 50: the top hat shows it, with its height over the background
    image8u bright(9, 9, stb::pixel_format::gray, std::vector<std::uint8_t>(81, 50));
    bright(4, 4, 0) = 200;
    const image8u top = stb::top_hat(bright, se);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) ASSERT_EQ(int(top(x, y, 0)), (x == 4 && y == 4) ? 150 : 0);
    // a dark hole of 0 in 100: the black hat shows it
    image8u dark(9, 9, stb::pixel_format::gray, std::vector<std::uint8_t>(81, 100));
    dark(4, 4, 0) = 0;
    const image8u black = stb::black_hat(dark, se);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) ASSERT_EQ(int(black(x, y, 0)), (x == 4 && y == 4) ? 100 : 0);
}

UTEST(libstb_morphology_tests, test_the_differences_never_wrap_below_zero_at_the_borders) {
    // with clamp the opening of a picture can be above it at a border: the hats must stay 0, not 255
    const image8u u = patterned8(21, 17, stb::pixel_format::gray, 8u);
    const mat8u se = stb::structuring_element(morph_shape::ellipse, 5);
    for (edge_mode mode : all_modes) {
        const image8u top = stb::top_hat(u, se, mode), black = stb::black_hat(u, se, mode), grad = stb::morph_gradient(u, se, mode);
        const image8u op = stb::morph_open(u, se, mode), cl = stb::morph_close(u, se, mode);
        for (int y = 0; y < u.height(); ++y)
            for (int x = 0; x < u.width(); ++x) {
                ASSERT_EQ(int(top(x, y, 0)), std::max(0, int(u(x, y, 0)) - int(op(x, y, 0))));
                ASSERT_EQ(int(black(x, y, 0)), std::max(0, int(cl(x, y, 0)) - int(u(x, y, 0))));
                ASSERT_GE(int(grad(x, y, 0)), 0);
            }
    }
}

UTEST(libstb_morphology_tests, test_the_three_forms_agree_and_the_source_is_left_alone) {
    const image8u src = patterned8(23, 19, stb::pixel_format::rgb, 9u);
    const image8u keep(src.copy());
    const mat8u se = stb::structuring_element(morph_shape::cross, 5);
    const image8u copy_form = stb::erode(src, se);
    EXPECT_TRUE(same_bytes(src, keep));
    image8u moved = stb::erode(src.copy(), se);
    image8u inplace(src.copy());
    stb::erode_inplace(inplace, se);
    EXPECT_TRUE(same_bytes(copy_form, moved));
    EXPECT_TRUE(same_bytes(copy_form, inplace));
    image8u opened(src.copy());
    stb::morph_open_inplace(opened, se);
    EXPECT_TRUE(same_bytes(opened, stb::morph_open(src, se)));
    const mat32f f = patterned_f(12, 12, 3, 10u);
    mat32f fi(f.copy());
    stb::black_hat_inplace(fi, se, edge_mode::mirror);
    EXPECT_TRUE(same_bits(fi, stb::black_hat(f, se, edge_mode::mirror)));
}

UTEST(libstb_morphology_tests, test_any_number_of_channels_and_bad_elements) {
    mat8u many(6, 5, 5);
    for (std::size_t i = 0; i < many.size(); ++i) many.data()[i] = std::uint8_t(i * 7);
    const mat8u se = stb::structuring_element(morph_shape::rect, 3);
    EXPECT_EQ(stb::erode(many, se).channels(), 5);
    mat8u two_channels(3, 3, 2), even(4, 3, 1, std::vector<std::uint8_t>(12, 255)), none(3, 3, 1, std::vector<std::uint8_t>(9, 0));
    ASSERT_THROWS(stb::erode(many, two_channels), std::invalid_argument);
    ASSERT_THROWS(stb::dilate(many, even), std::invalid_argument);
    ASSERT_THROWS(stb::morph_open(many, none), std::invalid_argument);
    ASSERT_THROWS(stb::erode(many, mat8u()), std::invalid_argument);
    ASSERT_THROWS(stb::erode(image8u(), none), std::invalid_argument);  // the element is checked even for an empty picture
    EXPECT_TRUE(stb::erode(image8u(), se).empty());
    mat8u untouched(many.copy());
    ASSERT_THROWS(stb::erode_inplace(untouched, none), std::invalid_argument);
    EXPECT_TRUE(same_bytes(untouched, many));
}

UTEST(libstb_morphology_tests, test_the_result_does_not_depend_on_the_number_of_threads) {
    const image8u src = patterned8(500, 400, stb::pixel_format::rgb, 11u);
    const mat32f f = patterned_f(300, 200, 3, 12u);
    const mat8u se = stb::structuring_element(morph_shape::ellipse, 7), box = stb::structuring_element(morph_shape::rect, 9, 5);
    image8u one_u;
    mat32f one_f;
    {
        stb::thread_scope scope(1);
        one_u = stb::morph_gradient(src, se);
        one_f = stb::morph_close(f, box, edge_mode::reflect);
    }
    stb::thread_scope scope(4);
    EXPECT_TRUE(same_bytes(stb::morph_gradient(src, se), one_u));
    EXPECT_TRUE(same_bits(stb::morph_close(f, box, edge_mode::reflect), one_f));
}
