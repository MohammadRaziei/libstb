// The SIMD abstraction (SSE2 / AVX / NEON / WASM / scalar) every kernel is written against, and the
// small sized copies that go with it (faster than memcpy for the 4-16 byte moves of the kernels).
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

  return (unsigned char) ((bias + scale*t) >> 16);
}

#ifndef STBIR_FORCE_GATHER_FILTER_SCANLINES_AMOUNT
#define STBIR_FORCE_GATHER_FILTER_SCANLINES_AMOUNT 32 // when downsampling and <= 32 scanlines of buffering, use gather. gather used down to 1/8th scaling for 25% win.
#endif

#ifndef STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS
#define STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS 4 // when threading, what is the minimum number of scanlines for a split?
#endif

#define STBIR_INPUT_CALLBACK_PADDING 3

#ifdef _M_IX86_FP
#if ( _M_IX86_FP >= 1 )
#ifndef STBIR_SSE
#define STBIR_SSE
#endif
#endif
#endif

#ifdef __TINYC__
  // tiny c has no intrinsics yet - this can become a version check if they add them
  #define STBIR_NO_SIMD
#endif

#if defined(_x86_64) || defined( __x86_64__ ) || defined( _M_X64 ) || defined(__x86_64) || defined(_M_AMD64) || defined(__SSE2__) || defined(STBIR_SSE) || defined(STBIR_SSE2)
  #ifndef STBIR_SSE2
    #define STBIR_SSE2
  #endif
  #if defined(__AVX__) || defined(STBIR_AVX2)
    #ifndef STBIR_AVX
      #ifndef STBIR_NO_AVX
        #define STBIR_AVX
      #endif
    #endif
  #endif
  #if defined(__AVX2__) || defined(STBIR_AVX2)
    #ifndef STBIR_NO_AVX2
      #ifndef STBIR_AVX2
        #define STBIR_AVX2
      #endif
      #if defined( _MSC_VER ) && !defined(__clang__)
        #ifndef STBIR_FP16C  // FP16C instructions are on all AVX2 cpus, so we can autoselect it here on microsoft - clang needs -mf16c
          #define STBIR_FP16C
        #endif
      #endif
    #endif
  #endif
  #ifdef __F16C__
    #ifndef STBIR_FP16C  // turn on FP16C instructions if the define is set (for clang and gcc)
      #define STBIR_FP16C
    #endif
  #endif
#endif

#if defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ ) || ((__ARM_NEON_FP & 4) != 0) || defined(__ARM_NEON__)
#ifndef STBIR_NEON
#define STBIR_NEON
#endif
#endif

#if defined(_M_ARM) || defined(__arm__)
#ifdef STBIR_USE_FMA
#undef STBIR_USE_FMA // no FMA for 32-bit arm on MSVC
#endif
#endif

#if defined(__wasm__) && defined(__wasm_simd128__)
#ifndef STBIR_WASM
#define STBIR_WASM
#endif
#endif

// restrict pointers for the output pointers, other loop and unroll control
#if defined( _MSC_VER ) && !defined(__clang__)
  #define STBIR_STREAMOUT_PTR( star ) star __restrict
  #define STBIR_NO_UNROLL( ptr ) __assume(ptr) // this oddly keeps msvc from unrolling a loop
  #if _MSC_VER >= 1900
    #define STBIR_NO_UNROLL_LOOP_START __pragma(loop( no_vector )) 
  #else
    #define STBIR_NO_UNROLL_LOOP_START 
  #endif
#elif defined( __clang__ )
  #define STBIR_STREAMOUT_PTR( star ) star __restrict__
  #define STBIR_NO_UNROLL( ptr ) __asm__ (""::"r"(ptr)) 
  #if ( __clang_major__ >= 4 ) || ( ( __clang_major__ >= 3 ) && ( __clang_minor__ >= 5 ) )
    #define STBIR_NO_UNROLL_LOOP_START _Pragma("clang loop unroll(disable)") _Pragma("clang loop vectorize(disable)")
  #else
    #define STBIR_NO_UNROLL_LOOP_START
  #endif 
#elif defined( __GNUC__ )
  #define STBIR_STREAMOUT_PTR( star ) star __restrict__
  #define STBIR_NO_UNROLL( ptr ) __asm__ (""::"r"(ptr))
  #if __GNUC__ >= 14
    #define STBIR_NO_UNROLL_LOOP_START _Pragma("GCC unroll 0") _Pragma("GCC novector")
  #else
    #define STBIR_NO_UNROLL_LOOP_START
  #endif
  #define STBIR_NO_UNROLL_LOOP_START_INF_FOR
#else
  #define STBIR_STREAMOUT_PTR( star ) star
  #define STBIR_NO_UNROLL( ptr )
  #define STBIR_NO_UNROLL_LOOP_START
#endif

#ifndef STBIR_NO_UNROLL_LOOP_START_INF_FOR
#define STBIR_NO_UNROLL_LOOP_START_INF_FOR STBIR_NO_UNROLL_LOOP_START
#endif

#ifdef STBIR_NO_SIMD // force simd off for whatever reason

// force simd off overrides everything else, so clear it all

#ifdef STBIR_SSE2
#undef STBIR_SSE2
#endif

#ifdef STBIR_AVX
#undef STBIR_AVX
#endif

#ifdef STBIR_NEON
#undef STBIR_NEON
#endif

#ifdef STBIR_AVX2
#undef STBIR_AVX2
#endif

#ifdef STBIR_FP16C
#undef STBIR_FP16C
#endif

#ifdef STBIR_WASM
#undef STBIR_WASM
#endif

#ifdef STBIR_SIMD
#undef STBIR_SIMD
#endif

#else // STBIR_SIMD

#ifdef STBIR_SSE2
  #include <emmintrin.h>

  #define stbir__simdf __m128
  #define stbir__simdi __m128i

  #define stbir_simdi_castf( reg ) _mm_castps_si128(reg)
  #define stbir_simdf_casti( reg ) _mm_castsi128_ps(reg)

  #define stbir__simdf_load( reg, ptr ) (reg) = _mm_loadu_ps( (float const*)(ptr) )
  #define stbir__simdi_load( reg, ptr ) (reg) = _mm_loadu_si128 ( (stbir__simdi const*)(ptr) )
  #define stbir__simdf_load1( out, ptr ) (out) = _mm_load_ss( (float const*)(ptr) )  // top values can be random (not denormal or nan for perf)
  #define stbir__simdi_load1( out, ptr ) (out) = _mm_castps_si128( _mm_load_ss( (float const*)(ptr) ))
  #define stbir__simdf_load1z( out, ptr ) (out) = _mm_load_ss( (float const*)(ptr) )  // top values must be zero
  #define stbir__simdf_frep4( fvar ) _mm_set_ps1( fvar )
  #define stbir__simdf_load1frep4( out, fvar ) (out) = _mm_set_ps1( fvar )
  #define stbir__simdf_load2( out, ptr ) (out) = _mm_castsi128_ps( _mm_loadl_epi64( (__m128i*)(ptr)) ) // top values can be random (not denormal or nan for perf)
  #define stbir__simdf_load2z( out, ptr ) (out) = _mm_castsi128_ps( _mm_loadl_epi64( (__m128i*)(ptr)) ) // top values must be zero
  #define stbir__simdf_load2hmerge( out, reg, ptr ) (out) = _mm_castpd_ps(_mm_loadh_pd( _mm_castps_pd(reg), (double*)(ptr) ))

  #define stbir__simdf_zeroP() _mm_setzero_ps()
  #define stbir__simdf_zero( reg ) (reg) = _mm_setzero_ps()

  #define stbir__simdf_store( ptr, reg )  _mm_storeu_ps( (float*)(ptr), reg )
  #define stbir__simdf_store1( ptr, reg ) _mm_store_ss( (float*)(ptr), reg )
  #define stbir__simdf_store2( ptr, reg ) _mm_storel_epi64( (__m128i*)(ptr), _mm_castps_si128(reg) )
  #define stbir__simdf_store2h( ptr, reg ) _mm_storeh_pd( (double*)(ptr), _mm_castps_pd(reg) )

  #define stbir__simdi_store( ptr, reg )  _mm_storeu_si128( (__m128i*)(ptr), reg )
  #define stbir__simdi_store1( ptr, reg ) _mm_store_ss( (float*)(ptr), _mm_castsi128_ps(reg) )
  #define stbir__simdi_store2( ptr, reg ) _mm_storel_epi64( (__m128i*)(ptr), (reg) )

  #define stbir__prefetch( ptr ) _mm_prefetch((char*)(ptr), _MM_HINT_T0 )

  #define stbir__simdi_expand_u8_to_u32(out0,out1,out2,out3,ireg) \
  { \
    stbir__simdi zero = _mm_setzero_si128(); \
    out2 = _mm_unpacklo_epi8( ireg, zero ); \
    out3 = _mm_unpackhi_epi8( ireg, zero ); \
    out0 = _mm_unpacklo_epi16( out2, zero ); \
    out1 = _mm_unpackhi_epi16( out2, zero ); \
    out2 = _mm_unpacklo_epi16( out3, zero ); \
    out3 = _mm_unpackhi_epi16( out3, zero ); \
  }

