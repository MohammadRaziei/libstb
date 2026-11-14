#pragma once

// Tone: histograms and the adjustments of brightness and contrast built on them (equalize, auto_contrast,
// match_histogram), and gamma, levels and look-up tables. They work on 8-bit data (mat8u, so image8u too)
// and on floats (mat32f, which is image32f).
//
// The functions that know what a channel means take an image: 1 to 4 channels, which are gray, gray + alpha,
// RGB and RGBA. The alpha channel is never changed. Whatever you pass in is what you get back: an image8u
// gives an image8u, a mat8u a mat8u, a mat32f a mat32f. Floats are read as 0 = black and 1 = full
// intensity: values outside [0, 1] are treated as the nearest end of the range (the result is always in
// [0, 1]) and a NaN passes through unchanged.
//
// Every function exists in three forms (shown for equalize):
//
//   equalize(img)              a new image; img is not touched
//   equalize(std::move(img))   the result is made in img's own memory and returned: no new buffer. A
//                              temporary is a rvalue too, so equalize(gamma(img, 1.2f)) allocates once
//   equalize_inplace(img)      img itself is changed (also an image that views memory of someone else's,
//                              such as a numpy array: that memory changes)
//
// All three give the same bytes. A call that throws (a mat of 5 or more channels, a bad argument) leaves
// the image as it was. An empty image is returned as it is.
//
//   image8u img = image8u::decode(bytes, size);
//   image8u better = stb::equalize(img);                         // luma only: the colours do not shift
//   stb::auto_contrast_inplace(img, 1.0f);                       // drop the darkest and brightest 1 %
//   mat32f f = stb::equalize(stb::to_f32(img));                  // the same on floats
//   mat32i h = stb::histogram(img);                              // 256 x 1 x channels counts
//
// On 8-bit data the result is exact integer arithmetic; on floats the order of the operations is fixed, so
// the same call gives the same bits on every number of threads (stb::set_threads).

#include <type_traits>

#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

// What a function that adjusts the tones of a colour image looks at.
enum class tone_mode {
    luma,         // only the brightness (BT.601 Y of RGB) is changed and the colour is kept: the same amount
                  // is added to red, green and blue. For gray and gray + alpha it is the gray value.
    per_channel,  // every colour channel (not alpha) on its own, which can shift the colours
};

// ------------------------------------------------------------------------------------------- histograms

// How many values of each level there are, one channel of the result for each channel of src (alpha
// included): a 256 x 1 x channels mat32i, (level, 0, channel). An empty src gives an empty mat32i. Throws
// invalid_argument for more than 2^31 - 1 cells.
STB_API mat32i histogram(const mat8u& src);

// The same for floats: `bins` equal bins over [0, 1] (the last one includes 1) and a bins x 1 x channels
// result. Values outside [0, 1] count in the first or last bin, NaNs are not counted. Throws
// invalid_argument for bins below 1 or above 65536.
STB_API mat32i histogram(const mat32f& src, int bins = 256);

// ----------------------------------------------------------------------------------- look-up tables (8-bit)

// A 256 x 1 x 1 table of 255 * (i / 255) ^ (1 / g), rounded: g above 1 brightens, below 1 darkens.
// Throws invalid_argument unless g is a positive finite number.
STB_API mat8u gamma_lut(float g);

// A 256 x 1 x 1 table of the levels adjustment of an image editor: the input range [in_black, in_white] is
// stretched to [0, 1] (and clamped), raised to 1 / g, then mapped to [out_black, out_white]. All
// levels are fractions of the full range (0.1 is 25.5 of 255). Throws invalid_argument unless
// 0 <= in_black < in_white <= 1, 0 <= out_black, out_white <= 1 and g is a positive finite number.
STB_API mat8u levels_lut(float in_black, float in_white, float g = 1.0f, float out_black = 0.0f,
                         float out_white = 1.0f);

// src with every value v replaced by the table's entry v. A lut with one channel is used for all channels of
// src (alpha too: this is plain arithmetic on the numbers, nothing here knows what a channel is), a lut with
// as many channels as src has one table for each. The lut is 256 x 1. Throws invalid_argument for another
// shape.
STB_API image8u apply_lut(const mat8u& src, const mat8u& lut);

// ------------------------------------------------------------------------------------------- adjustments
namespace detail {
// The work, on the mat itself. Defined in src/image/tone.cpp; the templates below are thin wrappers.
STB_API void equalize_core(mat8u& img, tone_mode mode);
STB_API void equalize_core(mat32f& img, tone_mode mode);
STB_API void auto_contrast_core(mat8u& img, float cutoff_percent, tone_mode mode);
STB_API void auto_contrast_core(mat32f& img, float cutoff_percent, tone_mode mode);
STB_API void match_histogram_core(mat8u& img, const mat8u& reference, tone_mode mode);
STB_API void match_histogram_core(mat32f& img, const mat32f& reference, tone_mode mode);
STB_API void gamma_core(mat8u& img, float g);
STB_API void gamma_core(mat32f& img, float g);
STB_API void levels_core(mat8u& img, float in_black, float in_white, float g, float out_black, float out_white);
STB_API void levels_core(mat32f& img, float in_black, float in_white, float g, float out_black, float out_white);
}  // namespace detail

