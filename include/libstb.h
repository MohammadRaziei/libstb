/**
 @file libstb.h
 Single umbrella header for the libstb public API. `cmake/DynamicVersion.cmake`
 (and `version.py`, and pyproject's regex) parse the LIBSTB_VERSION_* macros
 below to derive the project version - keep them as plain
 `#define NAME NUMBER` lines.
 */

#ifndef LIBSTB_H
#define LIBSTB_H

#define LIBSTB_VERSION_MAJOR 0
#define LIBSTB_VERSION_MINOR 3
#define LIBSTB_VERSION_PATCH 0

#ifdef __cplusplus
#include "libstb/error.hpp"
#include "libstb/utf8.hpp"
#include "libstb/image.hpp"
#include "libstb/encoder.hpp"
#include "libstb/resizer.hpp"
#include "libstb/font.hpp"
#endif

#endif  // LIBSTB_H
