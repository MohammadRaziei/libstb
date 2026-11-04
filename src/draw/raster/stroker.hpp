#pragma once

#include "draw/raster/outline.hpp"
#include "stb/raster.hpp"

namespace stb::detail {

// The outline of the stroke of `in`: for every contour, a closed contour going along both sides of
// the line (an open contour with its caps, a closed contour as two rings) that, filled with the
// non-zero rule, is the stroked line. Following FreeType's stroker: the two sides are built as
// offset curves, so curves stay curves (cubics) and corners get their join.
//   radius        half the line's width, 26.6
//   miter_limit   16.16: how far a miter may reach, in half-widths, before it is cut to a bevel
// Everything is fixed point. Returns an empty outline when `in` is malformed.
outline stroke_outline(const outline& in, fixed radius, line_cap cap, line_join join, fixed miter_limit);

}  // namespace stb::detail