// ---- equalize: the levels are spread so that each one is used about as often (the cumulative histogram becomes
// a straight line). A picture whose values are all equal is returned unchanged. On floats the histogram has 256
// bins and the mapping is the cumulative histogram, interpolated between the bin edges.
template <class M>
traits::if_image_t<M, void> equalize_inplace(M& img, tone_mode mode = tone_mode::luma) {
    static_assert(!std::is_const_v<M>, "equalize_inplace changes its argument");
    detail::equalize_core(img, mode);
}
template <class M>
traits::if_image_rvalue_t<M> equalize(M&& img, tone_mode mode = tone_mode::luma) {
    equalize_inplace(img, mode);
    return std::move(img);
}
template <class M>
traits::if_image_t<M> equalize(const M& src, tone_mode mode = tone_mode::luma) {
    return equalize(src.copy(), mode);
}

// ---- auto_contrast: the darkest and the brightest `cutoff_percent` percent of the values are dropped (they
// become 0 and full intensity), and what is left is stretched linearly over the whole range, as Pillow's
// ImageOps.autocontrast does. 0 stretches from the smallest to the largest value. Throws invalid_argument
// unless 0 <= cutoff_percent < 50. A picture that has a single value left is returned unchanged.
template <class M>
traits::if_image_t<M, void> auto_contrast_inplace(M& img, float cutoff_percent = 0.0f,
                                                  tone_mode mode = tone_mode::luma) {
    static_assert(!std::is_const_v<M>, "auto_contrast_inplace changes its argument");
    detail::auto_contrast_core(img, cutoff_percent, mode);
}
template <class M>
traits::if_image_rvalue_t<M> auto_contrast(M&& img, float cutoff_percent = 0.0f, tone_mode mode = tone_mode::luma) {
    auto_contrast_inplace(img, cutoff_percent, mode);
    return std::move(img);
}
template <class M>
traits::if_image_t<M> auto_contrast(const M& src, float cutoff_percent = 0.0f, tone_mode mode = tone_mode::luma) {
    return auto_contrast(src.copy(), cutoff_percent, mode);
}

// ---- match_histogram: the levels are moved so that the histogram of the result follows the one of `reference`
// (its size does not matter, its element type and number of channels must be those of the image). The
// reference is not changed. Throws invalid_argument for an empty reference or another number of channels.
template <class M>
traits::if_image_t<M, void> match_histogram_inplace(M& img, const mat<traits::mat_value_t<M>>& reference,
                                                    tone_mode mode = tone_mode::luma) {
    static_assert(!std::is_const_v<M>, "match_histogram_inplace changes its argument");
    detail::match_histogram_core(img, reference, mode);
}
template <class M>
traits::if_image_rvalue_t<M> match_histogram(M&& img, const mat<traits::mat_value_t<M>>& reference,
                                             tone_mode mode = tone_mode::luma) {
    match_histogram_inplace(img, reference, mode);
    return std::move(img);
}
template <class M>
traits::if_image_t<M> match_histogram(const M& src, const mat<traits::mat_value_t<M>>& reference,
                                      tone_mode mode = tone_mode::luma) {
    return match_histogram(src.copy(), reference, mode);
}

// ---- gamma: every colour channel c becomes c ^ (1 / g) (0 to 1): g above 1 brightens, below 1 darkens. Alpha is
// kept. Throws invalid_argument unless g is a positive finite number.
template <class M>
traits::if_image_t<M, void> gamma_inplace(M& img, float g) {
    static_assert(!std::is_const_v<M>, "gamma_inplace changes its argument");
    detail::gamma_core(img, g);
}
template <class M>
traits::if_image_rvalue_t<M> gamma(M&& img, float g) {
    gamma_inplace(img, g);
    return std::move(img);
}
template <class M>
traits::if_image_t<M> gamma(const M& src, float g) {
    return gamma(src.copy(), g);
}

// ---- levels: the input range [in_black, in_white] of every colour channel is stretched to [0, 1], raised to
// 1 / g and mapped to [out_black, out_white] (see levels_lut for the rules). Alpha is kept.
template <class M>
traits::if_image_t<M, void> levels_inplace(M& img, float in_black, float in_white, float g = 1.0f,
                                           float out_black = 0.0f, float out_white = 1.0f) {
    static_assert(!std::is_const_v<M>, "levels_inplace changes its argument");
    detail::levels_core(img, in_black, in_white, g, out_black, out_white);
}
template <class M>
traits::if_image_rvalue_t<M> levels(M&& img, float in_black, float in_white, float g = 1.0f,
                                    float out_black = 0.0f, float out_white = 1.0f) {
    levels_inplace(img, in_black, in_white, g, out_black, out_white);
    return std::move(img);
}
template <class M>
traits::if_image_t<M> levels(const M& src, float in_black, float in_white, float g = 1.0f,
                             float out_black = 0.0f, float out_white = 1.0f) {
    return levels(src.copy(), in_black, in_white, g, out_black, out_white);
}

}  // namespace stb