#define stbir__simdi_expand_u8_to_1u32(out,ireg) \
  { \
    stbir__simdi zero = _mm_setzero_si128(); \
    out = _mm_unpacklo_epi8( ireg, zero ); \
    out = _mm_unpacklo_epi16( out, zero ); \
  }

  #define stbir__simdi_expand_u16_to_u32(out0,out1,ireg) \
  { \
    stbir__simdi zero = _mm_setzero_si128(); \
    out0 = _mm_unpacklo_epi16( ireg, zero ); \
    out1 = _mm_unpackhi_epi16( ireg, zero ); \
  }

  #define stbir__simdf_convert_float_to_i32( i, f ) (i) = _mm_cvttps_epi32(f)
  #define stbir__simdf_convert_float_to_int( f ) _mm_cvtt_ss2si(f)
  #define stbir__simdf_convert_float_to_uint8( f ) ((unsigned char)_mm_cvtsi128_si32(_mm_cvttps_epi32(_mm_max_ps(_mm_min_ps(f,STBIR__CONSTF(STBIR_max_uint8_as_float)),_mm_setzero_ps()))))
  #define stbir__simdf_convert_float_to_short( f ) ((unsigned short)_mm_cvtsi128_si32(_mm_cvttps_epi32(_mm_max_ps(_mm_min_ps(f,STBIR__CONSTF(STBIR_max_uint16_as_float)),_mm_setzero_ps()))))

  #define stbir__simdi_to_int( i ) _mm_cvtsi128_si32(i)
  #define stbir__simdi_convert_i32_to_float(out, ireg) (out) = _mm_cvtepi32_ps( ireg )
  #define stbir__simdf_add( out, reg0, reg1 ) (out) = _mm_add_ps( reg0, reg1 )
  #define stbir__simdf_mult( out, reg0, reg1 ) (out) = _mm_mul_ps( reg0, reg1 )
  #define stbir__simdf_mult_mem( out, reg, ptr ) (out) = _mm_mul_ps( reg, _mm_loadu_ps( (float const*)(ptr) ) )
  #define stbir__simdf_mult1_mem( out, reg, ptr ) (out) = _mm_mul_ss( reg, _mm_load_ss( (float const*)(ptr) ) )
  #define stbir__simdf_add_mem( out, reg, ptr ) (out) = _mm_add_ps( reg, _mm_loadu_ps( (float const*)(ptr) ) )
  #define stbir__simdf_add1_mem( out, reg, ptr ) (out) = _mm_add_ss( reg, _mm_load_ss( (float const*)(ptr) ) )

  #ifdef STBIR_USE_FMA           // not on by default to maintain bit identical simd to non-simd
  #include <immintrin.h>
  #define stbir__simdf_madd( out, add, mul1, mul2 ) (out) = _mm_fmadd_ps( mul1, mul2, add )
  #define stbir__simdf_madd1( out, add, mul1, mul2 ) (out) = _mm_fmadd_ss( mul1, mul2, add )
  #define stbir__simdf_madd_mem( out, add, mul, ptr ) (out) = _mm_fmadd_ps( mul, _mm_loadu_ps( (float const*)(ptr) ), add )
  #define stbir__simdf_madd1_mem( out, add, mul, ptr ) (out) = _mm_fmadd_ss( mul, _mm_load_ss( (float const*)(ptr) ), add )
  #else
  #define stbir__simdf_madd( out, add, mul1, mul2 ) (out) = _mm_add_ps( add, _mm_mul_ps( mul1, mul2 ) )
  #define stbir__simdf_madd1( out, add, mul1, mul2 ) (out) = _mm_add_ss( add, _mm_mul_ss( mul1, mul2 ) )
  #define stbir__simdf_madd_mem( out, add, mul, ptr ) (out) = _mm_add_ps( add, _mm_mul_ps( mul, _mm_loadu_ps( (float const*)(ptr) ) ) )
  #define stbir__simdf_madd1_mem( out, add, mul, ptr ) (out) = _mm_add_ss( add, _mm_mul_ss( mul, _mm_load_ss( (float const*)(ptr) ) ) )
  #endif

  #define stbir__simdf_add1( out, reg0, reg1 ) (out) = _mm_add_ss( reg0, reg1 )
  #define stbir__simdf_mult1( out, reg0, reg1 ) (out) = _mm_mul_ss( reg0, reg1 )

  #define stbir__simdf_and( out, reg0, reg1 ) (out) = _mm_and_ps( reg0, reg1 )
  #define stbir__simdf_or( out, reg0, reg1 ) (out) = _mm_or_ps( reg0, reg1 )

  #define stbir__simdf_min( out, reg0, reg1 ) (out) = _mm_min_ps( reg0, reg1 )
  #define stbir__simdf_max( out, reg0, reg1 ) (out) = _mm_max_ps( reg0, reg1 )
  #define stbir__simdf_min1( out, reg0, reg1 ) (out) = _mm_min_ss( reg0, reg1 )
  #define stbir__simdf_max1( out, reg0, reg1 ) (out) = _mm_max_ss( reg0, reg1 )

  #define stbir__simdf_0123ABCDto3ABx( out, reg0, reg1 ) (out)=_mm_castsi128_ps( _mm_shuffle_epi32( _mm_castps_si128( _mm_shuffle_ps( reg1,reg0, (0<<0) + (1<<2) + (2<<4) + (3<<6) )), (3<<0) + (0<<2) + (1<<4) + (2<<6) ) )
  #define stbir__simdf_0123ABCDto23Ax( out, reg0, reg1 ) (out)=_mm_castsi128_ps( _mm_shuffle_epi32( _mm_castps_si128( _mm_shuffle_ps( reg1,reg0, (0<<0) + (1<<2) + (2<<4) + (3<<6) )), (2<<0) + (3<<2) + (0<<4) + (1<<6) ) )

  static const stbir__simdf STBIR_zeroones = { 0.0f,1.0f,0.0f,1.0f };
  static const stbir__simdf STBIR_onezeros = { 1.0f,0.0f,1.0f,0.0f };
  #define stbir__simdf_aaa1( out, alp, ones ) (out)=_mm_castsi128_ps( _mm_shuffle_epi32( _mm_castps_si128( _mm_movehl_ps( ones, alp ) ), (1<<0) + (1<<2) + (1<<4) + (2<<6) ) )
  #define stbir__simdf_1aaa( out, alp, ones ) (out)=_mm_castsi128_ps( _mm_shuffle_epi32( _mm_castps_si128( _mm_movelh_ps( ones, alp ) ), (0<<0) + (2<<2) + (2<<4) + (2<<6) ) )
  #define stbir__simdf_a1a1( out, alp, ones) (out) = _mm_or_ps( _mm_castsi128_ps( _mm_srli_epi64( _mm_castps_si128(alp), 32 ) ), STBIR_zeroones )
  #define stbir__simdf_1a1a( out, alp, ones) (out) = _mm_or_ps( _mm_castsi128_ps( _mm_slli_epi64( _mm_castps_si128(alp), 32 ) ), STBIR_onezeros )

  #define stbir__simdf_swiz( reg, one, two, three, four ) _mm_castsi128_ps( _mm_shuffle_epi32( _mm_castps_si128( reg ), (one<<0) + (two<<2) + (three<<4) + (four<<6) ) )

  #define stbir__simdi_and( out, reg0, reg1 ) (out) = _mm_and_si128( reg0, reg1 )
  #define stbir__simdi_or( out, reg0, reg1 ) (out) = _mm_or_si128( reg0, reg1 )
  #define stbir__simdi_16madd( out, reg0, reg1 ) (out) = _mm_madd_epi16( reg0, reg1 )

  #define stbir__simdf_pack_to_8bytes(out,aa,bb) \
  { \
    stbir__simdf af,bf; \
    stbir__simdi a,b; \
    af = _mm_min_ps( aa, STBIR_max_uint8_as_float ); \
    bf = _mm_min_ps( bb, STBIR_max_uint8_as_float ); \
    af = _mm_max_ps( af, _mm_setzero_ps() ); \
    bf = _mm_max_ps( bf, _mm_setzero_ps() ); \
    a = _mm_cvttps_epi32( af ); \
    b = _mm_cvttps_epi32( bf ); \
    a = _mm_packs_epi32( a, b ); \
    out = _mm_packus_epi16( a, a ); \
  }

  #define stbir__simdf_load4_transposed( o0, o1, o2, o3, ptr ) \
      stbir__simdf_load( o0, (ptr) );    \
      stbir__simdf_load( o1, (ptr)+4 );  \
      stbir__simdf_load( o2, (ptr)+8 );  \
      stbir__simdf_load( o3, (ptr)+12 ); \
      {                                  \
        __m128 tmp0, tmp1, tmp2, tmp3;   \
        tmp0 = _mm_unpacklo_ps(o0, o1);  \
        tmp2 = _mm_unpacklo_ps(o2, o3);  \
        tmp1 = _mm_unpackhi_ps(o0, o1);  \
        tmp3 = _mm_unpackhi_ps(o2, o3);  \
        o0 = _mm_movelh_ps(tmp0, tmp2);  \
        o1 = _mm_movehl_ps(tmp2, tmp0);  \
        o2 = _mm_movelh_ps(tmp1, tmp3);  \
        o3 = _mm_movehl_ps(tmp3, tmp1);  \
      }

  #define stbir__interleave_pack_and_store_16_u8( ptr, r0, r1, r2, r3 ) \
      r0 = _mm_packs_epi32( r0, r1 ); \
      r2 = _mm_packs_epi32( r2, r3 ); \
      r1 = _mm_unpacklo_epi16( r0, r2 ); \
      r3 = _mm_unpackhi_epi16( r0, r2 ); \
      r0 = _mm_unpacklo_epi16( r1, r3 ); \
      r2 = _mm_unpackhi_epi16( r1, r3 ); \
      r0 = _mm_packus_epi16( r0, r2 ); \
      stbir__simdi_store( ptr, r0 ); \

  #define stbir__simdi_32shr( out, reg, imm ) out = _mm_srli_epi32( reg, imm )

  #if defined(_MSC_VER) && !defined(__clang__)
    // msvc inits with 8 bytes
    #define STBIR__CONST_32_TO_8( v ) (char)(unsigned char)((v)&255),(char)(unsigned char)(((v)>>8)&255),(char)(unsigned char)(((v)>>16)&255),(char)(unsigned char)(((v)>>24)&255)
    #define STBIR__CONST_4_32i( v ) STBIR__CONST_32_TO_8( v ), STBIR__CONST_32_TO_8( v ), STBIR__CONST_32_TO_8( v ), STBIR__CONST_32_TO_8( v )
    #define STBIR__CONST_4d_32i( v0, v1, v2, v3 ) STBIR__CONST_32_TO_8( v0 ), STBIR__CONST_32_TO_8( v1 ), STBIR__CONST_32_TO_8( v2 ), STBIR__CONST_32_TO_8( v3 )
  #else
    // everything else inits with long long's
    #define STBIR__CONST_4_32i( v ) (long long)((((stbir_uint64)(stbir_uint32)(v))<<32)|((stbir_uint64)(stbir_uint32)(v))),(long long)((((stbir_uint64)(stbir_uint32)(v))<<32)|((stbir_uint64)(stbir_uint32)(v)))
    #define STBIR__CONST_4d_32i( v0, v1, v2, v3 ) (long long)((((stbir_uint64)(stbir_uint32)(v1))<<32)|((stbir_uint64)(stbir_uint32)(v0))),(long long)((((stbir_uint64)(stbir_uint32)(v3))<<32)|((stbir_uint64)(stbir_uint32)(v2)))
  #endif

  #define STBIR__SIMDF_CONST(var, x) stbir__simdf var = { x, x, x, x }
  #define STBIR__SIMDI_CONST(var, x) stbir__simdi var = { STBIR__CONST_4_32i(x) }
  #define STBIR__CONSTF(var) (var)
  #define STBIR__CONSTI(var) (var)

  #if defined(STBIR_AVX) || defined(__SSE4_1__)
    #include <smmintrin.h>
    #define stbir__simdf_pack_to_8words(out,reg0,reg1) out = _mm_packus_epi32(_mm_cvttps_epi32(_mm_max_ps(_mm_min_ps(reg0,STBIR__CONSTF(STBIR_max_uint16_as_float)),_mm_setzero_ps())), _mm_cvttps_epi32(_mm_max_ps(_mm_min_ps(reg1,STBIR__CONSTF(STBIR_max_uint16_as_float)),_mm_setzero_ps())))
  #else
    static STBIR__SIMDI_CONST(stbir__s32_32768, 32768);
    static STBIR__SIMDI_CONST(stbir__s16_32768, ((32768<<16)|32768));

    #define stbir__simdf_pack_to_8words(out,reg0,reg1) \
      { \
        stbir__simdi tmp0,tmp1; \
        tmp0 = _mm_cvttps_epi32(_mm_max_ps(_mm_min_ps(reg0,STBIR__CONSTF(STBIR_max_uint16_as_float)),_mm_setzero_ps())); \
        tmp1 = _mm_cvttps_epi32(_mm_max_ps(_mm_min_ps(reg1,STBIR__CONSTF(STBIR_max_uint16_as_float)),_mm_setzero_ps())); \
        tmp0 = _mm_sub_epi32( tmp0, stbir__s32_32768 ); \
        tmp1 = _mm_sub_epi32( tmp1, stbir__s32_32768 ); \
        out = _mm_packs_epi32( tmp0, tmp1 ); \
        out = _mm_sub_epi16( out, stbir__s16_32768 ); \
      }

  #endif

  #define STBIR_SIMD

  // if we detect AVX, set the simd8 defines
  #ifdef STBIR_AVX
    #include <immintrin.h>
    #define STBIR_SIMD8
    #define stbir__simdf8 __m256
    #define stbir__simdi8 __m256i
    #define stbir__simdf8_load( out, ptr ) (out) = _mm256_loadu_ps( (float const *)(ptr) )
    #define stbir__simdi8_load( out, ptr ) (out) = _mm256_loadu_si256( (__m256i const *)(ptr) )
    #define stbir__simdf8_mult( out, a, b ) (out) = _mm256_mul_ps( (a), (b) )
    #define stbir__simdf8_store( ptr, out ) _mm256_storeu_ps( (float*)(ptr), out )
    #define stbir__simdi8_store( ptr, reg )  _mm256_storeu_si256( (__m256i*)(ptr), reg )
    #define stbir__simdf8_frep8( fval ) _mm256_set1_ps( fval )

    #define stbir__simdf8_min( out, reg0, reg1 ) (out) = _mm256_min_ps( reg0, reg1 )
    #define stbir__simdf8_max( out, reg0, reg1 ) (out) = _mm256_max_ps( reg0, reg1 )

    #define stbir__simdf8_add4halves( out, bot4, top8 ) (out) = _mm_add_ps( bot4, _mm256_extractf128_ps( top8, 1 ) )
    #define stbir__simdf8_mult_mem( out, reg, ptr ) (out) = _mm256_mul_ps( reg, _mm256_loadu_ps( (float const*)(ptr) ) )
    #define stbir__simdf8_add_mem( out, reg, ptr ) (out) = _mm256_add_ps( reg, _mm256_loadu_ps( (float const*)(ptr) ) )
    #define stbir__simdf8_add( out, a, b ) (out) = _mm256_add_ps( a, b )
    #define stbir__simdf8_load1b( out, ptr ) (out) = _mm256_broadcast_ss( ptr )
    #define stbir__simdf_load1rep4( out, ptr ) (out) = _mm_broadcast_ss( ptr )  // avx load instruction

    #define stbir__simdi8_convert_i32_to_float(out, ireg) (out) = _mm256_cvtepi32_ps( ireg )
    #define stbir__simdf8_convert_float_to_i32( i, f ) (i) = _mm256_cvttps_epi32(f)

    #define stbir__simdf8_bot4s( out, a, b ) (out) = _mm256_permute2f128_ps(a,b, (0<<0)+(2<<4) )
    #define stbir__simdf8_top4s( out, a, b ) (out) = _mm256_permute2f128_ps(a,b, (1<<0)+(3<<4) )

    #define stbir__simdf8_gettop4( reg ) _mm256_extractf128_ps(reg,1)

    #ifdef STBIR_AVX2

    #define stbir__simdi8_expand_u8_to_u32(out0,out1,ireg) \
    { \
      stbir__simdi8 a, zero  =_mm256_setzero_si256();\
      a = _mm256_permute4x64_epi64( _mm256_unpacklo_epi8( _mm256_permute4x64_epi64(_mm256_castsi128_si256(ireg),(0<<0)+(2<<2)+(1<<4)+(3<<6)), zero ),(0<<0)+(2<<2)+(1<<4)+(3<<6)); \
      out0 = _mm256_unpacklo_epi16( a, zero ); \
      out1 = _mm256_unpackhi_epi16( a, zero ); \
    }

    #define stbir__simdf8_pack_to_16bytes(out,aa,bb) \
    { \
      stbir__simdi8 t; \
      stbir__simdf8 af,bf; \
      stbir__simdi8 a,b; \
      af = _mm256_min_ps( aa, STBIR_max_uint8_as_floatX ); \
      bf = _mm256_min_ps( bb, STBIR_max_uint8_as_floatX ); \
      af = _mm256_max_ps( af, _mm256_setzero_ps() ); \
      bf = _mm256_max_ps( bf, _mm256_setzero_ps() ); \
      a = _mm256_cvttps_epi32( af ); \
      b = _mm256_cvttps_epi32( bf ); \
      t = _mm256_permute4x64_epi64( _mm256_packs_epi32( a, b ), (0<<0)+(2<<2)+(1<<4)+(3<<6) ); \
      out = _mm256_castsi256_si128( _mm256_permute4x64_epi64( _mm256_packus_epi16( t, t ), (0<<0)+(2<<2)+(1<<4)+(3<<6) ) ); \
    }

    #define stbir__simdi8_expand_u16_to_u32(out,ireg) out = _mm256_unpacklo_epi16( _mm256_permute4x64_epi64(_mm256_castsi128_si256(ireg),(0<<0)+(2<<2)+(1<<4)+(3<<6)), _mm256_setzero_si256() );

    #define stbir__simdf8_pack_to_16words(out,aa,bb) \
      { \
        stbir__simdf8 af,bf; \
        stbir__simdi8 a,b; \
        af = _mm256_min_ps( aa, STBIR_max_uint16_as_floatX ); \
        bf = _mm256_min_ps( bb, STBIR_max_uint16_as_floatX ); \
        af = _mm256_max_ps( af, _mm256_setzero_ps() ); \
        bf = _mm256_max_ps( bf, _mm256_setzero_ps() ); \
        a = _mm256_cvttps_epi32( af ); \
        b = _mm256_cvttps_epi32( bf ); \
        (out) = _mm256_permute4x64_epi64( _mm256_packus_epi32(a, b), (0<<0)+(2<<2)+(1<<4)+(3<<6) ); \
      }

    #else

    #define stbir__simdi8_expand_u8_to_u32(out0,out1,ireg) \
    { \
      stbir__simdi a,zero = _mm_setzero_si128(); \
      a = _mm_unpacklo_epi8( ireg, zero ); \
      out0 = _mm256_setr_m128i( _mm_unpacklo_epi16( a, zero ), _mm_unpackhi_epi16( a, zero ) ); \
      a = _mm_unpackhi_epi8( ireg, zero ); \
      out1 = _mm256_setr_m128i( _mm_unpacklo_epi16( a, zero ), _mm_unpackhi_epi16( a, zero ) ); \
    }

    #define stbir__simdf8_pack_to_16bytes(out,aa,bb) \
    { \
      stbir__simdi t; \
      stbir__simdf8 af,bf; \
      stbir__simdi8 a,b; \
      af = _mm256_min_ps( aa, STBIR_max_uint8_as_floatX ); \
      bf = _mm256_min_ps( bb, STBIR_max_uint8_as_floatX ); \
      af = _mm256_max_ps( af, _mm256_setzero_ps() ); \
      bf = _mm256_max_ps( bf, _mm256_setzero_ps() ); \
      a = _mm256_cvttps_epi32( af ); \
      b = _mm256_cvttps_epi32( bf ); \
      out = _mm_packs_epi32( _mm256_castsi256_si128(a), _mm256_extractf128_si256( a, 1 ) ); \
      out = _mm_packus_epi16( out, out ); \
      t = _mm_packs_epi32( _mm256_castsi256_si128(b), _mm256_extractf128_si256( b, 1 ) ); \
      t = _mm_packus_epi16( t, t ); \
      out = _mm_castps_si128( _mm_shuffle_ps( _mm_castsi128_ps(out), _mm_castsi128_ps(t), (0<<0)+(1<<2)+(0<<4)+(1<<6) ) ); \
    }

    #define stbir__simdi8_expand_u16_to_u32(out,ireg) \
    { \
      stbir__simdi a,b,zero = _mm_setzero_si128(); \
      a = _mm_unpacklo_epi16( ireg, zero ); \
      b = _mm_unpackhi_epi16( ireg, zero ); \
      out = _mm256_insertf128_si256( _mm256_castsi128_si256( a ), b, 1 ); \
    }

    #define stbir__simdf8_pack_to_16words(out,aa,bb) \
      { \
        stbir__simdi t0,t1; \
        stbir__simdf8 af,bf; \
        stbir__simdi8 a,b; \
        af = _mm256_min_ps( aa, STBIR_max_uint16_as_floatX ); \
        bf = _mm256_min_ps( bb, STBIR_max_uint16_as_floatX ); \
        af = _mm256_max_ps( af, _mm256_setzero_ps() ); \
        bf = _mm256_max_ps( bf, _mm256_setzero_ps() ); \
        a = _mm256_cvttps_epi32( af ); \
        b = _mm256_cvttps_epi32( bf ); \
        t0 = _mm_packus_epi32( _mm256_castsi256_si128(a), _mm256_extractf128_si256( a, 1 ) ); \
        t1 = _mm_packus_epi32( _mm256_castsi256_si128(b), _mm256_extractf128_si256( b, 1 ) ); \
        out = _mm256_setr_m128i( t0, t1 ); \
      }

    #endif

    static __m256i stbir_00001111 = { STBIR__CONST_4d_32i( 0, 0, 0, 0 ), STBIR__CONST_4d_32i( 1, 1, 1, 1 ) };
    #define stbir__simdf8_0123to00001111( out, in ) (out) = _mm256_permutevar_ps ( in, stbir_00001111 )

    static __m256i stbir_22223333 = { STBIR__CONST_4d_32i( 2, 2, 2, 2 ), STBIR__CONST_4d_32i( 3, 3, 3, 3 ) };
    #define stbir__simdf8_0123to22223333( out, in ) (out) = _mm256_permutevar_ps ( in, stbir_22223333 )

    #define stbir__simdf8_0123to2222( out, in ) (out) = stbir__simdf_swiz(_mm256_castps256_ps128(in), 2,2,2,2 )

    #define stbir__simdf8_load4b( out, ptr ) (out) = _mm256_broadcast_ps( (__m128 const *)(ptr) )

    static __m256i stbir_00112233 = { STBIR__CONST_4d_32i( 0, 0, 1, 1 ), STBIR__CONST_4d_32i( 2, 2, 3, 3 ) };
    #define stbir__simdf8_0123to00112233( out, in ) (out) = _mm256_permutevar_ps ( in, stbir_00112233 )
    #define stbir__simdf8_add4( out, a8, b ) (out) = _mm256_add_ps( a8,  _mm256_castps128_ps256( b ) )

    static __m256i stbir_load6 = { STBIR__CONST_4_32i( 0x80000000 ), STBIR__CONST_4d_32i(  0x80000000,  0x80000000, 0, 0 ) };
    #define stbir__simdf8_load6z( out, ptr ) (out) = _mm256_maskload_ps( ptr, stbir_load6 )

    #define stbir__simdf8_0123to00000000( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (0<<0)+(0<<2)+(0<<4)+(0<<6) )
    #define stbir__simdf8_0123to11111111( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (1<<0)+(1<<2)+(1<<4)+(1<<6) )
    #define stbir__simdf8_0123to22222222( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (2<<0)+(2<<2)+(2<<4)+(2<<6) )
    #define stbir__simdf8_0123to33333333( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (3<<0)+(3<<2)+(3<<4)+(3<<6) )
    #define stbir__simdf8_0123to21032103( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (2<<0)+(1<<2)+(0<<4)+(3<<6) )
    #define stbir__simdf8_0123to32103210( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (3<<0)+(2<<2)+(1<<4)+(0<<6) )
    #define stbir__simdf8_0123to12301230( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (1<<0)+(2<<2)+(3<<4)+(0<<6) )
    #define stbir__simdf8_0123to10321032( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (1<<0)+(0<<2)+(3<<4)+(2<<6) )
    #define stbir__simdf8_0123to30123012( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (3<<0)+(0<<2)+(1<<4)+(2<<6) )

    #define stbir__simdf8_0123to11331133( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (1<<0)+(1<<2)+(3<<4)+(3<<6) )
    #define stbir__simdf8_0123to00220022( out, in ) (out) =  _mm256_shuffle_ps ( in, in, (0<<0)+(0<<2)+(2<<4)+(2<<6) )

    #define stbir__simdf8_aaa1( out, alp, ones ) (out) = _mm256_blend_ps( alp, ones, (1<<0)+(1<<1)+(1<<2)+(0<<3)+(1<<4)+(1<<5)+(1<<6)+(0<<7)); (out)=_mm256_shuffle_ps( out,out, (3<<0) + (3<<2) + (3<<4) + (0<<6) )
    #define stbir__simdf8_1aaa( out, alp, ones ) (out) = _mm256_blend_ps( alp, ones, (0<<0)+(1<<1)+(1<<2)+(1<<3)+(0<<4)+(1<<5)+(1<<6)+(1<<7)); (out)=_mm256_shuffle_ps( out,out, (1<<0) + (0<<2) + (0<<4) + (0<<6) )
    #define stbir__simdf8_a1a1( out, alp, ones) (out) = _mm256_blend_ps( alp, ones, (1<<0)+(0<<1)+(1<<2)+(0<<3)+(1<<4)+(0<<5)+(1<<6)+(0<<7)); (out)=_mm256_shuffle_ps( out,out, (1<<0) + (0<<2) + (3<<4) + (2<<6) )
    #define stbir__simdf8_1a1a( out, alp, ones) (out) = _mm256_blend_ps( alp, ones, (0<<0)+(1<<1)+(0<<2)+(1<<3)+(0<<4)+(1<<5)+(0<<6)+(1<<7)); (out)=_mm256_shuffle_ps( out,out, (1<<0) + (0<<2) + (3<<4) + (2<<6) )

    #define stbir__simdf8_zero( reg ) (reg) = _mm256_setzero_ps()

    #ifdef STBIR_USE_FMA           // not on by default to maintain bit identical simd to non-simd
    #define stbir__simdf8_madd( out, add, mul1, mul2 ) (out) = _mm256_fmadd_ps( mul1, mul2, add )
    #define stbir__simdf8_madd_mem( out, add, mul, ptr ) (out) = _mm256_fmadd_ps( mul, _mm256_loadu_ps( (float const*)(ptr) ), add )
    #define stbir__simdf8_madd_mem4( out, add, mul, ptr )(out) = _mm256_fmadd_ps( _mm256_setr_m128( mul, _mm_setzero_ps() ), _mm256_setr_m128( _mm_loadu_ps( (float const*)(ptr) ), _mm_setzero_ps() ), add )
    #else
    #define stbir__simdf8_madd( out, add, mul1, mul2 ) (out) = _mm256_add_ps( add, _mm256_mul_ps( mul1, mul2 ) )
    #define stbir__simdf8_madd_mem( out, add, mul, ptr ) (out) = _mm256_add_ps( add, _mm256_mul_ps( mul, _mm256_loadu_ps( (float const*)(ptr) ) ) )
    #define stbir__simdf8_madd_mem4( out, add, mul, ptr )  (out) = _mm256_add_ps( add, _mm256_setr_m128( _mm_mul_ps( mul, _mm_loadu_ps( (float const*)(ptr) ) ), _mm_setzero_ps() ) )
    #endif
    #define stbir__if_simdf8_cast_to_simdf4( val ) _mm256_castps256_ps128( val )

  #endif

  #ifdef STBIR_FLOORF
  #undef STBIR_FLOORF
  #endif
  #define STBIR_FLOORF stbir_simd_floorf
  static stbir__inline float stbir_simd_floorf(float x)  // martins floorf
  {
    #if defined(STBIR_AVX) || defined(__SSE4_1__) || defined(STBIR_SSE41)
    __m128 t = _mm_set_ss(x);
    return _mm_cvtss_f32( _mm_floor_ss(t, t) );
    #else
    __m128 f = _mm_set_ss(x);
    __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(f));
    __m128 r = _mm_add_ss(t, _mm_and_ps(_mm_cmplt_ss(f, t), _mm_set_ss(-1.0f)));
    return _mm_cvtss_f32(r);
    #endif
  }

  #ifdef STBIR_CEILF
  #undef STBIR_CEILF
  #endif
  #define STBIR_CEILF stbir_simd_ceilf
  static stbir__inline float stbir_simd_ceilf(float x)  // martins ceilf
  {
    #if defined(STBIR_AVX) || defined(__SSE4_1__) || defined(STBIR_SSE41)
    __m128 t = _mm_set_ss(x);
    return _mm_cvtss_f32( _mm_ceil_ss(t, t) );
    #else
    __m128 f = _mm_set_ss(x);
    __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(f));
    __m128 r = _mm_add_ss(t, _mm_and_ps(_mm_cmplt_ss(t, f), _mm_set_ss(1.0f)));
    return _mm_cvtss_f32(r);
    #endif
  }

