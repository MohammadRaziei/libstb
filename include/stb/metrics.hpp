#pragma once

// Metrics: how far apart two pictures are. They work on 8-bit data (mat8u, so image8u too) and on floats (mat32f,
// which is image32f); both pictures must have the same element type, size and number of channels, otherwise
// invalid_argument is thrown (so does an empty picture). Every channel counts, alpha included: this is arithmetic
// on the numbers, nothing here knows what a channel means (pass one channel at a time to look at a single one).
//
//   double e = stb::mae(a, b);        // mean absolute error, in levels (0..255) for 8 bits, in value units for floats
//   double q = stb::psnr(a, b);       // dB; +infinity when the pictures are equal
//   double s = stb::ssim(a, b);       // 1 for equal pictures
//   mat32f m = stb::ssim_map(a, b);   // the local values behind ssim(), (width - 10) x (height - 10)
//
// Floats are read as 0 to 1 (the peak of psnr is 1 for them and 255 for 8 bits); a NaN makes the result NaN. On 8
// bits the sums are exact integers; on floats they are added in double in a fixed order, so the same call gives the
// same bits on every number of threads (stb::set_threads) and on every SIMD backend.

#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

// The mean of |a - b| over all the values.
STB_API double mae(const mat8u& a, const mat8u& b);
STB_API double mae(const mat32f& a, const mat32f& b);

// The mean of (a - b)^2 over all the values.
STB_API double mse(const mat8u& a, const mat8u& b);
STB_API double mse(const mat32f& a, const mat32f& b);

// 10 * log10(peak^2 / mse(a, b)) in decibels; peak is 255 for 8 bits and 1 for floats. +infinity when mse is 0.
STB_API double psnr(const mat8u& a, const mat8u& b);
STB_API double psnr(const mat32f& a, const mat32f& b);

// The structural similarity of Wang et al. (2004): a Gaussian window of 11 x 11 values with sigma 1.5, K1 = 0.01 and
// K2 = 0.03, as scikit-image's structural_similarity(gaussian_weights=True, use_sample_covariance=False) does. The
// map has one value per window that lies completely inside the picture, so it is (width - 10) x (height - 10) with
// the channels of the input; ssim() is its mean over all positions and channels. 8-bit values are read as
// value / 255. Throws invalid_argument if the picture is smaller than 11 x 11.
STB_API mat32f ssim_map(const mat8u& a, const mat8u& b);
STB_API mat32f ssim_map(const mat32f& a, const mat32f& b);
STB_API double ssim(const mat8u& a, const mat8u& b);
STB_API double ssim(const mat32f& a, const mat32f& b);

}  // namespace stb
