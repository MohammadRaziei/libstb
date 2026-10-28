#pragma once

// Internal to the JPEG decoder: the SIMD detection and the kernels jpeg.cpp calls through function
// pointers (so the SSE2 / NEON versions can be picked at run time). idct.cpp holds the inverse DCT,
// upsample.cpp the chroma upsampling and the YCbCr -> RGB conversion.

#include "codec/decode/formats.hpp"

#include "codec/decode/formats.hpp"

// x86/x64 detection
#if defined(__x86_64__) || defined(_M_X64)
#define STBI__X64_TARGET
#elif defined(__i386) || defined(_M_IX86)
#define STBI__X86_TARGET
#endif

#if defined(__GNUC__) && defined(STBI__X86_TARGET) && !defined(__SSE2__) && !defined(STBI_NO_SIMD)
// gcc doesn't support sse2 intrinsics unless you compile with -msse2,
// which in turn means it gets to use SSE2 everywhere. This is unfortunate,
// but previous attempts to provide the SSE2 functions with runtime
// detection caused numerous issues. The way architecture extensions are
// exposed in GCC/Clang is, sadly, not really suited for one-file libs.
// New behavior: if compiled with -msse2, we use SSE2 without any
// detection; if not, we don't use it at all.
#define STBI_NO_SIMD
#endif

#if defined(__MINGW32__) && defined(STBI__X86_TARGET) && !defined(STBI_MINGW_ENABLE_SSE2) && !defined(STBI_NO_SIMD)
// Note that __MINGW32__ doesn't actually mean 32-bit, so we have to avoid STBI__X64_TARGET
//
// 32-bit MinGW wants ESP to be 16-byte aligned, but this is not in the
// Windows ABI and VC++ as well as Windows DLLs don't maintain that invariant.
// As a result, enabling SSE2 on 32-bit MinGW is dangerous when not
// simultaneously enabling "-mstackrealign".
//
// See https://github.com/nothings/stb/issues/81 for more information.
//
// So default to no SSE2 on 32-bit MinGW. If you've read this far and added
// -mstackrealign to your build settings, feel free to #define STBI_MINGW_ENABLE_SSE2.
#define STBI_NO_SIMD
#endif

#if !defined(STBI_NO_SIMD) && (defined(STBI__X86_TARGET) || defined(STBI__X64_TARGET))
#define STBI_SSE2
#include <emmintrin.h>

#ifdef _MSC_VER

#if _MSC_VER >= 1400  // not VC6
#include <intrin.h> // __cpuid
inline int cpuid3(void)
{
   int info[4];
   __cpuid(info,1);
   return info[3];
}
#else
inline int cpuid3(void)
{
   int res;
   __asm {
      mov  eax,1
      cpuid
      mov  res,edx
   }
   return res;
}
#endif

#define STBI_SIMD_ALIGN(type, name) __declspec(align(16)) type name

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
inline int sse2_available(void)
{
   int info3 = cpuid3();
   return ((info3 >> 26) & 1) != 0;
}
#endif

#else // assume GCC-style if not VC++
#define STBI_SIMD_ALIGN(type, name) type name __attribute__((aligned(16)))

#if !defined(STBI_NO_JPEG) && defined(STBI_SSE2)
inline int sse2_available(void)
{
   // If we're even attempting to compile this on GCC/Clang, that means
   // -msse2 is on, which means the compiler is allowed to use SSE2
   // instructions at will, and so are we.
   return 1;
}
#endif

#endif
#endif

// ARM NEON


#ifndef STBI_SIMD_ALIGN
#define STBI_SIMD_ALIGN(type, name) type name
#endif


namespace stb::codec::detail {

// take a -128..127 value, clamp it and convert it to 0..255
inline byte clamp_to_byte(int x)
{
   // trick to use a single test to catch both cases
   if ((unsigned int) x > 255) {
      if (x < 0) return 0;
      if (x > 255) return 255;
   }
   return (byte) x;
}

// fast 0..255 * 0..255 => 0..255 rounded multiplication
inline byte blinn_8x8(byte x, byte y)
{
   unsigned int t = x*y + 128;
   return (byte) ((t + (t >>8)) >> 8);
}

// ---- idct.cpp
void idct_block(byte *out, int out_stride, short data[64]);
#ifdef STBI_SSE2
void idct_simd(byte *out, int out_stride, short data[64]);
#endif

// ---- upsample.cpp: one output row from the rows above and below it, for each expansion factor
typedef byte *(*resample_row_func)(byte *out, byte *in0, byte *in1, int w, int hs);

byte *resample_row_1(byte *out, byte *in_near, byte *in_far, int w, int hs);
byte *resample_row_v_2(byte *out, byte *in_near, byte *in_far, int w, int hs);
byte *resample_row_h_2(byte *out, byte *in_near, byte *in_far, int w, int hs);
byte *resample_row_hv_2(byte *out, byte *in_near, byte *in_far, int w, int hs);
#if defined(STBI_SSE2) || defined(STBI_NEON)
byte *resample_row_hv_2_simd(byte *out, byte *in_near, byte *in_far, int w, int hs);
#endif
byte *resample_row_generic(byte *out, byte *in_near, byte *in_far, int w, int hs);

void YCbCr_to_RGB_row(byte *out, const byte *y, const byte *pcb, const byte *pcr, int count, int step);
#if defined(STBI_SSE2) || defined(STBI_NEON)
void YCbCr_to_RGB_simd(byte *out, byte const *y, byte const *pcb, byte const *pcr, int count, int step);
#endif

}  // namespace stb::codec::detail