#elif defined(STBIR_NEON)

  #include <arm_neon.h>

  #define stbir__simdf float32x4_t
  #define stbir__simdi uint32x4_t

  #define stbir_simdi_castf( reg ) vreinterpretq_u32_f32(reg)
  #define stbir_simdf_casti( reg ) vreinterpretq_f32_u32(reg)

  #define stbir__simdf_load( reg, ptr ) (reg) = vld1q_f32( (float const*)(ptr) )
  #define stbir__simdi_load( reg, ptr ) (reg) = vld1q_u32( (uint32_t const*)(ptr) )
  #define stbir__simdf_load1( out, ptr ) (out) = vld1q_dup_f32( (float const*)(ptr) ) // top values can be random (not denormal or nan for perf)
  #define stbir__simdi_load1( out, ptr ) (out) = vld1q_dup_u32( (uint32_t const*)(ptr) )
  #define stbir__simdf_load1z( out, ptr ) (out) = vld1q_lane_f32( (float const*)(ptr), vdupq_n_f32(0), 0 ) // top values must be zero
  #define stbir__simdf_frep4( fvar ) vdupq_n_f32( fvar )
  #define stbir__simdf_load1frep4( out, fvar ) (out) = vdupq_n_f32( fvar )
  #define stbir__simdf_load2( out, ptr ) (out) = vcombine_f32( vld1_f32( (float const*)(ptr) ), vcreate_f32(0) ) // top values can be random (not denormal or nan for perf)
  #define stbir__simdf_load2z( out, ptr ) (out) = vcombine_f32( vld1_f32( (float const*)(ptr) ), vcreate_f32(0) )  // top values must be zero
  #define stbir__simdf_load2hmerge( out, reg, ptr ) (out) = vcombine_f32( vget_low_f32(reg), vld1_f32( (float const*)(ptr) ) )

  #define stbir__simdf_zeroP() vdupq_n_f32(0)
  #define stbir__simdf_zero( reg ) (reg) = vdupq_n_f32(0)

  #define stbir__simdf_store( ptr, reg )  vst1q_f32( (float*)(ptr), reg )
  #define stbir__simdf_store1( ptr, reg ) vst1q_lane_f32( (float*)(ptr), reg, 0)
  #define stbir__simdf_store2( ptr, reg ) vst1_f32( (float*)(ptr), vget_low_f32(reg) )
  #define stbir__simdf_store2h( ptr, reg ) vst1_f32( (float*)(ptr), vget_high_f32(reg) )

  #define stbir__simdi_store( ptr, reg )  vst1q_u32( (uint32_t*)(ptr), reg )
  #define stbir__simdi_store1( ptr, reg ) vst1q_lane_u32( (uint32_t*)(ptr), reg, 0 )
  #define stbir__simdi_store2( ptr, reg ) vst1_u32( (uint32_t*)(ptr), vget_low_u32(reg) )

  #define stbir__prefetch( ptr )

  #define stbir__simdi_expand_u8_to_u32(out0,out1,out2,out3,ireg) \
  { \
    uint16x8_t l = vmovl_u8( vget_low_u8 ( vreinterpretq_u8_u32(ireg) ) ); \
    uint16x8_t h = vmovl_u8( vget_high_u8( vreinterpretq_u8_u32(ireg) ) ); \
    out0 = vmovl_u16( vget_low_u16 ( l ) ); \
    out1 = vmovl_u16( vget_high_u16( l ) ); \
    out2 = vmovl_u16( vget_low_u16 ( h ) ); \
    out3 = vmovl_u16( vget_high_u16( h ) ); \
  }

  #define stbir__simdi_expand_u8_to_1u32(out,ireg) \
  { \
    uint16x8_t tmp = vmovl_u8( vget_low_u8( vreinterpretq_u8_u32(ireg) ) ); \
    out = vmovl_u16( vget_low_u16( tmp ) ); \
  }

  #define stbir__simdi_expand_u16_to_u32(out0,out1,ireg) \
  { \
    uint16x8_t tmp = vreinterpretq_u16_u32(ireg); \
    out0 = vmovl_u16( vget_low_u16 ( tmp ) ); \
    out1 = vmovl_u16( vget_high_u16( tmp ) ); \
  }

  #define stbir__simdf_convert_float_to_i32( i, f ) (i) = vreinterpretq_u32_s32( vcvtq_s32_f32(f) )
  #define stbir__simdf_convert_float_to_int( f ) vgetq_lane_s32(vcvtq_s32_f32(f), 0)
  #define stbir__simdi_to_int( i ) (int)vgetq_lane_u32(i, 0)
  #define stbir__simdf_convert_float_to_uint8( f ) ((unsigned char)vgetq_lane_s32(vcvtq_s32_f32(vmaxq_f32(vminq_f32(f,STBIR__CONSTF(STBIR_max_uint8_as_float)),vdupq_n_f32(0))), 0))
  #define stbir__simdf_convert_float_to_short( f ) ((unsigned short)vgetq_lane_s32(vcvtq_s32_f32(vmaxq_f32(vminq_f32(f,STBIR__CONSTF(STBIR_max_uint16_as_float)),vdupq_n_f32(0))), 0))
  #define stbir__simdi_convert_i32_to_float(out, ireg) (out) = vcvtq_f32_s32( vreinterpretq_s32_u32(ireg) )
  #define stbir__simdf_add( out, reg0, reg1 ) (out) = vaddq_f32( reg0, reg1 )
  #define stbir__simdf_mult( out, reg0, reg1 ) (out) = vmulq_f32( reg0, reg1 )
  #define stbir__simdf_mult_mem( out, reg, ptr ) (out) = vmulq_f32( reg, vld1q_f32( (float const*)(ptr) ) )
  #define stbir__simdf_mult1_mem( out, reg, ptr ) (out) = vmulq_f32( reg, vld1q_dup_f32( (float const*)(ptr) ) )
  #define stbir__simdf_add_mem( out, reg, ptr ) (out) = vaddq_f32( reg, vld1q_f32( (float const*)(ptr) ) )
  #define stbir__simdf_add1_mem( out, reg, ptr ) (out) = vaddq_f32( reg, vld1q_dup_f32( (float const*)(ptr) ) )

  #ifdef STBIR_USE_FMA           // not on by default to maintain bit identical simd to non-simd (and also x64 no madd to arm madd)
  #define stbir__simdf_madd( out, add, mul1, mul2 ) (out) = vfmaq_f32( add, mul1, mul2 )
  #define stbir__simdf_madd1( out, add, mul1, mul2 ) (out) = vfmaq_f32( add, mul1, mul2 )
  #define stbir__simdf_madd_mem( out, add, mul, ptr ) (out) = vfmaq_f32( add, mul, vld1q_f32( (float const*)(ptr) ) )
  #define stbir__simdf_madd1_mem( out, add, mul, ptr ) (out) = vfmaq_f32( add, mul, vld1q_dup_f32( (float const*)(ptr) ) )
  #else
  #define stbir__simdf_madd( out, add, mul1, mul2 ) (out) = vaddq_f32( add, vmulq_f32( mul1, mul2 ) )
  #define stbir__simdf_madd1( out, add, mul1, mul2 ) (out) = vaddq_f32( add, vmulq_f32( mul1, mul2 ) )
  #define stbir__simdf_madd_mem( out, add, mul, ptr ) (out) = vaddq_f32( add, vmulq_f32( mul, vld1q_f32( (float const*)(ptr) ) ) )
  #define stbir__simdf_madd1_mem( out, add, mul, ptr ) (out) = vaddq_f32( add, vmulq_f32( mul, vld1q_dup_f32( (float const*)(ptr) ) ) )
  #endif

  #define stbir__simdf_add1( out, reg0, reg1 ) (out) = vaddq_f32( reg0, reg1 )
  #define stbir__simdf_mult1( out, reg0, reg1 ) (out) = vmulq_f32( reg0, reg1 )

  #define stbir__simdf_and( out, reg0, reg1 ) (out) = vreinterpretq_f32_u32( vandq_u32( vreinterpretq_u32_f32(reg0), vreinterpretq_u32_f32(reg1) ) )
  #define stbir__simdf_or( out, reg0, reg1 ) (out) = vreinterpretq_f32_u32( vorrq_u32( vreinterpretq_u32_f32(reg0), vreinterpretq_u32_f32(reg1) ) )

  #define stbir__simdf_min( out, reg0, reg1 ) (out) = vminq_f32( reg0, reg1 )
  #define stbir__simdf_max( out, reg0, reg1 ) (out) = vmaxq_f32( reg0, reg1 )
  #define stbir__simdf_min1( out, reg0, reg1 ) (out) = vminq_f32( reg0, reg1 )
  #define stbir__simdf_max1( out, reg0, reg1 ) (out) = vmaxq_f32( reg0, reg1 )

  #define stbir__simdf_0123ABCDto3ABx( out, reg0, reg1 ) (out) = vextq_f32( reg0, reg1, 3 )
  #define stbir__simdf_0123ABCDto23Ax( out, reg0, reg1 ) (out) = vextq_f32( reg0, reg1, 2 )

  #define stbir__simdf_a1a1( out, alp, ones ) (out) = vzipq_f32(vuzpq_f32(alp, alp).val[1], ones).val[0]
  #define stbir__simdf_1a1a( out, alp, ones ) (out) = vzipq_f32(ones, vuzpq_f32(alp, alp).val[0]).val[0]

  #if defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ )

    #define stbir__simdf_aaa1( out, alp, ones ) (out) = vcopyq_laneq_f32(vdupq_n_f32(vgetq_lane_f32(alp, 3)), 3, ones, 3)
    #define stbir__simdf_1aaa( out, alp, ones ) (out) = vcopyq_laneq_f32(vdupq_n_f32(vgetq_lane_f32(alp, 0)), 0, ones, 0)

    #if defined( _MSC_VER ) && !defined(__clang__)
      #define stbir_make16(a,b,c,d) vcombine_u8( \
        vcreate_u8( (4*a+0) | ((4*a+1)<<8) | ((4*a+2)<<16) | ((4*a+3)<<24) | \
          ((stbir_uint64)(4*b+0)<<32) | ((stbir_uint64)(4*b+1)<<40) | ((stbir_uint64)(4*b+2)<<48) | ((stbir_uint64)(4*b+3)<<56)), \
        vcreate_u8( (4*c+0) | ((4*c+1)<<8) | ((4*c+2)<<16) | ((4*c+3)<<24) | \
          ((stbir_uint64)(4*d+0)<<32) | ((stbir_uint64)(4*d+1)<<40) | ((stbir_uint64)(4*d+2)<<48) | ((stbir_uint64)(4*d+3)<<56) ) )

      static stbir__inline uint8x16x2_t stbir_make16x2(float32x4_t rega,float32x4_t regb)
      {
        uint8x16x2_t r = { vreinterpretq_u8_f32(rega), vreinterpretq_u8_f32(regb) };
        return r;
      }
    #else
      #define stbir_make16(a,b,c,d) (uint8x16_t){4*a+0,4*a+1,4*a+2,4*a+3,4*b+0,4*b+1,4*b+2,4*b+3,4*c+0,4*c+1,4*c+2,4*c+3,4*d+0,4*d+1,4*d+2,4*d+3}
      #define stbir_make16x2(a,b) (uint8x16x2_t){{vreinterpretq_u8_f32(a),vreinterpretq_u8_f32(b)}}
    #endif

    #define stbir__simdf_swiz( reg, one, two, three, four ) vreinterpretq_f32_u8( vqtbl1q_u8( vreinterpretq_u8_f32(reg), stbir_make16(one, two, three, four) ) )
    #define stbir__simdf_swiz2( rega, regb, one, two, three, four ) vreinterpretq_f32_u8( vqtbl2q_u8( stbir_make16x2(rega,regb), stbir_make16(one, two, three, four) ) )

    #define stbir__simdi_16madd( out, reg0, reg1 ) \
    { \
      int16x8_t r0 = vreinterpretq_s16_u32(reg0); \
      int16x8_t r1 = vreinterpretq_s16_u32(reg1); \
      int32x4_t tmp0 = vmull_s16( vget_low_s16(r0), vget_low_s16(r1) ); \
      int32x4_t tmp1 = vmull_s16( vget_high_s16(r0), vget_high_s16(r1) ); \
      (out) = vreinterpretq_u32_s32( vpaddq_s32(tmp0, tmp1) ); \
    }

  #else

    #define stbir__simdf_aaa1( out, alp, ones ) (out) = vsetq_lane_f32(1.0f, vdupq_n_f32(vgetq_lane_f32(alp, 3)), 3)
    #define stbir__simdf_1aaa( out, alp, ones ) (out) = vsetq_lane_f32(1.0f, vdupq_n_f32(vgetq_lane_f32(alp, 0)), 0)

    #if defined( _MSC_VER ) && !defined(__clang__)
      static stbir__inline uint8x8x2_t stbir_make8x2(float32x4_t reg)
      {
        uint8x8x2_t r = { { vget_low_u8(vreinterpretq_u8_f32(reg)), vget_high_u8(vreinterpretq_u8_f32(reg)) } };
        return r;
      }
      #define stbir_make8(a,b) vcreate_u8( \
        (4*a+0) | ((4*a+1)<<8) | ((4*a+2)<<16) | ((4*a+3)<<24) | \
        ((stbir_uint64)(4*b+0)<<32) | ((stbir_uint64)(4*b+1)<<40) | ((stbir_uint64)(4*b+2)<<48) | ((stbir_uint64)(4*b+3)<<56) )
    #else
      #define stbir_make8x2(reg) (uint8x8x2_t){ { vget_low_u8(vreinterpretq_u8_f32(reg)), vget_high_u8(vreinterpretq_u8_f32(reg)) } }
      #define stbir_make8(a,b) (uint8x8_t){4*a+0,4*a+1,4*a+2,4*a+3,4*b+0,4*b+1,4*b+2,4*b+3}
    #endif

    #define stbir__simdf_swiz( reg, one, two, three, four ) vreinterpretq_f32_u8( vcombine_u8( \
        vtbl2_u8( stbir_make8x2( reg ), stbir_make8( one, two ) ), \
        vtbl2_u8( stbir_make8x2( reg ), stbir_make8( three, four ) ) ) )

    #define stbir__simdi_16madd( out, reg0, reg1 ) \
    { \
      int16x8_t r0 = vreinterpretq_s16_u32(reg0); \
      int16x8_t r1 = vreinterpretq_s16_u32(reg1); \
      int32x4_t tmp0 = vmull_s16( vget_low_s16(r0), vget_low_s16(r1) ); \
      int32x4_t tmp1 = vmull_s16( vget_high_s16(r0), vget_high_s16(r1) ); \
      int32x2_t out0 = vpadd_s32( vget_low_s32(tmp0), vget_high_s32(tmp0) ); \
      int32x2_t out1 = vpadd_s32( vget_low_s32(tmp1), vget_high_s32(tmp1) ); \
      (out) = vreinterpretq_u32_s32( vcombine_s32(out0, out1) ); \
    }

  #endif

  #define stbir__simdi_and( out, reg0, reg1 ) (out) = vandq_u32( reg0, reg1 )
  #define stbir__simdi_or( out, reg0, reg1 ) (out) = vorrq_u32( reg0, reg1 )

  #define stbir__simdf_pack_to_8bytes(out,aa,bb) \
  { \
    float32x4_t af = vmaxq_f32( vminq_f32(aa,STBIR__CONSTF(STBIR_max_uint8_as_float) ), vdupq_n_f32(0) ); \
    float32x4_t bf = vmaxq_f32( vminq_f32(bb,STBIR__CONSTF(STBIR_max_uint8_as_float) ), vdupq_n_f32(0) ); \
    int16x4_t ai = vqmovn_s32( vcvtq_s32_f32( af ) ); \
    int16x4_t bi = vqmovn_s32( vcvtq_s32_f32( bf ) ); \
    uint8x8_t out8 = vqmovun_s16( vcombine_s16(ai, bi) ); \
    out = vreinterpretq_u32_u8( vcombine_u8(out8, out8) ); \
  }

  #define stbir__simdf_pack_to_8words(out,aa,bb) \
  { \
    float32x4_t af = vmaxq_f32( vminq_f32(aa,STBIR__CONSTF(STBIR_max_uint16_as_float) ), vdupq_n_f32(0) ); \
    float32x4_t bf = vmaxq_f32( vminq_f32(bb,STBIR__CONSTF(STBIR_max_uint16_as_float) ), vdupq_n_f32(0) ); \
    int32x4_t ai = vcvtq_s32_f32( af ); \
    int32x4_t bi = vcvtq_s32_f32( bf ); \
    out = vreinterpretq_u32_u16( vcombine_u16(vqmovun_s32(ai), vqmovun_s32(bi)) ); \
  }

  #define stbir__interleave_pack_and_store_16_u8( ptr, r0, r1, r2, r3 ) \
  { \
    int16x4x2_t tmp0 = vzip_s16( vqmovn_s32(vreinterpretq_s32_u32(r0)), vqmovn_s32(vreinterpretq_s32_u32(r2)) ); \
    int16x4x2_t tmp1 = vzip_s16( vqmovn_s32(vreinterpretq_s32_u32(r1)), vqmovn_s32(vreinterpretq_s32_u32(r3)) ); \
    uint8x8x2_t out = \
    { { \
      vqmovun_s16( vcombine_s16(tmp0.val[0], tmp0.val[1]) ), \
      vqmovun_s16( vcombine_s16(tmp1.val[0], tmp1.val[1]) ), \
    } }; \
    vst2_u8(ptr, out); \
  }

  #define stbir__simdf_load4_transposed( o0, o1, o2, o3, ptr ) \
  { \
    float32x4x4_t tmp = vld4q_f32(ptr); \
    o0 = tmp.val[0]; \
    o1 = tmp.val[1]; \
    o2 = tmp.val[2]; \
    o3 = tmp.val[3]; \
  }

  #define stbir__simdi_32shr( out, reg, imm ) out = vshrq_n_u32( reg, imm )

  #if defined( _MSC_VER ) && !defined(__clang__)
    #define STBIR__SIMDF_CONST(var, x) __declspec(align(8)) float var[] = { x, x, x, x }
    #define STBIR__SIMDI_CONST(var, x) __declspec(align(8)) uint32_t var[] = { x, x, x, x }
    #define STBIR__CONSTF(var) (*(const float32x4_t*)var)
    #define STBIR__CONSTI(var) (*(const uint32x4_t*)var)
  #else
    #define STBIR__SIMDF_CONST(var, x) stbir__simdf var = { x, x, x, x }
    #define STBIR__SIMDI_CONST(var, x) stbir__simdi var = { x, x, x, x }
    #define STBIR__CONSTF(var) (var)
    #define STBIR__CONSTI(var) (var)
  #endif

  #ifdef STBIR_FLOORF
  #undef STBIR_FLOORF
  #endif
  #define STBIR_FLOORF stbir_simd_floorf
  static stbir__inline float stbir_simd_floorf(float x)
  {
    #if defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ )
    return vget_lane_f32( vrndm_f32( vdup_n_f32(x) ), 0);
    #else
    float32x2_t f = vdup_n_f32(x);
    float32x2_t t = vcvt_f32_s32(vcvt_s32_f32(f));
    uint32x2_t a = vclt_f32(f, t);
    uint32x2_t b = vreinterpret_u32_f32(vdup_n_f32(-1.0f));
    float32x2_t r = vadd_f32(t, vreinterpret_f32_u32(vand_u32(a, b)));
    return vget_lane_f32(r, 0);
    #endif
  }

  #ifdef STBIR_CEILF
  #undef STBIR_CEILF
  #endif
  #define STBIR_CEILF stbir_simd_ceilf
  static stbir__inline float stbir_simd_ceilf(float x)
  {
    #if defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ )
    return vget_lane_f32( vrndp_f32( vdup_n_f32(x) ), 0);
    #else
    float32x2_t f = vdup_n_f32(x);
    float32x2_t t = vcvt_f32_s32(vcvt_s32_f32(f));
    uint32x2_t a = vclt_f32(t, f);
    uint32x2_t b = vreinterpret_u32_f32(vdup_n_f32(1.0f));
    float32x2_t r = vadd_f32(t, vreinterpret_f32_u32(vand_u32(a, b)));
    return vget_lane_f32(r, 0);
    #endif
  }

  #define STBIR_SIMD

