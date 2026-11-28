#pragma once

// Colour spaces: the same colour written as red, green and blue, as hue, saturation and value (HSV) or lightness
// (HSL), or as the luma and two colour differences of JPEG (YCbCr). Images of 3 channels (the colour) or 4 (the
// colour and an alpha channel that is never changed); gray and gray + alpha have no colour to convert, so they throw
// invalid_argument, like a mat of 5 or more channels.
//
// The array keeps its type and size, and the three colour channels are simply rewritten as (H, S, V), (H, S, L),
// (Y, Cb, Cr) or (R, G, B), in that order of channels:
//
//   mat32f (floats): every channel is 0 to 1. The hue is a fraction of the full turn (0 red, 1/3 green, 2/3 blue,
//                    and 1 is red again), so the hue of an input wraps instead of being clamped. Cb and Cr are
//                    stored with 0.5 added. Other channels outside [0, 1] are read as the nearest end; a NaN in
//                    a pixel makes its three colour channels NaN.
//   mat8u (8 bits):  every channel is 0 to 255, hue included: 256 steps for the full turn (round(turns * 256), mod
//                    256), so the hue of 8 bits is a multiple of 1.40625 degrees. Because of that rounding a trip
//                    through HSV or HSL and back can be off by a few levels for saturated colours (use floats when
//                    that matters); the other directions are exact to within one level.
//
// YCbCr is the full-range BT.601 transform of JPEG: Y = 0.299 R + 0.587 G + 0.114 B, Cb = 0.5 + (B - Y) / 1.772 and
// Cr = 0.5 + (R - Y) / 1.402. A grey (R = G = B) has hue 0 and saturation 0 in HSV and HSL.
//
// Every function exists in three forms, with the same rules as the tone functions (tone.hpp): a new image, an rvalue
// that is converted in its own memory, and *_inplace. The work is done by convert_color_inplace, a template that
// libstb has built for std::uint8_t and for float. It is plain scalar code (no SIMD) split over the rows
// (stb::set_threads); the result does not depend on the number of threads.
//
//   image8u hsv = stb::convert_color(img, stb::color_space::rgb, stb::color_space::hsv);
//   stb::convert_color_inplace(hsv, stb::color_space::hsv, stb::color_space::rgb);

#include <type_traits>
#include <utility>

#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

enum class color_space {
    rgb,    // red, green, blue
    hsv,    // hue, saturation, value (the largest of R, G, B)
    hsl,    // hue, saturation, lightness (the middle of the smallest and the largest)
    ycbcr,  // luma and the blue and red differences, as in JPEG
};

// The name of a colour space ("rgb", "hsv", "hsl", "ycbcr") and back. color_space_from_name throws
// std::invalid_argument for another name.
STB_API std::string_view color_space_name(color_space s) noexcept;
STB_API color_space color_space_from_name(std::string_view name);

namespace detail {
template <class T>
STB_API traits::if_image_t<mat<T>, void> convert_color_inplace_mat(mat<T>& img, color_space from, color_space to);
template <class T, class = std::enable_if_t<traits::is_emulated_pixel_v<T>>>
void convert_color_inplace_mat(mat<T>& img, color_space from, color_space to) {
    via_float_inplace(img, [&](mat32f& f) { convert_color_inplace_mat<float>(f, from, to); });
}
}  // namespace detail

// img is converted from the space `from` to the space `to` (the same space is a no-op that still checks the
// channels). Throws invalid_argument unless img has 3 or 4 channels; an empty image is left as it is, and a call
// that throws leaves img as it was.
template <class M>
traits::if_pixel_mutable_t<M> convert_color_inplace(M& img, color_space from, color_space to) {
    detail::convert_color_inplace_mat<traits::mat_value_t<M>>(img, from, to);
    return img;
}

template <class M>
traits::if_pixel_rvalue_t<M> convert_color(M&& img, color_space from, color_space to) {
    convert_color_inplace(img, from, to);
    return std::move(img);
}
template <class M>
traits::if_pixel_t<M> convert_color(const M& src, color_space from, color_space to) {
    return convert_color(src.copy(), from, to);
}

}  // namespace stb
