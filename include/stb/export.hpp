#pragma once

// STB_API marks what the libstb shared library exports: the classes and the non-inline functions
// of the public headers. Everything else in the library is hidden (-fvisibility=hidden), so the
// decoders, the font reader and the SIMD kernels are not part of the library's interface.
//
//   building the shared library      STB_BUILDING_LIBRARY is defined: dllexport on Windows,
//                                    default visibility elsewhere.
//   using the shared library         dllimport on Windows, default visibility elsewhere.
//   static library, or the sources   STB_STATIC_DEFINE is defined (CMake does it for you):
//   compiled straight into a target  STB_API is empty.

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