#elif defined(STBIR_WASM)

  #include <wasm_simd128.h>

  #define stbir__simdf v128_t
  #define stbir__simdi v128_t

  #define stbir_simdi_castf( reg ) (reg)
  #define stbir_simdf_casti( reg ) (reg)

  #define stbir__simdf_load( reg, ptr )             (reg) = wasm_v128_load( (void const*)(ptr) )
  #define stbir__simdi_load( reg, ptr )             (reg) = wasm_v128_load( (void const*)(ptr) )
  #define stbir__simdf_load1( out, ptr )            (out) = wasm_v128_load32_splat( (void const*)(ptr) ) // top values can be random (not denormal or nan for perf)
  #define stbir__simdi_load1( out, ptr )            (out) = wasm_v128_load32_splat( (void const*)(ptr) )
  #define stbir__simdf_load1z( out, ptr )           (out) = wasm_v128_load32_zero( (void const*)(ptr) ) // top values must be zero
  #define stbir__simdf_frep4( fvar )                wasm_f32x4_splat( fvar )
  #define stbir__simdf_load1frep4( out, fvar )      (out) = wasm_f32x4_splat( fvar )
  #define stbir__simdf_load2( out, ptr )            (out) = wasm_v128_load64_splat( (void const*)(ptr) ) // top values can be random (not denormal or nan for perf)
  #define stbir__simdf_load2z( out, ptr )           (out) = wasm_v128_load64_zero( (void const*)(ptr) ) // top values must be zero
  #define stbir__simdf_load2hmerge( out, reg, ptr ) (out) = wasm_v128_load64_lane( (void const*)(ptr), reg, 1 )

  #define stbir__simdf_zeroP() wasm_f32x4_const_splat(0)
  #define stbir__simdf_zero( reg ) (reg) = wasm_f32x4_const_splat(0)

  #define stbir__simdf_store( ptr, reg )   wasm_v128_store( (void*)(ptr), reg )
  #define stbir__simdf_store1( ptr, reg )  wasm_v128_store32_lane( (void*)(ptr), reg, 0 )
  #define stbir__simdf_store2( ptr, reg )  wasm_v128_store64_lane( (void*)(ptr), reg, 0 )
  #define stbir__simdf_store2h( ptr, reg ) wasm_v128_store64_lane( (void*)(ptr), reg, 1 )

  #define stbir__simdi_store( ptr, reg )  wasm_v128_store( (void*)(ptr), reg )
  #define stbir__simdi_store1( ptr, reg ) wasm_v128_store32_lane( (void*)(ptr), reg, 0 )
  #define stbir__simdi_store2( ptr, reg ) wasm_v128_store64_lane( (void*)(ptr), reg, 0 )

  #define stbir__prefetch( ptr )

  #define stbir__simdi_expand_u8_to_u32(out0,out1,out2,out3,ireg) \
  { \
    v128_t l = wasm_u16x8_extend_low_u8x16 ( ireg ); \
    v128_t h = wasm_u16x8_extend_high_u8x16( ireg ); \
    out0 = wasm_u32x4_extend_low_u16x8 ( l ); \
    out1 = wasm_u32x4_extend_high_u16x8( l ); \
    out2 = wasm_u32x4_extend_low_u16x8 ( h ); \
    out3 = wasm_u32x4_extend_high_u16x8( h ); \
  }

  #define stbir__simdi_expand_u8_to_1u32(out,ireg) \
  { \
    v128_t tmp = wasm_u16x8_extend_low_u8x16(ireg); \
    out = wasm_u32x4_extend_low_u16x8(tmp); \
  }

  #define stbir__simdi_expand_u16_to_u32(out0,out1,ireg) \
  { \
    out0 = wasm_u32x4_extend_low_u16x8 ( ireg ); \
    out1 = wasm_u32x4_extend_high_u16x8( ireg ); \
  }

  #define stbir__simdf_convert_float_to_i32( i, f )    (i) = wasm_i32x4_trunc_sat_f32x4(f)
  #define stbir__simdf_convert_float_to_int( f )       wasm_i32x4_extract_lane(wasm_i32x4_trunc_sat_f32x4(f), 0)
  #define stbir__simdi_to_int( i )                     wasm_i32x4_extract_lane(i, 0)
  #define stbir__simdf_convert_float_to_uint8( f )     ((unsigned char)wasm_i32x4_extract_lane(wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_max(wasm_f32x4_min(f,STBIR_max_uint8_as_float),wasm_f32x4_const_splat(0))), 0))
  #define stbir__simdf_convert_float_to_short( f )     ((unsigned short)wasm_i32x4_extract_lane(wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_max(wasm_f32x4_min(f,STBIR_max_uint16_as_float),wasm_f32x4_const_splat(0))), 0))
  #define stbir__simdi_convert_i32_to_float(out, ireg) (out) = wasm_f32x4_convert_i32x4(ireg)
  #define stbir__simdf_add( out, reg0, reg1 )          (out) = wasm_f32x4_add( reg0, reg1 )
  #define stbir__simdf_mult( out, reg0, reg1 )         (out) = wasm_f32x4_mul( reg0, reg1 )
  #define stbir__simdf_mult_mem( out, reg, ptr )       (out) = wasm_f32x4_mul( reg, wasm_v128_load( (void const*)(ptr) ) )
  #define stbir__simdf_mult1_mem( out, reg, ptr )      (out) = wasm_f32x4_mul( reg, wasm_v128_load32_splat( (void const*)(ptr) ) )
  #define stbir__simdf_add_mem( out, reg, ptr )        (out) = wasm_f32x4_add( reg, wasm_v128_load( (void const*)(ptr) ) )
  #define stbir__simdf_add1_mem( out, reg, ptr )       (out) = wasm_f32x4_add( reg, wasm_v128_load32_splat( (void const*)(ptr) ) )

  #define stbir__simdf_madd( out, add, mul1, mul2 )    (out) = wasm_f32x4_add( add, wasm_f32x4_mul( mul1, mul2 ) )
  #define stbir__simdf_madd1( out, add, mul1, mul2 )   (out) = wasm_f32x4_add( add, wasm_f32x4_mul( mul1, mul2 ) )
  #define stbir__simdf_madd_mem( out, add, mul, ptr )  (out) = wasm_f32x4_add( add, wasm_f32x4_mul( mul, wasm_v128_load( (void const*)(ptr) ) ) )
  #define stbir__simdf_madd1_mem( out, add, mul, ptr ) (out) = wasm_f32x4_add( add, wasm_f32x4_mul( mul, wasm_v128_load32_splat( (void const*)(ptr) ) ) )

  #define stbir__simdf_add1( out, reg0, reg1 )  (out) = wasm_f32x4_add( reg0, reg1 )
  #define stbir__simdf_mult1( out, reg0, reg1 ) (out) = wasm_f32x4_mul( reg0, reg1 )

  #define stbir__simdf_and( out, reg0, reg1 ) (out) = wasm_v128_and( reg0, reg1 )
  #define stbir__simdf_or( out, reg0, reg1 )  (out) = wasm_v128_or( reg0, reg1 )

  #define stbir__simdf_min( out, reg0, reg1 ) (out) = wasm_f32x4_min( reg0, reg1 )
  #define stbir__simdf_max( out, reg0, reg1 ) (out) = wasm_f32x4_max( reg0, reg1 )
  #define stbir__simdf_min1( out, reg0, reg1 ) (out) = wasm_f32x4_min( reg0, reg1 )
  #define stbir__simdf_max1( out, reg0, reg1 ) (out) = wasm_f32x4_max( reg0, reg1 )

  #define stbir__simdf_0123ABCDto3ABx( out, reg0, reg1 ) (out) = wasm_i32x4_shuffle( reg0, reg1, 3, 4, 5, -1 )
  #define stbir__simdf_0123ABCDto23Ax( out, reg0, reg1 ) (out) = wasm_i32x4_shuffle( reg0, reg1, 2, 3, 4, -1 )

  #define stbir__simdf_aaa1(out,alp,ones) (out) = wasm_i32x4_shuffle(alp, ones, 3, 3, 3, 4)
  #define stbir__simdf_1aaa(out,alp,ones) (out) = wasm_i32x4_shuffle(alp, ones, 4, 0, 0, 0)
  #define stbir__simdf_a1a1(out,alp,ones) (out) = wasm_i32x4_shuffle(alp, ones, 1, 4, 3, 4)
  #define stbir__simdf_1a1a(out,alp,ones) (out) = wasm_i32x4_shuffle(alp, ones, 4, 0, 4, 2)

  #define stbir__simdf_swiz( reg, one, two, three, four ) wasm_i32x4_shuffle(reg, reg, one, two, three, four)

  #define stbir__simdi_and( out, reg0, reg1 )    (out) = wasm_v128_and( reg0, reg1 )
  #define stbir__simdi_or( out, reg0, reg1 )     (out) = wasm_v128_or( reg0, reg1 )
  #define stbir__simdi_16madd( out, reg0, reg1 ) (out) = wasm_i32x4_dot_i16x8( reg0, reg1 )

  #define stbir__simdf_pack_to_8bytes(out,aa,bb) \
  { \
    v128_t af = wasm_f32x4_max( wasm_f32x4_min(aa, STBIR_max_uint8_as_float), wasm_f32x4_const_splat(0) ); \
    v128_t bf = wasm_f32x4_max( wasm_f32x4_min(bb, STBIR_max_uint8_as_float), wasm_f32x4_const_splat(0) ); \
    v128_t ai = wasm_i32x4_trunc_sat_f32x4( af ); \
    v128_t bi = wasm_i32x4_trunc_sat_f32x4( bf ); \
    v128_t out16 = wasm_i16x8_narrow_i32x4( ai, bi ); \
    out = wasm_u8x16_narrow_i16x8( out16, out16 ); \
  }

  #define stbir__simdf_pack_to_8words(out,aa,bb) \
  { \
    v128_t af = wasm_f32x4_max( wasm_f32x4_min(aa, STBIR_max_uint16_as_float), wasm_f32x4_const_splat(0)); \
    v128_t bf = wasm_f32x4_max( wasm_f32x4_min(bb, STBIR_max_uint16_as_float), wasm_f32x4_const_splat(0)); \
    v128_t ai = wasm_i32x4_trunc_sat_f32x4( af ); \
    v128_t bi = wasm_i32x4_trunc_sat_f32x4( bf ); \
    out = wasm_u16x8_narrow_i32x4( ai, bi ); \
  }

  #define stbir__interleave_pack_and_store_16_u8( ptr, r0, r1, r2, r3 ) \
  { \
    v128_t tmp0 = wasm_i16x8_narrow_i32x4(r0, r1); \
    v128_t tmp1 = wasm_i16x8_narrow_i32x4(r2, r3); \
    v128_t tmp = wasm_u8x16_narrow_i16x8(tmp0, tmp1); \
    tmp = wasm_i8x16_shuffle(tmp, tmp, 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15); \
    wasm_v128_store( (void*)(ptr), tmp); \
  }

  #define stbir__simdf_load4_transposed( o0, o1, o2, o3, ptr ) \
  { \
    v128_t t0 = wasm_v128_load( ptr    ); \
    v128_t t1 = wasm_v128_load( ptr+4  ); \
    v128_t t2 = wasm_v128_load( ptr+8  ); \
    v128_t t3 = wasm_v128_load( ptr+12 ); \
    v128_t s0 = wasm_i32x4_shuffle(t0, t1, 0, 4, 2, 6); \
    v128_t s1 = wasm_i32x4_shuffle(t0, t1, 1, 5, 3, 7); \
    v128_t s2 = wasm_i32x4_shuffle(t2, t3, 0, 4, 2, 6); \
    v128_t s3 = wasm_i32x4_shuffle(t2, t3, 1, 5, 3, 7); \
    o0 = wasm_i32x4_shuffle(s0, s2, 0, 1, 4, 5); \
    o1 = wasm_i32x4_shuffle(s1, s3, 0, 1, 4, 5); \
    o2 = wasm_i32x4_shuffle(s0, s2, 2, 3, 6, 7); \
    o3 = wasm_i32x4_shuffle(s1, s3, 2, 3, 6, 7); \
  }

  #define stbir__simdi_32shr( out, reg, imm ) out = wasm_u32x4_shr( reg, imm )

  typedef float stbir__f32x4 __attribute__((__vector_size__(16), __aligned__(16)));
  #define STBIR__SIMDF_CONST(var, x) stbir__simdf var = (v128_t)(stbir__f32x4){ x, x, x, x }
  #define STBIR__SIMDI_CONST(var, x) stbir__simdi var = { x, x, x, x }
  #define STBIR__CONSTF(var) (var)
  #define STBIR__CONSTI(var) (var)

  #ifdef STBIR_FLOORF
  #undef STBIR_FLOORF
  #endif
  #define STBIR_FLOORF stbir_simd_floorf
  static stbir__inline float stbir_simd_floorf(float x)
  {
    return wasm_f32x4_extract_lane( wasm_f32x4_floor( wasm_f32x4_splat(x) ), 0);
  }

  #ifdef STBIR_CEILF
  #undef STBIR_CEILF
  #endif
  #define STBIR_CEILF stbir_simd_ceilf
  static stbir__inline float stbir_simd_ceilf(float x)
  {
    return wasm_f32x4_extract_lane( wasm_f32x4_ceil( wasm_f32x4_splat(x) ), 0);
  }

  #define STBIR_SIMD

