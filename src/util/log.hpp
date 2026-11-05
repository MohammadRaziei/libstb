#pragma once

// Internal: the loggers libstb itself writes to, one per area, named libstb.<area>.
//
// The policy is docs/adr/0003-logging-policy.md. In short: DEBUG says what libstb decided or did
// (cheap, one record per call, never per pixel or per row); WARNING says libstb did something
// different from what was asked, because of the input or the machine; there is no INFO, no ERROR
// (a failure is an exception, never also a record), and no record is written from a worker thread
// of util/parallel.hpp.
//
// A logger lives for the whole process, so each function keeps its reference in a static.

#include "stb/logging.hpp"

namespace stb {
namespace detail {
namespace loggers {

inline logging::logger& decode() {
    static logging::logger& l = logging::get_logger("libstb.decode");
    return l;
}
inline logging::logger& encode() {
    static logging::logger& l = logging::get_logger("libstb.encode");
    return l;
}
inline logging::logger& resize() {
    static logging::logger& l = logging::get_logger("libstb.resize");
    return l;
}
inline logging::logger& zlib() {
    static logging::logger& l = logging::get_logger("libstb.zlib");
    return l;
}
inline logging::logger& font() {
    static logging::logger& l = logging::get_logger("libstb.font");
    return l;
}
inline logging::logger& raster() {
    static logging::logger& l = logging::get_logger("libstb.raster");
    return l;
}
inline logging::logger& simd() {
    static logging::logger& l = logging::get_logger("libstb.simd");
    return l;
}
inline logging::logger& threads() {
    static logging::logger& l = logging::get_logger("libstb.threads");
    return l;
}

}  // namespace loggers
}  // namespace detail
}  // namespace stb
