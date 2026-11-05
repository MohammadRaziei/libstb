#pragma once

// Turning a stb::outline into pixels: anti-aliased filling, stroking (width, caps, joins, dashes), and
// the low-level result of both, coverage spans, for callers that do their own compositing
// (gradients, patterns, masks).
//
//     stb::image img(400, 300, stb::pixel_format::rgba);
//     img.fill(stb::color(255, 255, 255));
//
//     stb::outline p;
//     p.move_to(50, 50).cubic_to(150, 0, 250, 100, 350, 50).line_to(200, 250).close();
//
//     stb::fill(img, p, stb::color(0, 120, 255));                       // the inside
//     stb::stroke(img, p, stb::color(0, 0, 0), stb::stroke_style{3.f}); // the outline, 3 px wide
//
// Every function takes the outline in its own coordinates plus an optional matrix that moves it to pixel
// positions (draw_options::transform). The edge of a stroke is computed in pixels after that, at
// 1/64 pixel precision, and its width is scaled by the matrix (the mean scale of the two axes).
// Pixel (0, 0) covers [0, 1) x [0, 1): a coordinate is the corner of a pixel, not its centre.
//
// The geometry is computed with fixed-point arithmetic and no platform-dependent rounding, so the same
// call gives the same pixels everywhere. Coordinates that are NaN or infinite make the outline draw
// nothing, and coordinates beyond +-1,000,000 are limited to that.

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "stb/color.hpp"
#include "stb/export.hpp"
#include "stb/image.hpp"
#include "stb/outline.hpp"

namespace stb {

// How overlapping and self-crossing contours decide what is inside.
//   non_zero   a point is inside if the contours wind around it a net number of times (a circle drawn
//              inside a circle in the same direction is filled, in the opposite direction it is a hole)
//   even_odd   a point is inside if a ray from it crosses an odd number of edges (the inner circle
//              is always a hole)
enum class fill_rule { non_zero, even_odd };

// The two ends of an open contour (a closed contour has none) and the corners between two segments.
enum class line_cap { butt, round, square };
enum class line_join { miter, round, bevel };

// How stroke() draws the outline of an outline.
//   width         the line's width in outline units (see the matrix note above); 0 or less draws nothing
//   cap, join     the ends and the corners
//   miter_limit   a miter join is cut to a bevel when its length would exceed miter_limit * width / 2;
//                 values below 1 act as 1
//   dashes        alternating "on" and "off" lengths in outline units: {10, 5} is 10 on, 5 off, repeated.
//                 An odd count repeats the list once more with on and off swapped ({5} is 5 on, 5 off).
//                 Empty, containing a negative or non-finite entry, or adding up to 0: a solid line.
//   dash_offset   how far into the dash pattern the line starts
struct stroke_style {
    float width = 1.f;
    line_cap cap = line_cap::butt;
    line_join join = line_join::miter;
    float miter_limit = 4.f;
    std::vector<float> dashes{};  // (an initializer, so that `stroke_style{width}` is not a missing-initializer warning)
    float dash_offset = 0.f;
};

// A run of `len` pixels starting at (x, y) with the same coverage (1..255; 0 is never reported).
struct span {
    int y = 0;
    int x = 0;
    int len = 0;
    std::uint8_t coverage = 0;
};

// A batch of spans: the rasterizer reports its result in batches, in order from top to bottom and,
// within a row, left to right, and a pixel is never reported twice.
struct span_view {
    const span* data = nullptr;
    std::size_t size = 0;
    const span* begin() const noexcept { return data; }
    const span* end() const noexcept { return data + size; }
};

// Where spans go: any callable taking a span_view. It is not stored: it only has to be alive during
// the call that receives it, so a lambda can be passed directly.
//
//     std::size_t covered = 0;
//     stb::rasterize_fill(p, stb::fill_rule::non_zero, {}, clip, [&](stb::span_view v) {
//         for (const stb::span& s : v) covered += std::size_t(s.len);
//     });
class span_sink {
public:
    template <class F, class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, span_sink>>>
    span_sink(F&& f) noexcept  // NOLINT: implicit on purpose, so a lambda converts
        : object_(const_cast<void*>(static_cast<const void*>(&f))),
          call_([](void* o, span_view v) { (*static_cast<std::remove_reference_t<F>*>(o))(v); }) {}

    void operator()(span_view v) const { call_(object_, v); }

private:
    void* object_;
    void (*call_)(void*, span_view);
};

// The part of the plane that is reported: pixels x in [x, x + w) and y in [y, y + h). Nothing outside
// it is, and nothing is computed for parts of the outline that fall outside it.
struct clip_rect {
    int x = 0, y = 0, w = 0, h = 0;
};

// --- coverage spans -------------------------------------------------------------------------------

// Reports the coverage of the inside of `p` (after `m` moved it) within `clip`. An empty outline or an
// empty clip reports nothing; `sink` is called zero or more times.
STB_API void rasterize_fill(const outline& p, fill_rule rule, const matrix& m, const clip_rect& clip, span_sink sink);

// The same for the outline of `p` drawn with `style` (see stroke_style).
STB_API void rasterize_stroke(const outline& p, const stroke_style& style, const matrix& m, const clip_rect& clip,
                              span_sink sink);

// --- drawing on an image ----------------------------------------------------------------------------

// What fill() and stroke() share.
//   transform   moves the outline to pixel positions
//   blend       how the colour mixes with what is below (see image::blend)
//   opacity     0..1, scales the colour's own alpha
struct draw_options {
    matrix transform;
    blend_mode blend = blend_mode::normal;
    float opacity = 1.f;
};

// Fills the inside of `p` with `c`, anti-aliased, blended over `img` like image::blend does with a
// layer. Throws invalid_argument on an empty image or an opacity outside 0..1. Parts of the outline
// outside the image are ignored.
STB_API void fill(image& img, const outline& p, const color& c, fill_rule rule = fill_rule::non_zero,
                  const draw_options& options = {});

// Draws the outline of `p` with `c`. Throws like fill().
STB_API void stroke(image& img, const outline& p, const color& c, const stroke_style& style = {},
                    const draw_options& options = {});

}  // namespace stb