#endif  // SSE2/NEON/WASM

#endif // NO SIMD

#ifdef STBIR_SIMD8
  #define stbir__simdfX stbir__simdf8
  #define stbir__simdiX stbir__simdi8
  #define stbir__simdfX_load stbir__simdf8_load
  #define stbir__simdiX_load stbir__simdi8_load
  #define stbir__simdfX_mult stbir__simdf8_mult
  #define stbir__simdfX_add_mem stbir__simdf8_add_mem
  #define stbir__simdfX_madd_mem stbir__simdf8_madd_mem
  #define stbir__simdfX_store stbir__simdf8_store
  #define stbir__simdiX_store stbir__simdi8_store
  #define stbir__simdf_frepX  stbir__simdf8_frep8
  #define stbir__simdfX_madd stbir__simdf8_madd
  #define stbir__simdfX_min stbir__simdf8_min
  #define stbir__simdfX_max stbir__simdf8_max
  #define stbir__simdfX_aaa1 stbir__simdf8_aaa1
  #define stbir__simdfX_1aaa stbir__simdf8_1aaa
  #define stbir__simdfX_a1a1 stbir__simdf8_a1a1
  #define stbir__simdfX_1a1a stbir__simdf8_1a1a
  #define stbir__simdfX_convert_float_to_i32 stbir__simdf8_convert_float_to_i32
  #define stbir__simdfX_pack_to_words stbir__simdf8_pack_to_16words
  #define stbir__simdfX_zero stbir__simdf8_zero
  #define STBIR_onesX STBIR_ones8
  #define STBIR_max_uint8_as_floatX STBIR_max_uint8_as_float8
  #define STBIR_max_uint16_as_floatX STBIR_max_uint16_as_float8
  #define STBIR_simd_point5X STBIR_simd_point58
  #define stbir__simdfX_float_count 8
  #define stbir__simdfX_0123to1230 stbir__simdf8_0123to12301230
  #define stbir__simdfX_0123to2103 stbir__simdf8_0123to21032103
  static const stbir__simdf8 STBIR_max_uint16_as_float_inverted8 = { stbir__max_uint16_as_float_inverted,stbir__max_uint16_as_float_inverted,stbir__max_uint16_as_float_inverted,stbir__max_uint16_as_float_inverted,stbir__max_uint16_as_float_inverted,stbir__max_uint16_as_float_inverted,stbir__max_uint16_as_float_inverted,stbir__max_uint16_as_float_inverted };
  static const stbir__simdf8 STBIR_max_uint8_as_float_inverted8 = { stbir__max_uint8_as_float_inverted,stbir__max_uint8_as_float_inverted,stbir__max_uint8_as_float_inverted,stbir__max_uint8_as_float_inverted,stbir__max_uint8_as_float_inverted,stbir__max_uint8_as_float_inverted,stbir__max_uint8_as_float_inverted,stbir__max_uint8_as_float_inverted };
  static const stbir__simdf8 STBIR_ones8 = { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 };
  static const stbir__simdf8 STBIR_simd_point58 = { 0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5 };
  static const stbir__simdf8 STBIR_max_uint8_as_float8 = { stbir__max_uint8_as_float,stbir__max_uint8_as_float,stbir__max_uint8_as_float,stbir__max_uint8_as_float, stbir__max_uint8_as_float,stbir__max_uint8_as_float,stbir__max_uint8_as_float,stbir__max_uint8_as_float };
  static const stbir__simdf8 STBIR_max_uint16_as_float8 = { stbir__max_uint16_as_float,stbir__max_uint16_as_float,stbir__max_uint16_as_float,stbir__max_uint16_as_float, stbir__max_uint16_as_float,stbir__max_uint16_as_float,stbir__max_uint16_as_float,stbir__max_uint16_as_float };
