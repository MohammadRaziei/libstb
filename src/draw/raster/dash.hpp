#pragma once

// Path preparation shared by the rasterizer and path::dashed: quadratic curves into cubics, and
// cutting a path into dashes.

#include <cstddef>
#include <vector>

#include "stb/path.hpp"

namespace stb::detail {

// The commands of `p` with every quad_to turned into the equal cubic_to, and every close carrying the
// start point of its contour in p[0] (so a command can be handled without remembering anything).
// Returns nothing (an empty list) when a coordinate is NaN or infinite; coordinates beyond +-1,000,000
// are limited to that.
std::vector<path_command> to_cubic_commands(const path& p);

// Whether a dash pattern can be used: at least one entry, all finite and not negative, a sum above 0.
bool valid_dashes(const float* dashes, std::size_t n) noexcept;

// `cubics` (from to_cubic_commands) cut into dashes: curves are flattened into lines (to within a quarter
// of a unit), and the result holds only move_to and line_to commands, one contour per dash. `dashes`
// must pass valid_dashes. A path that would give more than about four million commands is cut off there.
std::vector<path_command> dash_commands(const std::vector<path_command>& cubics, const float* dashes, std::size_t n,
                                        float offset);

}  // namespace stb::detail
