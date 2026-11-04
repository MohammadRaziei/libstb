#pragma once

#include "draw/raster/fixed_outline.hpp"
#include "stb/raster.hpp"

namespace stb::detail {

// The anti-aliasing scan converter. It follows the cell-accumulation method of FreeType's "smooth"
// rasterizer: every edge of the outline is cut at pixel boundaries and adds, to the pixels it passes
// through, the area it covers and the net height it spans ("cover"); one pass over each row then
// turns those sums into coverage values 0..255. The arithmetic is all integer, so the result is exact
// and the same on every platform.
//
// `o` is in 26.6 fixed point and in pixel space (y down). Only what falls inside `clip` is computed
// and reported; spans go to `sink` in batches, top to bottom, left to right.
void rasterize_outline(const fixed_outline& o, bool even_odd, const clip_rect& clip, span_sink sink);

}  // namespace stb::detail
