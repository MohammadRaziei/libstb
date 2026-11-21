#pragma once

// warp_affine and warp_perspective: a picture seen through a transform.
//
// Like the tone and morphology functions they are templates over the input type M and give back an M
// (mat8u -> mat8u, image8u -> image8u, mat32f -> mat32f), for 1 to 4 channels; every channel is sampled on its own, alpha
// included (use image8u::premultiplied() first for a picture whose colours must not leak out of transparent pixels).
//
// The transform takes a point of the SOURCE to the point of the result, in the canvas / SVG convention of stb::matrix: the
// centre of pixel (x, y) is the point (x + 0.5, y + 0.5). So the identity returns the picture as it is, and a whole-number
// translation moves it by exactly that many pixels. The function walks the pixels of the result and asks the inverse
// transform where each comes from, so the transform must be invertible.
//
//     stb::matrix m;
//     m.translate(w / 2.0f, h / 2.0f).rotate(0.3f).translate(-w / 2.0f, -h / 2.0f);   // turn about the centre (the last call acts first)
//     image8u turned = stb::warp_affine(img, m);

#include <cstdint>
#include <optional>

#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/outline.hpp"

namespace stb {

// How a value is read between pixel centres.
enum class interpolation_method : std::uint8_t {
    nearest,   // the pixel the point falls in
    bilinear,  // a mix of the 4 nearest pixels (the default)
    bicubic,   // Catmull-Rom over 4 x 4 pixels, sharper; 8-bit results are clamped to 0..255, floats are not
};

// A projective transform of the plane, as a 3 x 3 matrix stored by rows:
//     x' = (m[0] x + m[1] y + m[2]) / (m[6] x + m[7] y + m[8])
//     y' = (m[3] x + m[4] y + m[5]) / (m[6] x + m[7] y + m[8])
// The default is the identity, and a stb::matrix converts into one (an affine transform is a projective one).
struct STB_API homography {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    homography() noexcept = default;
    homography(const matrix& affine) noexcept;  // NOLINT: an affine transform is a homography, so the conversion is implicit

    // The transform that takes the four points src[i] to dst[i] (like OpenCV's getPerspectiveTransform). nullopt if the
    // points are degenerate (three on a line, or two the same), where no such transform exists.
    static std::optional<homography> from_quad(const point src[4], const point dst[4]);

    // The transform that applies `*this` first and `next` second.
    homography then(const homography& next) const noexcept;
    // The point after the transform; (NaN, NaN) where it is sent to infinity.
    point map(point p) const noexcept;
    point map(float x, float y) const noexcept { return map(point{x, y}); }
    // The inverse, or nullopt if the transform cannot be inverted.
    std::optional<homography> inverse() const noexcept;
};

namespace detail {
// The one implementation, instantiated for std::uint8_t and float. out_width or out_height 0 means the size of `src`.
template <class T>
STB_API mat<T> warp_core(const mat<T>& src, const homography& h, int out_width, int out_height, interpolation_method interpolation, edge_mode edge);
}  // namespace detail

// ---- warp_perspective: `h` takes a source point to a point of the result. out_width and out_height are the size of the result
// (0, the default, is the size of src). edge says what is read where the transform reaches outside the picture; the default
// is `zero` (not `clamp` as for the filters), because there is no data out there: transparent black on an RGBA picture.
// A pixel whose source point is at infinity is 0. Throws invalid_argument for a transform that cannot be inverted, a
// negative or too large size, or an interpolation that is not one of the enum; more than 4 channels throw too; an empty
// picture gives an empty one. The result is the same bits on every number of threads.
template <class M>
traits::if_image_t<M> warp_perspective(const M& src, const homography& h, int out_width = 0, int out_height = 0,
                                       interpolation_method interpolation = interpolation_method::bilinear, edge_mode edge = edge_mode::zero) {
    return M(detail::warp_core<traits::mat_value_t<M>>(src, h, out_width, out_height, interpolation, edge));
}

// ---- warp_affine: the same for a stb::matrix.
template <class M>
traits::if_image_t<M> warp_affine(const M& src, const matrix& m, int out_width = 0, int out_height = 0,
                                  interpolation_method interpolation = interpolation_method::bilinear, edge_mode edge = edge_mode::zero) {
    return warp_perspective(src, homography(m), out_width, out_height, interpolation, edge);
}

}  // namespace stb