#else
  #define stbir__simdfX stbir__simdf
  #define stbir__simdiX stbir__simdi
  #define stbir__simdfX_load stbir__simdf_load
  #define stbir__simdiX_load stbir__simdi_load
  #define stbir__simdfX_mult stbir__simdf_mult
  #define stbir__simdfX_add_mem stbir__simdf_add_mem
  #define stbir__simdfX_madd_mem stbir__simdf_madd_mem
  #define stbir__simdfX_store stbir__simdf_store
  #define stbir__simdiX_store stbir__simdi_store
  #define stbir__simdf_frepX  stbir__simdf_frep4
  #define stbir__simdfX_madd stbir__simdf_madd
  #define stbir__simdfX_min stbir__simdf_min
  #define stbir__simdfX_max stbir__simdf_max
  #define stbir__simdfX_aaa1 stbir__simdf_aaa1
  #define stbir__simdfX_1aaa stbir__simdf_1aaa
  #define stbir__simdfX_a1a1 stbir__simdf_a1a1
  #define stbir__simdfX_1a1a stbir__simdf_1a1a
  #define stbir__simdfX_convert_float_to_i32 stbir__simdf_convert_float_to_i32
  #define stbir__simdfX_pack_to_words stbir__simdf_pack_to_8words
  #define stbir__simdfX_zero stbir__simdf_zero
  #define STBIR_onesX STBIR__CONSTF(STBIR_ones)
  #define STBIR_simd_point5X STBIR__CONSTF(STBIR_simd_point5)
  #define STBIR_max_uint8_as_floatX STBIR__CONSTF(STBIR_max_uint8_as_float)
  #define STBIR_max_uint16_as_floatX STBIR__CONSTF(STBIR_max_uint16_as_float)
  #define stbir__simdfX_float_count 4
  #define stbir__if_simdf8_cast_to_simdf4( val ) ( val )
  #define stbir__simdfX_0123to1230 stbir__simdf_0123to1230
  #define stbir__simdfX_0123to2103 stbir__simdf_0123to2103
#endif


#if defined(STBIR_NEON) && !defined(_M_ARM) && !defined(__arm__)

  #if defined( _MSC_VER ) && !defined(__clang__)
  typedef __int16 stbir__FP16;
  #else
  typedef float16_t stbir__FP16;
  #endif

#else // no NEON, or 32-bit ARM for MSVC

  typedef union stbir__FP16
  {
    unsigned short u;
  } stbir__FP16;

#endif

#if (!defined(STBIR_NEON) && !defined(STBIR_FP16C)) || (defined(STBIR_NEON) && defined(_M_ARM)) || (defined(STBIR_NEON) && defined(__arm__))

  // Fabian's half float routines, see: https://gist.github.com/rygorous/2156668

  static stbir__inline float stbir__half_to_float( stbir__FP16 h )
  {
    static const stbir__FP32 magic = { (254 - 15) << 23 };
    static const stbir__FP32 was_infnan = { (127 + 16) << 23 };
    stbir__FP32 o;

    o.u = (h.u & 0x7fff) << 13;     // exponent/mantissa bits
    o.f *= magic.f;                 // exponent adjust
    if (o.f >= was_infnan.f)        // make sure Inf/NaN survive
      o.u |= 255 << 23;
    o.u |= (h.u & 0x8000) << 16;    // sign bit
    return o.f;
  }

  static stbir__inline stbir__FP16 stbir__float_to_half(float val)
  {
    stbir__FP32 f32infty = { 255 << 23 };
    stbir__FP32 f16max   = { (127 + 16) << 23 };
    stbir__FP32 denorm_magic = { ((127 - 15) + (23 - 10) + 1) << 23 };
    unsigned int sign_mask = 0x80000000u;
    stbir__FP16 o = { 0 };
    stbir__FP32 f;
    unsigned int sign;

    f.f = val;
    sign = f.u & sign_mask;
    f.u ^= sign;

    if (f.u >= f16max.u) // result is Inf or NaN (all exponent bits set)
      o.u = (f.u > f32infty.u) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
    else // (De)normalized number or zero
    {
      if (f.u < (113 << 23)) // resulting FP16 is subnormal or zero
      {
        // use a magic value to align our 10 mantissa bits at the bottom of
        // the float. as long as FP addition is round-to-nearest-even this
        // just works.
        f.f += denorm_magic.f;
        // and one integer subtract of the bias later, we have our final float!
        o.u = (unsigned short) ( f.u - denorm_magic.u );
      }
      else
      {
        unsigned int mant_odd = (f.u >> 13) & 1; // resulting mantissa is odd
        // update exponent, rounding bias part 1
        f.u = f.u + ((15u - 127) << 23) + 0xfff;
        // rounding bias part 2
        f.u += mant_odd;
        // take the bits!
        o.u = (unsigned short) ( f.u >> 13 );
      }
    }

    o.u |= sign >> 16;
    return o;
  }

#endif


#if defined(STBIR_FP16C)

  #include <immintrin.h>

  static stbir__inline void stbir__half_to_float_SIMD(float * output, stbir__FP16 const * input)
  {
    _mm256_storeu_ps( (float*)output, _mm256_cvtph_ps( _mm_loadu_si128( (__m128i const* )input ) ) );
  }

  static stbir__inline void stbir__float_to_half_SIMD(stbir__FP16 * output, float const * input)
  {
    _mm_storeu_si128( (__m128i*)output, _mm256_cvtps_ph( _mm256_loadu_ps( input ), 0 ) );
  }

  static stbir__inline float stbir__half_to_float( stbir__FP16 h )
  {
    return _mm_cvtss_f32( _mm_cvtph_ps( _mm_cvtsi32_si128( (int)h.u ) ) );
  }

  static stbir__inline stbir__FP16 stbir__float_to_half( float f )
  {
    stbir__FP16 h;
    h.u = (unsigned short) _mm_cvtsi128_si32( _mm_cvtps_ph( _mm_set_ss( f ), 0 ) );
    return h;
  }

