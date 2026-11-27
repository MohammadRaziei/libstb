#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "stb.h"
#include "conftest.hpp"

// The in-place functions (equalize_inplace, gamma_inplace, erode_inplace, ...) give their argument back, with the
// exact type it had, so they chain; and the names of the enums they take can be parsed and printed.

namespace {

using stb::image;
using stb::mat32f;
using stb::mat8u;

mat32f floats(int w, int h, int c) {
    mat32f m(w, h, c);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < c; ++k) m(x, y, k) = float((x * 31 + y * 17 + k * 53) & 0xFF) / 255.0f;
    return m;
}

bool same_values(const mat32f& a, const mat32f& b) {
    return a.width() == b.width() && a.height() == b.height() && a.channels() == b.channels() &&
           std::equal(a.begin(), a.end(), b.begin());
}

// The call is checked twice: its type is M& (a reference to what was given, not to its base: a compile-time check)
// and its address is the address of img (`ok` goes false otherwise).
#define STB_CHECK_RETURNS_ARGUMENT(M, img, call)                       \
    do {                                                               \
        static_assert(std::is_same<decltype(call), M&>::value, #call); \
        ok = (&(call) == &(img)) && ok;                                \
    } while (0)

// True when every call gave img back.
template <class M>
bool every_inplace_function_returns_its_argument(M& img, const M& reference) {
    bool ok = true;
    const mat8u se = stb::structuring_element(stb::morph_shape::rect, 3);
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::equalize_inplace(img));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::auto_contrast_inplace(img, 1.0f));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::match_histogram_inplace(img, reference));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::clahe_inplace(img));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::gamma_inplace(img, 1.5f));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::levels_inplace(img, 0.1f, 0.9f));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::convert_color_inplace(img, stb::color_space::rgb, stb::color_space::hsv));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::pixelize_inplace(img, 2));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::unsharp_mask_inplace(img, 1.0f));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::erode_inplace(img, se));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::dilate_inplace(img, se));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::morph_open_inplace(img, se));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::morph_close_inplace(img, se));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::morph_gradient_inplace(img, se));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::top_hat_inplace(img, se));
    STB_CHECK_RETURNS_ARGUMENT(M, img, stb::black_hat_inplace(img, se));
    return ok;
}

}  // namespace

UTEST(libstb_inplace_tests, test_every_inplace_function_returns_its_image_argument_as_an_image) {
    image img = testutil::gradient(12, 9, 3);
    const image reference = testutil::gradient(7, 5, 3);
    ASSERT_TRUE(every_inplace_function_returns_its_argument(img, reference));
}

UTEST(libstb_inplace_tests, test_every_inplace_function_returns_its_float_argument_as_a_mat32f) {
    mat32f img = floats(12, 9, 3);
    const mat32f reference = floats(7, 5, 3);
    ASSERT_TRUE(every_inplace_function_returns_its_argument(img, reference));
}

UTEST(libstb_inplace_tests, test_inplace_calls_chain_and_match_the_value_forms) {
    const image src = testutil::gradient(12, 9, 3);
    image chained = src.copy();
    stb::gamma_inplace(stb::equalize_inplace(chained), 2.2f);
    const image sequential = stb::gamma(stb::equalize(src), 2.2f);
    ASSERT_TRUE(testutil::same(chained, sequential));

    const mat32f fsrc = floats(12, 9, 3);
    mat32f fchained = fsrc.copy();
    stb::gamma_inplace(stb::equalize_inplace(fchained), 2.2f);
    ASSERT_TRUE(same_values(fchained, stb::gamma(stb::equalize(fsrc), 2.2f)));
}

UTEST(libstb_inplace_tests, test_the_enums_the_inplace_functions_take_have_names) {
    for (const stb::tone_mode m : {stb::tone_mode::luma, stb::tone_mode::per_channel})
        ASSERT_TRUE(stb::tone_mode_from_name(stb::tone_mode_name(m)) == m);
    for (const stb::edge_mode e :
         {stb::edge_mode::clamp, stb::edge_mode::reflect, stb::edge_mode::mirror, stb::edge_mode::wrap, stb::edge_mode::zero})
        ASSERT_TRUE(stb::edge_mode_from_name(stb::edge_mode_name(e)) == e);
    for (const stb::color_space s : {stb::color_space::rgb, stb::color_space::hsv, stb::color_space::hsl, stb::color_space::ycbcr})
        ASSERT_TRUE(stb::color_space_from_name(stb::color_space_name(s)) == s);

    ASSERT_TRUE(stb::tone_mode_name(stb::tone_mode::per_channel) == "per_channel");
    ASSERT_TRUE(stb::edge_mode_name(stb::edge_mode::mirror) == "mirror");
    ASSERT_TRUE(stb::color_space_name(stb::color_space::ycbcr) == "ycbcr");
}

UTEST(libstb_inplace_tests, test_an_unknown_enum_name_is_an_invalid_argument_that_lists_the_valid_ones) {
    ASSERT_THROWS(stb::tone_mode_from_name("bright"), std::invalid_argument);
    ASSERT_THROWS(stb::edge_mode_from_name("Clamp"), std::invalid_argument);  // the names are exact, lower case
    ASSERT_THROWS(stb::color_space_from_name(""), std::invalid_argument);
    try {
        stb::edge_mode_from_name("nowhere");
    } catch (const std::invalid_argument& e) {
        const std::string what = e.what();
        ASSERT_TRUE(what.find("nowhere") != std::string::npos);
        ASSERT_TRUE(what.find("clamp") != std::string::npos && what.find("zero") != std::string::npos);
    }
}
