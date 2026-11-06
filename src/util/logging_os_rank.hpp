#pragma once

// Internal: how the handlers of stb/logging_os.hpp sort a level into the five severities every
// operating system log has. A level between two named ones takes the one below it (25 is info), so
// the mapping needs no table per platform: each handler turns the rank into its own constants.

#include "stb/logging.hpp"

namespace stb::logging::detail {

enum rank : int { rank_debug = 0, rank_info = 1, rank_warning = 2, rank_error = 3, rank_critical = 4 };

constexpr int severity_rank(level lv) noexcept {
    const int v = static_cast<int>(lv);
    return v >= static_cast<int>(level::critical) ? rank_critical
           : v >= static_cast<int>(level::error)  ? rank_error
           : v >= static_cast<int>(level::warning) ? rank_warning
           : v >= static_cast<int>(level::info)   ? rank_info
                                                  : rank_debug;
}

}  // namespace stb::logging::detail
