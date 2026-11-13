#pragma once

// Filters: convolution, blurs and gradients. They work on 8-bit data (mat8u, so image8u too) and on
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
// On 8-bit data the arithmetic is done in floats and rounded once at the end (to_8u), so a chain of
// filters is best written on mat32f: to_f32 first, to_8u last.

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

// sqrt(dx^2 + dy^2) for each value of g. Throws invalid_argument if dx and dy differ in size or channels.
STB_API mat32f magnitude(const gradients& g);

}  // namespace stb
