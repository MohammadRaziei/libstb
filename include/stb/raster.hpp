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
#include <optional>
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

// A set of spans kept for later: the coverage of a shape on the pixels, which is what a clip path or a mask is.
// It collects what rasterize_fill / rasterize_stroke report (it is a span_sink) and combines two such sets.
// The spans are in order, top to bottom and left to right within a row, each with a len >= 1 and a coverage
// of 1..255, and no pixel twice: the order a rasterizer reports them in, and what every function below relies on.
//
//     stb::span_set clip;
//     stb::rasterize_fill(circle, stb::fill_rule::non_zero, {}, area, stb::span_sink(clip));
//     stb::span_set both = stb::span_set::intersect(clip, stb::span_set::from_rect(0, 0, 64, 64));
class STB_API span_set {
public:
    span_set() = default;

    // Full coverage on x in [x, x + w) and y in [y, y + h), one span per row. Empty when w or h is not positive.
    static span_set from_rect(int x, int y, int w, int h);

    // The sink: appends the spans of a report. Throws invalid_argument for a span with len < 1 or coverage 0,
    // one that overlaps or comes before what the set holds, or one whose end does not fit an int.
    void operator()(span_view v);

    void clear() noexcept;
    bool empty() const noexcept { return spans_.empty(); }
    span_view spans() const noexcept { return {spans_.data(), spans_.size()}; }

    // The smallest box that holds every span; nullopt when the set is empty.
    std::optional<clip_rect> extents() const noexcept;

    // Whether the pixel (x, y) is covered (by any coverage). O(log n).
    bool contains(int x, int y) const noexcept;

    // The pixels both sets cover, with the coverages multiplied: (a * b) / 255, rounded down; a pixel that
    // this makes 0 is left out. O(a + b).
    static span_set intersect(const span_set& a, const span_set& b);

private:
    void append(const span& s) noexcept;  // updates the box; the caller has checked the order
    std::vector<span> spans_;
    int x1_ = 0, x2_ = 0;  // the box: first and one past the last column; the rows are the first and last span's
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
//   composite   how the alpha is shared with what is below (see composite_op). The usual
//               source_over is the plain "over". Any other operator acts only where the shape is,
//               in proportion to its coverage: a pixel half covered gets the middle between what
//               was there and what the operator makes of it, so  fill(img, circle, c) with
//               composite_op::clear cuts a smooth hole in the shape of the circle and leaves the
//               rest of the image alone (the way Skia and Cairo's bounded operators work).
struct draw_options {
    matrix transform;
    blend_mode blend = blend_mode::normal;
    float opacity = 1.f;
    composite_op composite = composite_op::source_over;
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
