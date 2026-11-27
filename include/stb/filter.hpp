#pragma once

// Filters: convolution, blurs, gradients, the median, sharpening and pixelizing. They work on 8-bit data (mat8u, so image8u too) and on
// floats (mat32f, which is image32f); an 8-bit input gives an image8u, a float one gives a mat32f. Every
// filter is applied to each channel on its own, returns a new array of the same size and channels, and
// uses all the cores when the work is big enough (stb::set_threads), with the same result on every
// number of threads and on every SIMD backend (stb::set_simd).
//
// What lies outside the array is decided by an edge_mode (mat.hpp); the default is clamp.
//
//   image8u img = image8u::decode(bytes, size);
//   image8u soft = stb::gaussian_blur(img, 2.0f);
//   stb::gradients g = stb::sobel(img);               // floats: 8-bit values are read as value / 255
//   mat32f edges = stb::magnitude(g);
//   mat32f k = {{0, -1, 0}, {-1, 5, -1}, {0, -1, 0}}; // any kernel is a mat32f
//   image8u sharp = stb::convolve(img, k);
//
// On 8-bit data the arithmetic is done in floats and rounded once at the end (cast<std::uint8_t>), so a chain of
// filters is best written on mat32f: cast<float> first, cast<std::uint8_t> last.

#include <type_traits>
#include <utility>

#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/mat.hpp"

