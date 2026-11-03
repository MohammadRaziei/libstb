#pragma once

// STB_API marks what the libstb shared library exports: the classes and the non-inline functions
// of the public headers. Everything else in the library is hidden (-fvisibility=hidden), so the
// decoders, the font reader and the SIMD kernels are not part of the library's interface.
//
//   building the shared library      STB_BUILDING_LIBRARY is defined: dllexport on Windows,
//                                    default visibility elsewhere.
//   using the shared library         dllimport on Windows, default visibility elsewhere.
//   sources compiled straight into   STB_STATIC_DEFINE is defined (CMake does it for the tests):
//   a target (no library to use)     STB_API is empty.

#if defined(STB_STATIC_DEFINE)
#define STB_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(STB_BUILDING_LIBRARY)
#define STB_API __declspec(dllexport)
#else
#define STB_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define STB_API __attribute__((visibility("default")))
#else
#define STB_API
#endif

// STB_MIN and STB_MAX: std::min and std::max without the Windows trap. <windows.h> defines min and
// max as macros (unless NOMINMAX is set before it), which turns std::min(a, b) into a syntax error
// in any file that happens to see that header first. These two have the same result as the std
// functions (the first argument when they are equal) but cannot be hit by that, and need no
// <algorithm>. They are macros, so each argument is evaluated twice: pass names and plain
// expressions, never calls with side effects or a costly call.
#ifndef STB_MIN
#define STB_MIN(a, b) (((b) < (a)) ? (b) : (a))
#endif
#ifndef STB_MAX
#define STB_MAX(a, b) (((a) < (b)) ? (b) : (a))
#endif
