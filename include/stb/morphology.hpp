#pragma once

// Morphology: erosion and dilation of a picture by a structuring element, and what is built from the two. A
// structuring element is a mat8u of one channel whose non-zero values are its members, with odd width and height; the
// middle one is the anchor (structuring_element() makes the usual shapes). The windows are laid over each value as they
// are (the element is not flipped, which makes no difference for the symmetric shapes). The functions work on 8-bit data
// (mat8u, so image8u too) and on floats (mat32f), with any number of channels, each channel on its own, and `edge` says
// what lies outside the picture (the default, clamp, makes the border neither eat nor grow: with edge_mode::zero an
// erosion eats the picture from its borders). A NaN in a float picture gives no promise about the windows it touches.
//
//   mat8u clean = stb::morph_open(mask, stb::structuring_element(stb::morph_shape::ellipse, 5));   // removes specks
//   mat8u whole = stb::morph_close(clean, stb::structuring_element(stb::morph_shape::rect, 7));    // fills small holes
//
//   erode:         the smallest value under the element        dilate:       the largest value under it
//   morph_open:    erode, then dilate (removes small bright parts)         morph_close: dilate, then erode (fills dark holes)
//   morph_gradient: dilate - erode (the outline of the shapes)
//   top_hat:       src - morph_open(src) (the small bright parts)    black_hat:    morph_close(src) - src (the small dark parts)
//
// A full rectangle costs (width + height) comparisons per value, any other element one per member. The differences are
// never below 0 (a value that would fall below is 0). Every function exists in three forms, with the rules of the tone
// functions (tone.hpp): a new image, an rvalue that is changed in its own memory (a temporary copy is used while it
// works), and *_inplace, a template that libstb has built for std::uint8_t and for float. Each throws invalid_argument
// for an element that is not odd-sized, has more than one channel or has no member.

#include <type_traits>
#include <utility>

#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

enum class morph_shape {
    rect,     // every value of the rectangle
    ellipse,  // the ellipse through the middle of the four sides (a 3 x 3 one is the cross)
    cross,    // the middle row and the middle column
};

// The element of a shape, width x height values (height 0 means a square); both must be odd and positive, else
// invalid_argument. Its members are 255 and the rest 0.
STB_API mat8u structuring_element(morph_shape shape, int width, int height = 0);

// The work of the five operations that are not made of others, for std::uint8_t and float: call the functions below.
namespace detail {
template <class T>
STB_API traits::if_image_t<mat<T>, void> erode_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge);
template <class T, class = std::enable_if_t<is_wide_v<T>>>
void erode_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    via_float_inplace(img, [&](mat32f& f) { erode_inplace_mat<float>(f, se, edge); });
}
template <class T>
STB_API traits::if_image_t<mat<T>, void> dilate_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge);
template <class T, class = std::enable_if_t<is_wide_v<T>>>
void dilate_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    via_float_inplace(img, [&](mat32f& f) { dilate_inplace_mat<float>(f, se, edge); });
}
template <class T>
STB_API traits::if_image_t<mat<T>, void> morph_gradient_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge);
template <class T, class = std::enable_if_t<is_wide_v<T>>>
void morph_gradient_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    via_float_inplace(img, [&](mat32f& f) { morph_gradient_inplace_mat<float>(f, se, edge); });
}
template <class T>
STB_API traits::if_image_t<mat<T>, void> top_hat_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge);
template <class T, class = std::enable_if_t<is_wide_v<T>>>
void top_hat_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    via_float_inplace(img, [&](mat32f& f) { top_hat_inplace_mat<float>(f, se, edge); });
}
template <class T>
STB_API traits::if_image_t<mat<T>, void> black_hat_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge);
template <class T, class = std::enable_if_t<is_wide_v<T>>>
void black_hat_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    via_float_inplace(img, [&](mat32f& f) { black_hat_inplace_mat<float>(f, se, edge); });
}
}  // namespace detail

template <class M>
traits::if_pixel_mutable_t<M> erode_inplace(M& img, const mat8u& se, edge_mode edge = edge_mode::clamp) {
    detail::erode_inplace_mat<traits::mat_value_t<M>>(img, se, edge);
    return img;
}

template <class M>
traits::if_pixel_mutable_t<M> dilate_inplace(M& img, const mat8u& se, edge_mode edge = edge_mode::clamp) {
    detail::dilate_inplace_mat<traits::mat_value_t<M>>(img, se, edge);
    return img;
}

template <class M>
traits::if_pixel_mutable_t<M> morph_gradient_inplace(M& img, const mat8u& se, edge_mode edge = edge_mode::clamp) {
    detail::morph_gradient_inplace_mat<traits::mat_value_t<M>>(img, se, edge);
    return img;
}

template <class M>
traits::if_pixel_mutable_t<M> top_hat_inplace(M& img, const mat8u& se, edge_mode edge = edge_mode::clamp) {
    detail::top_hat_inplace_mat<traits::mat_value_t<M>>(img, se, edge);
    return img;
}

template <class M>
traits::if_pixel_mutable_t<M> black_hat_inplace(M& img, const mat8u& se, edge_mode edge = edge_mode::clamp) {
    detail::black_hat_inplace_mat<traits::mat_value_t<M>>(img, se, edge);
    return img;
}

template <class M>
traits::if_pixel_mutable_t<M> morph_open_inplace(M& img, const mat8u& se, edge_mode edge = edge_mode::clamp) {
    erode_inplace(img, se, edge);
    dilate_inplace(img, se, edge);
    return img;
}

template <class M>
traits::if_pixel_mutable_t<M> morph_close_inplace(M& img, const mat8u& se, edge_mode edge = edge_mode::clamp) {
    dilate_inplace(img, se, edge);
    erode_inplace(img, se, edge);
    return img;
}

// The rvalue and copy forms of each operation, the same for all seven.
#define STB_MORPH_FORMS(name)                                                                  \
    template <class M>                                                                         \
    traits::if_pixel_rvalue_t<M> name(M&& img, const mat8u& se, edge_mode edge = edge_mode::clamp) { \
        name##_inplace(img, se, edge);                                                         \
        return std::move(img);                                                                 \
    }                                                                                          \
    template <class M>                                                                         \
    traits::if_pixel_t<M> name(const M& src, const mat8u& se, edge_mode edge = edge_mode::clamp) {   \
        return name(src.copy(), se, edge);                                                     \
    }
STB_MORPH_FORMS(erode)
STB_MORPH_FORMS(dilate)
STB_MORPH_FORMS(morph_open)
STB_MORPH_FORMS(morph_close)
STB_MORPH_FORMS(morph_gradient)
STB_MORPH_FORMS(top_hat)
STB_MORPH_FORMS(black_hat)
#undef STB_MORPH_FORMS

}  // namespace stb