#elif defined(STBIR_SSE2)

  // Fabian's half float routines, see: https://gist.github.com/rygorous/2156668
  stbir__inline static void stbir__half_to_float_SIMD(float * output, void const * input)
  {
    static const STBIR__SIMDI_CONST(mask_nosign,      0x7fff);
    static const STBIR__SIMDI_CONST(smallest_normal,  0x0400);
    static const STBIR__SIMDI_CONST(infinity,         0x7c00);
    static const STBIR__SIMDI_CONST(expadjust_normal, (127 - 15) << 23);
    static const STBIR__SIMDI_CONST(magic_denorm,     113 << 23);

    __m128i i = _mm_loadu_si128 ( (__m128i const*)(input) );
    __m128i h = _mm_unpacklo_epi16 ( i, _mm_setzero_si128() );
    __m128i mnosign     = STBIR__CONSTI(mask_nosign);
    __m128i eadjust     = STBIR__CONSTI(expadjust_normal);
    __m128i smallest    = STBIR__CONSTI(smallest_normal);
    __m128i infty       = STBIR__CONSTI(infinity);
    __m128i expmant     = _mm_and_si128(mnosign, h);
    __m128i justsign    = _mm_xor_si128(h, expmant);
    __m128i b_notinfnan = _mm_cmpgt_epi32(infty, expmant);
    __m128i b_isdenorm  = _mm_cmpgt_epi32(smallest, expmant);
    __m128i shifted     = _mm_slli_epi32(expmant, 13);
    __m128i adj_infnan  = _mm_andnot_si128(b_notinfnan, eadjust);
    __m128i adjusted    = _mm_add_epi32(eadjust, shifted);
    __m128i den1        = _mm_add_epi32(shifted, STBIR__CONSTI(magic_denorm));
    __m128i adjusted2   = _mm_add_epi32(adjusted, adj_infnan);
    __m128  den2        = _mm_sub_ps(_mm_castsi128_ps(den1), *(const __m128 *)&magic_denorm);
    __m128  adjusted3   = _mm_and_ps(den2, _mm_castsi128_ps(b_isdenorm));
    __m128  adjusted4   = _mm_andnot_ps(_mm_castsi128_ps(b_isdenorm), _mm_castsi128_ps(adjusted2));
    __m128  adjusted5   = _mm_or_ps(adjusted3, adjusted4);
    __m128i sign        = _mm_slli_epi32(justsign, 16);
    __m128  final       = _mm_or_ps(adjusted5, _mm_castsi128_ps(sign));
    stbir__simdf_store( output + 0,  final );

    h = _mm_unpackhi_epi16 ( i, _mm_setzero_si128() );
    expmant     = _mm_and_si128(mnosign, h);
    justsign    = _mm_xor_si128(h, expmant);
    b_notinfnan = _mm_cmpgt_epi32(infty, expmant);
    b_isdenorm  = _mm_cmpgt_epi32(smallest, expmant);
    shifted     = _mm_slli_epi32(expmant, 13);
    adj_infnan  = _mm_andnot_si128(b_notinfnan, eadjust);
    adjusted    = _mm_add_epi32(eadjust, shifted);
    den1        = _mm_add_epi32(shifted, STBIR__CONSTI(magic_denorm));
    adjusted2   = _mm_add_epi32(adjusted, adj_infnan);
    den2        = _mm_sub_ps(_mm_castsi128_ps(den1), *(const __m128 *)&magic_denorm);
    adjusted3   = _mm_and_ps(den2, _mm_castsi128_ps(b_isdenorm));
    adjusted4   = _mm_andnot_ps(_mm_castsi128_ps(b_isdenorm), _mm_castsi128_ps(adjusted2));
    adjusted5   = _mm_or_ps(adjusted3, adjusted4);
    sign        = _mm_slli_epi32(justsign, 16);
    final       = _mm_or_ps(adjusted5, _mm_castsi128_ps(sign));
    stbir__simdf_store( output + 4,  final );

    // ~38 SSE2 ops for 8 values
  }

  // Fabian's round-to-nearest-even float to half
  // ~48 SSE2 ops for 8 output
  stbir__inline static void stbir__float_to_half_SIMD(void * output, float const * input)
  {
    static const STBIR__SIMDI_CONST(mask_sign,      0x80000000u);
    static const STBIR__SIMDI_CONST(c_f16max,       (127 + 16) << 23); // all FP32 values >=this round to +inf
    static const STBIR__SIMDI_CONST(c_nanbit,        0x200);
    static const STBIR__SIMDI_CONST(c_infty_as_fp16, 0x7c00);
    static const STBIR__SIMDI_CONST(c_min_normal,    (127 - 14) << 23); // smallest FP32 that yields a normalized FP16
    static const STBIR__SIMDI_CONST(c_subnorm_magic, ((127 - 15) + (23 - 10) + 1) << 23);
    static const STBIR__SIMDI_CONST(c_normal_bias,    0xfff - ((127 - 15) << 23)); // adjust exponent and add mantissa rounding

    __m128  f           =  _mm_loadu_ps(input);
    __m128  msign       = _mm_castsi128_ps(STBIR__CONSTI(mask_sign));
    __m128  justsign    = _mm_and_ps(msign, f);
    __m128  absf        = _mm_xor_ps(f, justsign);
    __m128i absf_int    = _mm_castps_si128(absf); // the cast is "free" (extra bypass latency, but no thruput hit)
    __m128i f16max      = STBIR__CONSTI(c_f16max);
    __m128  b_isnan     = _mm_cmpunord_ps(absf, absf); // is this a NaN?
    __m128i b_isregular = _mm_cmpgt_epi32(f16max, absf_int); // (sub)normalized or special?
    __m128i nanbit      = _mm_and_si128(_mm_castps_si128(b_isnan), STBIR__CONSTI(c_nanbit));
    __m128i inf_or_nan  = _mm_or_si128(nanbit, STBIR__CONSTI(c_infty_as_fp16)); // output for specials

    __m128i min_normal  = STBIR__CONSTI(c_min_normal);
    __m128i b_issub     = _mm_cmpgt_epi32(min_normal, absf_int);

    // "result is subnormal" path
    __m128  subnorm1    = _mm_add_ps(absf, _mm_castsi128_ps(STBIR__CONSTI(c_subnorm_magic))); // magic value to round output mantissa
    __m128i subnorm2    = _mm_sub_epi32(_mm_castps_si128(subnorm1), STBIR__CONSTI(c_subnorm_magic)); // subtract out bias

    // "result is normal" path
    __m128i mantoddbit  = _mm_slli_epi32(absf_int, 31 - 13); // shift bit 13 (mantissa LSB) to sign
    __m128i mantodd     = _mm_srai_epi32(mantoddbit, 31); // -1 if FP16 mantissa odd, else 0

    __m128i round1      = _mm_add_epi32(absf_int, STBIR__CONSTI(c_normal_bias));
    __m128i round2      = _mm_sub_epi32(round1, mantodd); // if mantissa LSB odd, bias towards rounding up (RTNE)
    __m128i normal      = _mm_srli_epi32(round2, 13); // rounded result

    // combine the two non-specials
    __m128i nonspecial  = _mm_or_si128(_mm_and_si128(subnorm2, b_issub), _mm_andnot_si128(b_issub, normal));

    // merge in specials as well
    __m128i joined      = _mm_or_si128(_mm_and_si128(nonspecial, b_isregular), _mm_andnot_si128(b_isregular, inf_or_nan));

    __m128i sign_shift  = _mm_srai_epi32(_mm_castps_si128(justsign), 16);
    __m128i final2, final= _mm_or_si128(joined, sign_shift);

    f           =  _mm_loadu_ps(input+4);
    justsign    = _mm_and_ps(msign, f);
    absf        = _mm_xor_ps(f, justsign);
    absf_int    = _mm_castps_si128(absf); // the cast is "free" (extra bypass latency, but no thruput hit)
    b_isnan     = _mm_cmpunord_ps(absf, absf); // is this a NaN?
    b_isregular = _mm_cmpgt_epi32(f16max, absf_int); // (sub)normalized or special?
    nanbit      = _mm_and_si128(_mm_castps_si128(b_isnan), c_nanbit);
    inf_or_nan  = _mm_or_si128(nanbit, STBIR__CONSTI(c_infty_as_fp16)); // output for specials

    b_issub     = _mm_cmpgt_epi32(min_normal, absf_int);

    // "result is subnormal" path
    subnorm1    = _mm_add_ps(absf, _mm_castsi128_ps(STBIR__CONSTI(c_subnorm_magic))); // magic value to round output mantissa
    subnorm2    = _mm_sub_epi32(_mm_castps_si128(subnorm1), STBIR__CONSTI(c_subnorm_magic)); // subtract out bias

    // "result is normal" path
    mantoddbit  = _mm_slli_epi32(absf_int, 31 - 13); // shift bit 13 (mantissa LSB) to sign
    mantodd     = _mm_srai_epi32(mantoddbit, 31); // -1 if FP16 mantissa odd, else 0

    round1      = _mm_add_epi32(absf_int, STBIR__CONSTI(c_normal_bias));
    round2      = _mm_sub_epi32(round1, mantodd); // if mantissa LSB odd, bias towards rounding up (RTNE)
    normal      = _mm_srli_epi32(round2, 13); // rounded result

    // combine the two non-specials
    nonspecial  = _mm_or_si128(_mm_and_si128(subnorm2, b_issub), _mm_andnot_si128(b_issub, normal));

    // merge in specials as well
    joined      = _mm_or_si128(_mm_and_si128(nonspecial, b_isregular), _mm_andnot_si128(b_isregular, inf_or_nan));

    sign_shift  = _mm_srai_epi32(_mm_castps_si128(justsign), 16);
    final2      = _mm_or_si128(joined, sign_shift);
    final       = _mm_packs_epi32(final, final2);
    stbir__simdi_store( output,final );
  }

#elif defined(STBIR_NEON) && defined(_MSC_VER) && defined(_M_ARM64) && !defined(__clang__) // 64-bit ARM on MSVC (not clang)

  static stbir__inline void stbir__half_to_float_SIMD(float * output, stbir__FP16 const * input)
  {
    float16x4_t in0 = vld1_f16(input + 0);
    float16x4_t in1 = vld1_f16(input + 4);
    vst1q_f32(output + 0, vcvt_f32_f16(in0));
    vst1q_f32(output + 4, vcvt_f32_f16(in1));
  }

  static stbir__inline void stbir__float_to_half_SIMD(stbir__FP16 * output, float const * input)
  {
    float16x4_t out0 = vcvt_f16_f32(vld1q_f32(input + 0));
    float16x4_t out1 = vcvt_f16_f32(vld1q_f32(input + 4));
    vst1_f16(output+0, out0);
    vst1_f16(output+4, out1);
  }

  static stbir__inline float stbir__half_to_float( stbir__FP16 h )
  {
    return vgetq_lane_f32(vcvt_f32_f16(vld1_dup_f16(&h)), 0);
  }

  static stbir__inline stbir__FP16 stbir__float_to_half( float f )
  {
    return vget_lane_f16(vcvt_f16_f32(vdupq_n_f32(f)), 0).n16_u16[0];
  }

#elif defined(STBIR_NEON) && ( defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ ) ) // 64-bit ARM

  static stbir__inline void stbir__half_to_float_SIMD(float * output, stbir__FP16 const * input)
  {
    float16x8_t in = vld1q_f16(input);
    vst1q_f32(output + 0, vcvt_f32_f16(vget_low_f16(in)));
    vst1q_f32(output + 4, vcvt_f32_f16(vget_high_f16(in)));
  }

  static stbir__inline void stbir__float_to_half_SIMD(stbir__FP16 * output, float const * input)
  {
    float16x4_t out0 = vcvt_f16_f32(vld1q_f32(input + 0));
    float16x4_t out1 = vcvt_f16_f32(vld1q_f32(input + 4));
    vst1q_f16(output, vcombine_f16(out0, out1));
  }

  static stbir__inline float stbir__half_to_float( stbir__FP16 h )
  {
    return vgetq_lane_f32(vcvt_f32_f16(vdup_n_f16(h)), 0);
  }

  static stbir__inline stbir__FP16 stbir__float_to_half( float f )
  {
    return vget_lane_f16(vcvt_f16_f32(vdupq_n_f32(f)), 0);
  }

#elif defined(STBIR_WASM) || (defined(STBIR_NEON) && (defined(_MSC_VER) || defined(_M_ARM) || defined(__arm__))) // WASM or 32-bit ARM on MSVC/clang

  static stbir__inline void stbir__half_to_float_SIMD(float * output, stbir__FP16 const * input)
  {
    for (int i=0; i<8; i++)
    {
      output[i] = stbir__half_to_float(input[i]);
    }
  }
  static stbir__inline void stbir__float_to_half_SIMD(stbir__FP16 * output, float const * input)
  {
    for (int i=0; i<8; i++)
    {
      output[i] = stbir__float_to_half(input[i]);
    }
  }

#endif


#ifdef STBIR_SIMD

#define stbir__simdf_0123to3333( out, reg ) (out) = stbir__simdf_swiz( reg, 3,3,3,3 )
#define stbir__simdf_0123to2222( out, reg ) (out) = stbir__simdf_swiz( reg, 2,2,2,2 )
#define stbir__simdf_0123to1111( out, reg ) (out) = stbir__simdf_swiz( reg, 1,1,1,1 )
#define stbir__simdf_0123to0000( out, reg ) (out) = stbir__simdf_swiz( reg, 0,0,0,0 )
#define stbir__simdf_0123to0003( out, reg ) (out) = stbir__simdf_swiz( reg, 0,0,0,3 )
#define stbir__simdf_0123to0001( out, reg ) (out) = stbir__simdf_swiz( reg, 0,0,0,1 )
#define stbir__simdf_0123to1122( out, reg ) (out) = stbir__simdf_swiz( reg, 1,1,2,2 )
#define stbir__simdf_0123to2333( out, reg ) (out) = stbir__simdf_swiz( reg, 2,3,3,3 )
#define stbir__simdf_0123to0023( out, reg ) (out) = stbir__simdf_swiz( reg, 0,0,2,3 )
#define stbir__simdf_0123to1230( out, reg ) (out) = stbir__simdf_swiz( reg, 1,2,3,0 )
#define stbir__simdf_0123to2103( out, reg ) (out) = stbir__simdf_swiz( reg, 2,1,0,3 )
#define stbir__simdf_0123to3210( out, reg ) (out) = stbir__simdf_swiz( reg, 3,2,1,0 )
#define stbir__simdf_0123to2301( out, reg ) (out) = stbir__simdf_swiz( reg, 2,3,0,1 )
#define stbir__simdf_0123to3012( out, reg ) (out) = stbir__simdf_swiz( reg, 3,0,1,2 )
#define stbir__simdf_0123to0011( out, reg ) (out) = stbir__simdf_swiz( reg, 0,0,1,1 )
#define stbir__simdf_0123to1100( out, reg ) (out) = stbir__simdf_swiz( reg, 1,1,0,0 )
#define stbir__simdf_0123to2233( out, reg ) (out) = stbir__simdf_swiz( reg, 2,2,3,3 )
#define stbir__simdf_0123to1133( out, reg ) (out) = stbir__simdf_swiz( reg, 1,1,3,3 )
#define stbir__simdf_0123to0022( out, reg ) (out) = stbir__simdf_swiz( reg, 0,0,2,2 )
#define stbir__simdf_0123to1032( out, reg ) (out) = stbir__simdf_swiz( reg, 1,0,3,2 )

typedef union stbir__simdi_u32
{
  stbir_uint32 m128i_u32[4];
  int m128i_i32[4];
  stbir__simdi m128i_i128;
} stbir__simdi_u32;

static const int STBIR_mask[9] = { 0,0,0,-1,-1,-1,0,0,0 };

static const STBIR__SIMDF_CONST(STBIR_max_uint8_as_float,           stbir__max_uint8_as_float);
static const STBIR__SIMDF_CONST(STBIR_max_uint16_as_float,          stbir__max_uint16_as_float);
static const STBIR__SIMDF_CONST(STBIR_max_uint8_as_float_inverted,  stbir__max_uint8_as_float_inverted);
static const STBIR__SIMDF_CONST(STBIR_max_uint16_as_float_inverted, stbir__max_uint16_as_float_inverted);

static const STBIR__SIMDF_CONST(STBIR_simd_point5,   0.5f);
static const STBIR__SIMDF_CONST(STBIR_ones,          1.0f);
static const STBIR__SIMDI_CONST(STBIR_almost_zero,   (127 - 13) << 23);
static const STBIR__SIMDI_CONST(STBIR_almost_one,    0x3f7fffff);
static const STBIR__SIMDI_CONST(STBIR_mantissa_mask, 0xff);
static const STBIR__SIMDI_CONST(STBIR_topscale,      0x02000000);

