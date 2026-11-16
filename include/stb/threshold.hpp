#pragma once

// Thresholds: a picture of one channel turned into a mask, the mat8u of 0 (no) and 255 (yes) that morphology.hpp and
// components.hpp take. They work on 8-bit data (mat8u, so image8u too) and on floats (mat32f, which is image32f);
// any other number of channels than one is an invalid_argument. The functions are templates for the two types,
// built by libstb for std::uint8_t and for float.
//
//   mat8u mask = stb::threshold(gray, 128);                 // 255 where gray > 128
//   std::uint8_t t = stb::otsu_threshold(gray);             // the level that best splits the histogram in two
//   mat8u text = stb::adaptive_threshold(gray, 15, 5.0f, stb::adaptive_method::mean, true);
//
// threshold is the step function: out = 255 * H(src - t), H(x) being 1 for x > 0 and 0 otherwise.

#include <cstdint>

#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

// How adaptive_threshold weighs the (2 * radius + 1)-square around a value to make its local level.
enum class adaptive_method {
    mean,      // every value the same
    gaussian,  // a Gaussian window of sigma 0.3 * radius + 0.5 (the one of OpenCV for that size), the middle counts most
};

// ---- the work, for std::uint8_t and float (the functions below call these)

template <class T>
STB_API mat8u threshold_mat(const mat<T>& src, T t, bool invert);
template <class T>
STB_API T otsu_threshold_mat(const mat<T>& src);
template <class T>
STB_API mat8u adaptive_threshold_mat(const mat<T>& src, int radius, float offset, adaptive_method method, bool invert,
                                     edge_mode edge);

// 255 where src > t (a value equal to t is 0), 0 elsewhere; `invert` swaps the two (so a NaN, which is never > t, is
// 255 in the inverted mask). For an image8u or mat8u `t` is a level (0 to 255), for a mat32f a value of the 0 to 1 range.
template <class M>
traits::if_image_t<M, mat8u> threshold(const M& src, traits::mat_value_t<M> t, bool invert = false) {
    return threshold_mat<traits::mat_value_t<M>>(src, t, invert);
}

// Otsu's level: the one that makes the variance between the two classes it splits the histogram into as large as
// possible, as scikit-image's threshold_otsu (the first of equal maxima). Use it as the `t` of threshold(): the
// foreground is what is above it. An 8-bit input has one bin per level between its smallest and its largest value;
// a float input has 256 bins between its smallest and largest finite value and the result is the centre of the bin
// (values that are not finite are left out). A picture whose values are all equal gives that value. Throws
// invalid_argument for an empty picture or one without a finite value.
template <class M>
traits::if_image_t<M, traits::mat_value_t<M>> otsu_threshold(const M& src) {
    return otsu_threshold_mat<traits::mat_value_t<M>>(src);
}

// The mask of the values that stand above the local level of their neighbourhood: 255 where
//   src > local - offset
// (`invert` swaps the two), `local` being the mean or the Gaussian-weighted mean of the (2 * radius + 1)-square around
// the value, with `edge` for what lies outside. It finds dark text on an uneven background with a positive offset
// and a radius a bit larger than the strokes. `offset` is in levels for 8 bits and in values for floats. A difference
// of less than 0.00001 of the 0 to 1 range counts as equal, so a flat area is 0 when the offset is 0 and does not fill
// with noise. Throws invalid_argument for a radius outside 1 to 1000 or an offset that is not finite.
template <class M>
traits::if_image_t<M, mat8u> adaptive_threshold(const M& src, int radius, float offset = 0.0f,
                                                adaptive_method method = adaptive_method::gaussian, bool invert = false,
                                                edge_mode edge = edge_mode::clamp) {
    return adaptive_threshold_mat<traits::mat_value_t<M>>(src, radius, offset, method, invert, edge);
}

}  // namespace stb
