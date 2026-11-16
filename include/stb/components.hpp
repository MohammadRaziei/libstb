#pragma once

// Connected components: the pieces a mask (or a map of labels) falls apart into.
//
//   stb::components c = stb::connected_components(mask);              // 4-connected; 8 with connectivity::eight
//   for (int i = 0; i < c.count; ++i) use(c.stats[i]);                // the piece labelled i + 1
//   int piece = c.labels(x, y);                                       // 0 is the background
//
// The pieces are numbered 1, 2, ... in the order their first value is met when the picture is read row by row (from the
// top, each row from the left), as scipy.ndimage.label does. The result does not depend on the number of threads.

#include <vector>

#include "stb/export.hpp"
#include "stb/mat.hpp"

namespace stb {

// Which values touch: the four that share an edge, or those four and the four that share a corner.
enum class connectivity { four, eight };

// One piece: its number of values, its bounding box (x, y of the top left value, width and height) and its centre of
// mass (the mean of the positions of its values, a value at column x and row y being at (x, y)).
struct component_stat {
    int area = 0;
    int x = 0, y = 0, width = 0, height = 0;
    float cx = 0.0f, cy = 0.0f;
};

struct components {
    mat32i labels;                      // one channel, the size of the picture: 0 for the background, 1 .. count
    int count = 0;                      // the number of pieces
    std::vector<component_stat> stats;  // count entries: stats[i] describes the piece labelled i + 1
};

// The pieces of the non-zero values of a mask (one channel, else invalid_argument).
STB_API components connected_components(const mat8u& mask, connectivity c = connectivity::four);

// The pieces of equal label of a map (one channel, else invalid_argument): two touching values are in one piece
// when they have the same label. A negative label means "none": those values are the background (0).
STB_API components connected_components(const mat32i& labels, connectivity c = connectivity::four);

}  // namespace stb