namespace stb {

// The horizontal and vertical derivative of an image (see sobel()), one value per input value.
struct gradients {
    mat32f dx;  // d/dx: positive where the values grow to the right
    mat32f dy;  // d/dy: positive where the values grow downwards
};

// The taps of a Gaussian of standard deviation `sigma`, as a 1 x N mat32f that sums to 1 (N = 2 * radius
// + 1). radius 0 means ceil(3 * sigma), where the tail is below 0.2 % of the peak. Use it as both kernels
// of the separable convolve() or on its own. Throws invalid_argument for a sigma that is not a positive
// finite number, a negative radius or a kernel too large to be sensible.
STB_API mat32f gaussian_kernel(float sigma, int radius = 0);

// The kernel is laid over each pixel as it is (a correlation: not flipped, like OpenCV's filter2D and
// Pillow's Kernel; for the usual symmetric kernels there is no difference) and the products are
// summed. Its width and height must be odd (the centre is the anchor) and it must have one channel.
// Throws invalid_argument otherwise. Costs width * height multiplications per value.
STB_API mat32f convolve(const mat32f& src, const mat32f& kernel, edge_mode edge = edge_mode::clamp);
STB_API image8u convolve(const mat8u& src, const mat32f& kernel, edge_mode edge = edge_mode::clamp);

// The same for a kernel that is a product of a row and a column: `row_kernel` along x, then `col_kernel`
// along y. Much cheaper (width + height multiplications per value). Each is a list of taps in a mat32f
// with one channel and a width or a height of 1 (either orientation), and an odd length.
STB_API mat32f convolve(const mat32f& src, const mat32f& row_kernel, const mat32f& col_kernel,
                        edge_mode edge = edge_mode::clamp);
STB_API image8u convolve(const mat8u& src, const mat32f& row_kernel, const mat32f& col_kernel,
                         edge_mode edge = edge_mode::clamp);

// Gaussian blur: convolve() with gaussian_kernel(sigma) along both axes. Throws invalid_argument for
// a sigma that is not a positive finite number.
STB_API mat32f gaussian_blur(const mat32f& src, float sigma, edge_mode edge = edge_mode::clamp);
STB_API image8u gaussian_blur(const mat8u& src, float sigma, edge_mode edge = edge_mode::clamp);

// The mean of the (2 * radius + 1)-square around each value; the cost does not depend on the radius.
// radius 0 returns a copy; a negative radius throws invalid_argument. (An 8-bit input goes through a
// float copy.)
STB_API mat32f box_blur(const mat32f& src, int radius, edge_mode edge = edge_mode::clamp);
STB_API image8u box_blur(const mat8u& src, int radius, edge_mode edge = edge_mode::clamp);

// The Sobel derivatives: the kernels {-1, 0, 1} (the difference) times {1, 2, 1} (a light smoothing
// across), so a step of height 1 gives 4, not 1. 8-bit values are read as value / 255.
STB_API gradients sobel(const mat32f& src, edge_mode edge = edge_mode::clamp);
STB_API gradients sobel(const mat8u& src, edge_mode edge = edge_mode::clamp);

// The Scharr derivatives: like sobel() with {3, 10, 3} as the smoothing across (so a step of height 1 gives 16), which
// is more exact for the direction of an edge.
STB_API gradients scharr(const mat32f& src, edge_mode edge = edge_mode::clamp);
STB_API gradients scharr(const mat8u& src, edge_mode edge = edge_mode::clamp);

// sqrt(dx^2 + dy^2) for each value of g. Throws invalid_argument if dx and dy differ in size or channels.
STB_API mat32f magnitude(const gradients& g);


// ------------------------------------------------------------------------------------------ median, sharpen, pixelize

// The median of the (2 * radius + 1)-square around each value, per channel: it removes isolated noise (salt and pepper)
// and keeps edges. radius must be 1 to 1000, else invalid_argument. An 8-bit input costs about the same for every
// radius (a sliding histogram); on floats the cost grows with radius^2 per value. On floats a NaN sorts after every
// number, so a window that is more than half NaN gives NaN.
STB_API mat32f median_filter(const mat32f& src, int radius, edge_mode edge = edge_mode::clamp);
STB_API image8u median_filter(const mat8u& src, int radius, edge_mode edge = edge_mode::clamp);

// ---- bilateral_filter: smooths without blurring edges. Every colour channel becomes the weighted mean of the
// (2 * radius + 1)-square around it, where a neighbour weighs exp(-r^2 / (2 sigma_space^2)) (r its distance in pixels)
// times exp(-d^2 / (2 sigma_range^2)); d is how different its colour is from the middle pixel's, taken over all the
// colour channels together, in units of the full range (0 to 1: the same on 8 bits and on floats). So a neighbour across
// an edge, whose colour is far, hardly counts. The alpha channel is copied (1 to 4 channels: gray, gray + alpha, RGB,
// RGBA; more throw invalid_argument). radius 0 changes nothing; radius must be 0 to 1000 and both sigmas positive and
// finite, else invalid_argument. The cost grows with radius^2 per value. On floats a neighbour that is NaN is left out
// and a NaN pixel stays NaN.
STB_API mat32f bilateral_filter(const mat32f& src, int radius, float sigma_space, float sigma_range, edge_mode edge = edge_mode::clamp);
STB_API image8u bilateral_filter(const mat8u& src, int radius, float sigma_space, float sigma_range, edge_mode edge = edge_mode::clamp);

// ---- guided_filter: smooths like a bilateral filter, keeping the edges of a guide, at a cost that does not depend on
// the radius (box means only). In every (2 * radius + 1)-square the output is a linear function of the guide that fits
// the image as well as it can, a penalty `eps` keeping the slope down, and the lines of the squares that cover a pixel are
// averaged. Where the guide is flat the output is the mean of the window means (a blur about twice as wide as a box blur
// of the same radius); where the guide has an edge larger than sqrt(eps) the edge stays. eps is in units of the full range squared (0 to 1
// on 8 bits and on floats: 1e-3 is a gentle value). The guide is the image itself, or `guide`: as wide and as high as the
// image, gray (1 or 2 channels, the second is alpha and ignored) or colour (3 or 4 channels); a colour guide uses its
// 3 x 3 covariance. Every colour channel of src is filtered, alpha is copied. radius 0 changes nothing; radius must be
// 0 to 1000 and eps positive and finite, else invalid_argument, as for a guide of another size or more than 4 channels.
// NaN is not handled: it spreads through the squares that cover it.
STB_API mat32f guided_filter(const mat32f& src, int radius, float eps, edge_mode edge = edge_mode::clamp);
STB_API mat32f guided_filter(const mat32f& src, const mat32f& guide, int radius, float eps, edge_mode edge = edge_mode::clamp);
STB_API image8u guided_filter(const mat8u& src, int radius, float eps, edge_mode edge = edge_mode::clamp);
STB_API image8u guided_filter(const mat8u& src, const mat8u& guide, int radius, float eps, edge_mode edge = edge_mode::clamp);

// The functions below take an image (1 to 4 channels: gray, gray + alpha, RGB, RGBA) and give back what they were
// given (see tone.hpp for the three forms and the rules). The alpha channel is never changed. Floats are read as 0 to 1.
// The work is done by the *_inplace templates, which libstb has built for std::uint8_t and for float.

namespace detail {
template <class T>
STB_API traits::if_image_t<mat<T>, void> unsharp_mask_inplace_mat(mat<T>& img, float sigma, float amount,
                                                                  float threshold, edge_mode edge);
template <class T, class = std::enable_if_t<is_wide_v<T>>>
void unsharp_mask_inplace_mat(mat<T>& img, float sigma, float amount, float threshold, edge_mode edge) {
    via_float_inplace(img, [&](mat32f& f) { unsharp_mask_inplace_mat<float>(f, sigma, amount, threshold, edge); });
}
}  // namespace detail

// ---- unsharp_mask: out = src + amount * (src - blur), where blur is gaussian_blur(src, sigma, edge), and a value whose
// difference |src - blur| is below `threshold` (a fraction of the whole range, 0 to 1) is left alone. The result is
// clamped to [0, 1] (a NaN stays NaN). amount 1 is "100 %" of Pillow's UnsharpMask; 0 changes nothing. Throws
// invalid_argument for a sigma that is not a positive finite number, an amount that is not finite and >= 0, or a
// threshold outside [0, 1]. Needs a float copy of the image while it works.
template <class M>
traits::if_pixel_mutable_t<M> unsharp_mask_inplace(M& img, float sigma, float amount = 1.0f, float threshold = 0.0f,
                                               edge_mode edge = edge_mode::clamp) {
    detail::unsharp_mask_inplace_mat<traits::mat_value_t<M>>(img, sigma, amount, threshold, edge);
    return img;
}

template <class M>
traits::if_pixel_rvalue_t<M> unsharp_mask(M&& img, float sigma, float amount = 1.0f, float threshold = 0.0f,
                                          edge_mode edge = edge_mode::clamp) {
    unsharp_mask_inplace(img, sigma, amount, threshold, edge);
    return std::move(img);
}
template <class M>
traits::if_pixel_t<M> unsharp_mask(const M& src, float sigma, float amount = 1.0f, float threshold = 0.0f,
                                   edge_mode edge = edge_mode::clamp) {
    return unsharp_mask(src.copy(), sigma, amount, threshold, edge);
}

namespace detail {
template <class T>
STB_API traits::if_image_t<mat<T>, void> pixelize_inplace_mat(mat<T>& img, int block_w, int block_h);
template <class T, class = std::enable_if_t<is_wide_v<T>>>
void pixelize_inplace_mat(mat<T>& img, int block_w, int block_h) {
    via_float_inplace(img, [&](mat32f& f) { pixelize_inplace_mat<float>(f, block_w, block_h); });
}
}  // namespace detail

// ---- pixelize: the picture is cut into blocks of block_w x block_h values (block_h 0 means the same as block_w),
// starting at the top left, and every block is filled with its mean (the blocks at the right and bottom edges are
// smaller and only use their own values). The colour of gray + alpha and RGBA images is the mean weighted by alpha
// (a transparent pixel does not tint its block; a block with no opacity at all gets the plain mean), and the alpha of
// a block becomes the plain mean of its alpha, so here the alpha channel does change. On 8 bits the means are exact
// integer arithmetic rounded to the nearest. Throws invalid_argument for block_w below 1 or block_h below 0.
template <class M>
traits::if_pixel_mutable_t<M> pixelize_inplace(M& img, int block_w, int block_h = 0) {
    detail::pixelize_inplace_mat<traits::mat_value_t<M>>(img, block_w, block_h);
    return img;
}

template <class M>
traits::if_pixel_rvalue_t<M> pixelize(M&& img, int block_w, int block_h = 0) {
    pixelize_inplace(img, block_w, block_h);
    return std::move(img);
}
template <class M>
traits::if_pixel_t<M> pixelize(const M& src, int block_w, int block_h = 0) {
    return pixelize(src.copy(), block_w, block_h);
}

namespace detail {
// The work of canny(), for std::uint8_t and float: not an API of its own, call canny().
template <class T>
STB_API mat8u canny_mat(const mat<T>& src, float low, float high, float sigma);
}  // namespace detail

namespace detail {
// canny for any pixel input: a wide image goes through float
template <class T>
mat8u canny_any(const mat<T>& src, float low, float high, float sigma) {
    if constexpr (is_wide_v<T>) return canny_mat<float>(cast<float>(src), low, high, sigma);
    else return canny_mat<T>(src, low, high, sigma);
}
}  // namespace detail

// ---- canny: the edges of a picture as a mask (a mat8u, 255 on an edge, 0 elsewhere), one value wide. A picture of 3 or
// 4 channels is read as its luma (0.299 R + 0.587 G + 0.114 B), one of 1 channel as it is; 2 or 5 and more channels
// throw invalid_argument. The steps: a Gaussian blur of `sigma` (0: none), the Sobel gradient, the thinning of its
// magnitude to the ridge along the direction of the gradient, and the hysteresis: a ridge point is an edge when its
// magnitude is at least `high`, or at least `low` and connected (8 neighbours) through such points to one that is.
// The two limits are fractions of a step from 0 to 1 across the picture, along an axis: the magnitude of Sobel divided
// by 4 (8-bit values are read as value / 255), the same for 8 bits and floats. Throws invalid_argument unless
// 0 <= low <= high and sigma is a finite number >= 0.
template <class M>
traits::if_pixel_t<M, mat8u> canny(const M& src, float low = 0.1f, float high = 0.2f, float sigma = 1.0f) {
    return detail::canny_any<traits::mat_value_t<M>>(src, low, high, sigma);
}

}  // namespace stb