//   Basically, in simd mode, we unroll the proper amount, and we don't want
//   the non-simd remnant loops to be unroll because they only run a few times
//   Adding this switch saves about 5K on clang which is Captain Unroll the 3rd.
#define STBIR_SIMD_STREAMOUT_PTR( star )  STBIR_STREAMOUT_PTR( star )
#define STBIR_SIMD_NO_UNROLL(ptr) STBIR_NO_UNROLL(ptr)
#define STBIR_SIMD_NO_UNROLL_LOOP_START STBIR_NO_UNROLL_LOOP_START
#define STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR STBIR_NO_UNROLL_LOOP_START_INF_FOR

#ifdef STBIR_MEMCPY
#undef STBIR_MEMCPY
#endif
#define STBIR_MEMCPY stbir_simd_memcpy

// override normal use of memcpy with much simpler copy (faster and smaller with our sized copies)
static void stbir_simd_memcpy( void * dest, void const * src, size_t bytes )
{
  char STBIR_SIMD_STREAMOUT_PTR (*) d = (char*) dest;
  char STBIR_SIMD_STREAMOUT_PTR( * ) d_end = ((char*) dest) + bytes;
  ptrdiff_t ofs_to_src = (char*)src - (char*)dest;

  // check overlaps
  STBIR_ASSERT( ( ( d >= ( (char*)src) + bytes ) ) || ( ( d + bytes ) <= (char*)src ) );

  if ( bytes < (16*stbir__simdfX_float_count) )
  {
    if ( bytes < 16 )
    {
      if ( bytes )
      {
        STBIR_SIMD_NO_UNROLL_LOOP_START
        do
        {
          STBIR_SIMD_NO_UNROLL(d);
          d[ 0 ] = d[ ofs_to_src ];
          ++d;
        } while ( d < d_end );
      }
    }
    else
    {
      stbir__simdf x;
      // do one unaligned to get us aligned for the stream out below
      stbir__simdf_load( x, ( d + ofs_to_src ) );
      stbir__simdf_store( d, x );
      d = (char*)( ( ( (size_t)d ) + 16 ) & ~15 );

      STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
      for(;;)
      {
        STBIR_SIMD_NO_UNROLL(d);

        if ( d > ( d_end - 16 ) )
        {
          if ( d == d_end )
            return;
          d = d_end - 16;
        }

        stbir__simdf_load( x, ( d + ofs_to_src ) );
        stbir__simdf_store( d, x );
        d += 16;
      }
    }
  }
  else
  {
    stbir__simdfX x0,x1,x2,x3;

    // do one unaligned to get us aligned for the stream out below
    stbir__simdfX_load( x0, ( d + ofs_to_src ) +  0*stbir__simdfX_float_count );
    stbir__simdfX_load( x1, ( d + ofs_to_src ) +  4*stbir__simdfX_float_count );
    stbir__simdfX_load( x2, ( d + ofs_to_src ) +  8*stbir__simdfX_float_count );
    stbir__simdfX_load( x3, ( d + ofs_to_src ) + 12*stbir__simdfX_float_count );
    stbir__simdfX_store( d +  0*stbir__simdfX_float_count, x0 );
    stbir__simdfX_store( d +  4*stbir__simdfX_float_count, x1 );
    stbir__simdfX_store( d +  8*stbir__simdfX_float_count, x2 );
    stbir__simdfX_store( d + 12*stbir__simdfX_float_count, x3 );
    d = (char*)( ( ( (size_t)d ) + (16*stbir__simdfX_float_count) ) & ~((16*stbir__simdfX_float_count)-1) );

    STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      STBIR_SIMD_NO_UNROLL(d);

      if ( d > ( d_end - (16*stbir__simdfX_float_count) ) )
      {
        if ( d == d_end )
          return;
        d = d_end - (16*stbir__simdfX_float_count);
      }

      stbir__simdfX_load( x0, ( d + ofs_to_src ) +  0*stbir__simdfX_float_count );
      stbir__simdfX_load( x1, ( d + ofs_to_src ) +  4*stbir__simdfX_float_count );
      stbir__simdfX_load( x2, ( d + ofs_to_src ) +  8*stbir__simdfX_float_count );
      stbir__simdfX_load( x3, ( d + ofs_to_src ) + 12*stbir__simdfX_float_count );
      stbir__simdfX_store( d +  0*stbir__simdfX_float_count, x0 );
      stbir__simdfX_store( d +  4*stbir__simdfX_float_count, x1 );
      stbir__simdfX_store( d +  8*stbir__simdfX_float_count, x2 );
      stbir__simdfX_store( d + 12*stbir__simdfX_float_count, x3 );
      d += (16*stbir__simdfX_float_count);
    }
  }
}

// memcpy that is specically intentionally overlapping (src is smaller then dest, so can be
//   a normal forward copy, bytes is divisible by 4 and bytes is greater than or equal to
//   the diff between dest and src)
static void stbir_overlapping_memcpy( void * dest, void const * src, size_t bytes )
{
  char STBIR_SIMD_STREAMOUT_PTR (*) sd = (char*) src;
  char STBIR_SIMD_STREAMOUT_PTR( * ) s_end = ((char*) src) + bytes;
  ptrdiff_t ofs_to_dest = (char*)dest - (char*)src;

  if ( ofs_to_dest >= 16 ) // is the overlap more than 16 away?
  {
    char STBIR_SIMD_STREAMOUT_PTR( * ) s_end16 = ((char*) src) + (bytes&~15);
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do
    {
      stbir__simdf x;
      STBIR_SIMD_NO_UNROLL(sd);
      stbir__simdf_load( x, sd );
      stbir__simdf_store(  ( sd + ofs_to_dest ), x );
      sd += 16;
    } while ( sd < s_end16 );

    if ( sd == s_end )
      return;
  }

  do
  {
    STBIR_SIMD_NO_UNROLL(sd);
    *(int*)( sd + ofs_to_dest ) = *(int*) sd;
    sd += 4;
  } while ( sd < s_end );
}

#else // no SSE2

// when in scalar mode, we let unrolling happen, so this macro just does the __restrict
#define STBIR_SIMD_STREAMOUT_PTR( star ) STBIR_STREAMOUT_PTR( star )
#define STBIR_SIMD_NO_UNROLL(ptr)
#define STBIR_SIMD_NO_UNROLL_LOOP_START
#define STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR

#endif // SSE2


#ifdef STBIR_PROFILE

#ifndef STBIR_PROFILE_FUNC

#if defined(_x86_64) || defined( __x86_64__ ) || defined( _M_X64 ) || defined(__x86_64) || defined(__SSE2__) || defined(STBIR_SSE) || defined( _M_IX86_FP ) || defined(__i386) || defined( __i386__ ) || defined( _M_IX86 ) || defined( _X86_ )

#ifdef _MSC_VER

  STBIRDEF stbir_uint64 __rdtsc();
  #define STBIR_PROFILE_FUNC() __rdtsc()

#else // non msvc

  static stbir__inline stbir_uint64 STBIR_PROFILE_FUNC()
  {
    stbir_uint32 lo, hi;
    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi) );
    return ( ( (stbir_uint64) hi ) << 32 ) | ( (stbir_uint64) lo );
  }

#endif  // msvc

#elif defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ ) || defined(__ARM_NEON__)

#if defined( _MSC_VER ) && !defined(__clang__)

  #define STBIR_PROFILE_FUNC() _ReadStatusReg(ARM64_CNTVCT)

#else

  static stbir__inline stbir_uint64 STBIR_PROFILE_FUNC()
  {
    stbir_uint64 tsc;
    asm volatile("mrs %0, cntvct_el0" : "=r" (tsc));
    return tsc;
  }

#endif

#else // x64, arm

#error Unknown platform for profiling.

#endif  // x64, arm

#endif // STBIR_PROFILE_FUNC

#define STBIR_ONLY_PROFILE_GET_SPLIT_INFO ,stbir__per_split_info * split_info
#define STBIR_ONLY_PROFILE_SET_SPLIT_INFO ,split_info

#define STBIR_ONLY_PROFILE_BUILD_GET_INFO ,stbir__info * profile_info
#define STBIR_ONLY_PROFILE_BUILD_SET_INFO ,profile_info

// super light-weight micro profiler
#define STBIR_PROFILE_START_ll( info, wh ) { stbir_uint64 wh##thiszonetime = STBIR_PROFILE_FUNC(); stbir_uint64 * wh##save_parent_excluded_ptr = info->current_zone_excluded_ptr; stbir_uint64 wh##current_zone_excluded = 0; info->current_zone_excluded_ptr = &wh##current_zone_excluded;
#define STBIR_PROFILE_END_ll( info, wh ) wh##thiszonetime = STBIR_PROFILE_FUNC() - wh##thiszonetime; info->profile.named.wh += wh##thiszonetime - wh##current_zone_excluded; *wh##save_parent_excluded_ptr += wh##thiszonetime; info->current_zone_excluded_ptr = wh##save_parent_excluded_ptr; }
#define STBIR_PROFILE_FIRST_START_ll( info, wh ) { int i; info->current_zone_excluded_ptr = &info->profile.named.total; for(i=0;i<STBIR__ARRAY_SIZE(info->profile.array);i++) info->profile.array[i]=0; } STBIR_PROFILE_START_ll( info, wh );
#define STBIR_PROFILE_CLEAR_EXTRAS_ll( info, num ) { int extra; for(extra=1;extra<(num);extra++) { int i; for(i=0;i<STBIR__ARRAY_SIZE((info)->profile.array);i++) (info)[extra].profile.array[i]=0; } }

// for thread data
#define STBIR_PROFILE_START( wh ) STBIR_PROFILE_START_ll( split_info, wh )
#define STBIR_PROFILE_END( wh ) STBIR_PROFILE_END_ll( split_info, wh )
#define STBIR_PROFILE_FIRST_START( wh ) STBIR_PROFILE_FIRST_START_ll( split_info, wh )
#define STBIR_PROFILE_CLEAR_EXTRAS() STBIR_PROFILE_CLEAR_EXTRAS_ll( split_info, split_count )

// for build data
#define STBIR_PROFILE_BUILD_START( wh ) STBIR_PROFILE_START_ll( profile_info, wh )
#define STBIR_PROFILE_BUILD_END( wh ) STBIR_PROFILE_END_ll( profile_info, wh )
#define STBIR_PROFILE_BUILD_FIRST_START( wh ) STBIR_PROFILE_FIRST_START_ll( profile_info, wh )
#define STBIR_PROFILE_BUILD_CLEAR( info ) { int i; for(i=0;i<STBIR__ARRAY_SIZE(info->profile.array);i++) info->profile.array[i]=0; }

#else  // no profile

#define STBIR_ONLY_PROFILE_GET_SPLIT_INFO
#define STBIR_ONLY_PROFILE_SET_SPLIT_INFO

#define STBIR_ONLY_PROFILE_BUILD_GET_INFO
#define STBIR_ONLY_PROFILE_BUILD_SET_INFO

#define STBIR_PROFILE_START( wh )
#define STBIR_PROFILE_END( wh )
#define STBIR_PROFILE_FIRST_START( wh )
#define STBIR_PROFILE_CLEAR_EXTRAS( )

#define STBIR_PROFILE_BUILD_START( wh )
#define STBIR_PROFILE_BUILD_END( wh )
#define STBIR_PROFILE_BUILD_FIRST_START( wh )
#define STBIR_PROFILE_BUILD_CLEAR( info )

#endif  // stbir_profile

#ifndef STBIR_CEILF
#include <math.h>
#if _MSC_VER <= 1200 // support VC6 for Sean
#define STBIR_CEILF(x) ((float)ceil((float)(x)))
#define STBIR_FLOORF(x) ((float)floor((float)(x)))
#else
#define STBIR_CEILF(x) ceilf(x)
#define STBIR_FLOORF(x) floorf(x)
#endif
#endif

#ifndef STBIR_MEMCPY
// For memcpy
#include <string.h>
#define STBIR_MEMCPY( dest, src, len ) memcpy( dest, src, len )
#endif

#ifndef STBIR_SIMD

// memcpy that is specifically intentionally overlapping (src is smaller then dest, so can be
//   a normal forward copy, bytes is divisible by 4 and bytes is greater than or equal to
//   the diff between dest and src)
static void stbir_overlapping_memcpy( void * dest, void const * src, size_t bytes )
{
  char STBIR_SIMD_STREAMOUT_PTR (*) sd = (char*) src;
  char STBIR_SIMD_STREAMOUT_PTR( * ) s_end = ((char*) src) + bytes;
  ptrdiff_t ofs_to_dest = (char*)dest - (char*)src;

  if ( ofs_to_dest >= 8 ) // is the overlap more than 8 away
  {
    char STBIR_SIMD_STREAMOUT_PTR( * ) s_end8 = ((char*) src) + (bytes&~7);

    if ( ( ( ((ptrdiff_t)dest)|((ptrdiff_t)src) ) & 7 ) == 0 ) // is it 8byte aligned?
    {
      STBIR_NO_UNROLL_LOOP_START
      do
      {
        STBIR_NO_UNROLL(sd);
        *(stbir_uint64*)( sd + ofs_to_dest ) = *(stbir_uint64*) sd;
        sd += 8;
      } while ( sd < s_end8 );
    }
    else
    {
      STBIR_NO_UNROLL_LOOP_START
      do
      {
        int a,b;
        STBIR_NO_UNROLL(sd);
        a = ((int*)sd)[0];
        b = ((int*)sd)[1];
        ((int*)( sd + ofs_to_dest ))[0] = a;
        ((int*)( sd + ofs_to_dest ))[1] = b;
        sd += 8;
      } while ( sd < s_end8 );
    }

    if ( sd == s_end )
      return;
  }

  STBIR_NO_UNROLL_LOOP_START
  do
  {
    STBIR_NO_UNROLL(sd);
    *(int*)( sd + ofs_to_dest ) = *(int*) sd;
    sd += 4;
  } while ( sd < s_end );
}

#endif
