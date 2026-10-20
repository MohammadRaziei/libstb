/**
 @file stb.h
 Single umbrella header for the libstb public API. `cmake/DynamicVersion.cmake`
 (and `version.py`, and pyproject's regex) parse the LIBSTB_VERSION_* macros
 below to derive the project version - keep them as plain
 `#define NAME NUMBER` lines.
 */

#ifndef LIBSTB_H
#define LIBSTB_H

#define LIBSTB_VERSION_MAJOR 0
#define LIBSTB_VERSION_MINOR 7
#define LIBSTB_VERSION_PATCH 0

#ifdef __cplusplus
#include "stb/env.hpp"
#include "stb/error.hpp"
#include "stb/utf8.hpp"
#include "stb/color.hpp"
#include "stb/image.hpp"
#include "stb/resizer.hpp"
#include "stb/font.hpp"
#include "stb/shape.hpp"
#include "stb/canvas.hpp"
#include "stb/zlib.hpp"
#endif

#endif  // LIBSTB_H
