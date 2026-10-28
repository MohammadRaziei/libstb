// Scanline coders for every pixel layout (instances of coders.inl), alpha weighting and unweighting.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

//========================================================================================================
// scanline decoders and encoders

#define stbir__coder_min_num 1
#include "image/resize/coders.inl"

#define stbir__decode_suffix BGRA
#define stbir__decode_swizzle
#define stbir__decode_order0  2
#define stbir__decode_order1  1
#define stbir__decode_order2  0
#define stbir__decode_order3  3
#define stbir__encode_order0  2
#define stbir__encode_order1  1
#define stbir__encode_order2  0
#define stbir__encode_order3  3
#define stbir__coder_min_num 4
#include "image/resize/coders.inl"

#define stbir__decode_suffix ARGB
#define stbir__decode_swizzle
#define stbir__decode_order0  1
#define stbir__decode_order1  2
#define stbir__decode_order2  3
#define stbir__decode_order3  0
#define stbir__encode_order0  3
#define stbir__encode_order1  0
#define stbir__encode_order2  1
#define stbir__encode_order3  2
#define stbir__coder_min_num 4
#include "image/resize/coders.inl"

#define stbir__decode_suffix ABGR
#define stbir__decode_swizzle
#define stbir__decode_order0  3
#define stbir__decode_order1  2
#define stbir__decode_order2  1
#define stbir__decode_order3  0
#define stbir__encode_order0  3
#define stbir__encode_order1  2
#define stbir__encode_order2  1
#define stbir__encode_order3  0
#define stbir__coder_min_num 4
#include "image/resize/coders.inl"

#define stbir__decode_suffix AR
#define stbir__decode_swizzle
#define stbir__decode_order0  1
#define stbir__decode_order1  0
#define stbir__decode_order2  3
#define stbir__decode_order3  2
#define stbir__encode_order0  1
#define stbir__encode_order1  0
#define stbir__encode_order2  3
#define stbir__encode_order3  2
#define stbir__coder_min_num 2
#include "image/resize/coders.inl"


// fancy alpha means we expand to keep both premultipied and non-premultiplied color channels
static void stbir__fancy_alpha_weight_4ch( float * out_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) out = out_buffer;
  float const * end_decode = out_buffer + ( width_times_channels / 4 ) * 7;  // decode buffer aligned to end of out_buffer
  float STBIR_STREAMOUT_PTR(*) decode = (float*)end_decode - width_times_channels;

  // fancy alpha is stored internally as R G B A Rpm Gpm Bpm

  #ifdef STBIR_SIMD

  #ifdef STBIR_SIMD8
  decode += 16;
  STBIR_NO_UNROLL_LOOP_START
  while ( decode <= end_decode )
  {
    stbir__simdf8 d0,d1,a0,a1,p0,p1;
    STBIR_NO_UNROLL(decode);
    stbir__simdf8_load( d0, decode-16 );
    stbir__simdf8_load( d1, decode-16+8 );
    stbir__simdf8_0123to33333333( a0, d0 );
    stbir__simdf8_0123to33333333( a1, d1 );
    stbir__simdf8_mult( p0, a0, d0 );
    stbir__simdf8_mult( p1, a1, d1 );
    stbir__simdf8_bot4s( a0, d0, p0 );
    stbir__simdf8_bot4s( a1, d1, p1 );
    stbir__simdf8_top4s( d0, d0, p0 );
    stbir__simdf8_top4s( d1, d1, p1 );
    stbir__simdf8_store ( out, a0 );
    stbir__simdf8_store ( out+7, d0 );
    stbir__simdf8_store ( out+14, a1 );
    stbir__simdf8_store ( out+21, d1 );
    decode += 16;
    out += 28;
  }
  decode -= 16;
  #else
  decode += 8;
  STBIR_NO_UNROLL_LOOP_START
  while ( decode <= end_decode )
  {
    stbir__simdf d0,a0,d1,a1,p0,p1;
    STBIR_NO_UNROLL(decode);
    stbir__simdf_load( d0, decode-8 );
    stbir__simdf_load( d1, decode-8+4 );
    stbir__simdf_0123to3333( a0, d0 );
    stbir__simdf_0123to3333( a1, d1 );
    stbir__simdf_mult( p0, a0, d0 );
    stbir__simdf_mult( p1, a1, d1 );
    stbir__simdf_store ( out, d0 );
    stbir__simdf_store ( out+4, p0 );
    stbir__simdf_store ( out+7, d1 );
    stbir__simdf_store ( out+7+4, p1 );
    decode += 8;
    out += 14;
  }
  decode -= 8;
  #endif

  // might be one last odd pixel
  #ifdef STBIR_SIMD8
  STBIR_NO_UNROLL_LOOP_START
  while ( decode < end_decode )
  #else
  if ( decode < end_decode )
  #endif
  {
    stbir__simdf d,a,p;
    STBIR_NO_UNROLL(decode);
    stbir__simdf_load( d, decode );
    stbir__simdf_0123to3333( a, d );
    stbir__simdf_mult( p, a, d );
    stbir__simdf_store ( out, d );
    stbir__simdf_store ( out+4, p );
    decode += 4;
    out += 7;
  }

  #else

  while( decode < end_decode )
  {
    float r = decode[0], g = decode[1], b = decode[2], alpha = decode[3];
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = alpha;
    out[4] = r * alpha;
    out[5] = g * alpha;
    out[6] = b * alpha;
    out += 7;
    decode += 4;
  }

  #endif
}

static void stbir__fancy_alpha_weight_2ch( float * out_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) out = out_buffer;
  float const * end_decode = out_buffer + ( width_times_channels / 2 ) * 3;
  float STBIR_STREAMOUT_PTR(*) decode = (float*)end_decode - width_times_channels;

  //  for fancy alpha, turns into: [X A Xpm][X A Xpm],etc

  #ifdef STBIR_SIMD

  decode += 8;
  if ( decode <= end_decode )
  {
    STBIR_NO_UNROLL_LOOP_START
    do {
      #ifdef STBIR_SIMD8
      stbir__simdf8 d0,a0,p0;
      STBIR_NO_UNROLL(decode);
      stbir__simdf8_load( d0, decode-8 );
      stbir__simdf8_0123to11331133( p0, d0 );
      stbir__simdf8_0123to00220022( a0, d0 );
      stbir__simdf8_mult( p0, p0, a0 );

      stbir__simdf_store2( out, stbir__if_simdf8_cast_to_simdf4( d0 ) );
      stbir__simdf_store( out+2, stbir__if_simdf8_cast_to_simdf4( p0 ) );
      stbir__simdf_store2h( out+3, stbir__if_simdf8_cast_to_simdf4( d0 ) );

      stbir__simdf_store2( out+6, stbir__simdf8_gettop4( d0 ) );
      stbir__simdf_store( out+8, stbir__simdf8_gettop4( p0 ) );
      stbir__simdf_store2h( out+9, stbir__simdf8_gettop4( d0 ) );
      #else
      stbir__simdf d0,a0,d1,a1,p0,p1;
      STBIR_NO_UNROLL(decode);
      stbir__simdf_load( d0, decode-8 );
      stbir__simdf_load( d1, decode-8+4 );
      stbir__simdf_0123to1133( p0, d0 );
      stbir__simdf_0123to1133( p1, d1 );
      stbir__simdf_0123to0022( a0, d0 );
      stbir__simdf_0123to0022( a1, d1 );
      stbir__simdf_mult( p0, p0, a0 );
      stbir__simdf_mult( p1, p1, a1 );

      stbir__simdf_store2( out, d0 );
      stbir__simdf_store( out+2, p0 );
      stbir__simdf_store2h( out+3, d0 );

      stbir__simdf_store2( out+6, d1 );
      stbir__simdf_store( out+8, p1 );
      stbir__simdf_store2h( out+9, d1 );
      #endif
      decode += 8;
      out += 12;
    } while ( decode <= end_decode );
  }
  decode -= 8;
  #endif

  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode < end_decode )
  {
    float x = decode[0], y = decode[1];
    STBIR_SIMD_NO_UNROLL(decode);
    out[0] = x;
    out[1] = y;
    out[2] = x * y;
    out += 3;
    decode += 2;
  }
}

static void stbir__fancy_alpha_unweight_4ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float STBIR_SIMD_STREAMOUT_PTR(*) input = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  // fancy RGBA is stored internally as R G B A Rpm Gpm Bpm

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float alpha = input[3];
#ifdef STBIR_SIMD
    stbir__simdf i,ia;
    STBIR_SIMD_NO_UNROLL(encode);
    if ( alpha < stbir__small_float )
    {
      stbir__simdf_load( i, input );
      stbir__simdf_store( encode, i );
    }
    else
    {
      stbir__simdf_load1frep4( ia, 1.0f / alpha );
      stbir__simdf_load( i, input+4 );
      stbir__simdf_mult( i, i, ia );
      stbir__simdf_store( encode, i );
      encode[3] = alpha;
    }
#else
    if ( alpha < stbir__small_float )
    {
      encode[0] = input[0];
      encode[1] = input[1];
      encode[2] = input[2];
    }
    else
    {
      float ialpha = 1.0f / alpha;
      encode[0] = input[4] * ialpha;
      encode[1] = input[5] * ialpha;
      encode[2] = input[6] * ialpha;
    }
    encode[3] = alpha;
#endif

    input += 7;
    encode += 4;
  } while ( encode < end_output );
}

//  format: [X A Xpm][X A Xpm] etc
static void stbir__fancy_alpha_unweight_2ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float STBIR_SIMD_STREAMOUT_PTR(*) input = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  do {
    float alpha = input[1];
    encode[0] = input[0];
    if ( alpha >= stbir__small_float )
      encode[0] = input[2] / alpha;
    encode[1] = alpha;

    input += 3;
    encode += 2;
  } while ( encode < end_output );
}

static void stbir__simple_alpha_weight_4ch( float * decode_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) decode = decode_buffer;
  float const * end_decode = decode_buffer + width_times_channels;

  #ifdef STBIR_SIMD
  {
    decode += 2 * stbir__simdfX_float_count;
    STBIR_NO_UNROLL_LOOP_START
    while ( decode <= end_decode )
    {
      stbir__simdfX d0,a0,d1,a1;
      STBIR_NO_UNROLL(decode);
      stbir__simdfX_load( d0, decode-2*stbir__simdfX_float_count );
      stbir__simdfX_load( d1, decode-2*stbir__simdfX_float_count+stbir__simdfX_float_count );
      stbir__simdfX_aaa1( a0, d0, STBIR_onesX );
      stbir__simdfX_aaa1( a1, d1, STBIR_onesX );
      stbir__simdfX_mult( d0, d0, a0 );
      stbir__simdfX_mult( d1, d1, a1 );
      stbir__simdfX_store ( decode-2*stbir__simdfX_float_count, d0 );
      stbir__simdfX_store ( decode-2*stbir__simdfX_float_count+stbir__simdfX_float_count, d1 );
      decode += 2 * stbir__simdfX_float_count;
    }
    decode -= 2 * stbir__simdfX_float_count;

    // few last pixels remnants
    #ifdef STBIR_SIMD8
    STBIR_NO_UNROLL_LOOP_START
    while ( decode < end_decode )
    #else
    if ( decode < end_decode )
    #endif
    {
      stbir__simdf d,a;
      stbir__simdf_load( d, decode );
      stbir__simdf_aaa1( a, d, STBIR__CONSTF(STBIR_ones) );
      stbir__simdf_mult( d, d, a );
      stbir__simdf_store ( decode, d );
      decode += 4;
    }
  }

  #else

  while( decode < end_decode )
  {
    float alpha = decode[3];
    decode[0] *= alpha;
    decode[1] *= alpha;
    decode[2] *= alpha;
    decode += 4;
  }

  #endif
}

static void stbir__simple_alpha_weight_2ch( float * decode_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) decode = decode_buffer;
  float const * end_decode = decode_buffer + width_times_channels;

  #ifdef STBIR_SIMD
  decode += 2 * stbir__simdfX_float_count;
  STBIR_NO_UNROLL_LOOP_START
  while ( decode <= end_decode )
  {
    stbir__simdfX d0,a0,d1,a1;
    STBIR_NO_UNROLL(decode);
    stbir__simdfX_load( d0, decode-2*stbir__simdfX_float_count );
    stbir__simdfX_load( d1, decode-2*stbir__simdfX_float_count+stbir__simdfX_float_count );
    stbir__simdfX_a1a1( a0, d0, STBIR_onesX );
    stbir__simdfX_a1a1( a1, d1, STBIR_onesX );
    stbir__simdfX_mult( d0, d0, a0 );
    stbir__simdfX_mult( d1, d1, a1 );
    stbir__simdfX_store ( decode-2*stbir__simdfX_float_count, d0 );
    stbir__simdfX_store ( decode-2*stbir__simdfX_float_count+stbir__simdfX_float_count, d1 );
    decode += 2 * stbir__simdfX_float_count;
  }
  decode -= 2 * stbir__simdfX_float_count;
  #endif

  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode < end_decode )
  {
    float alpha = decode[1];
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0] *= alpha;
    decode += 2;
  }
}

static void stbir__simple_alpha_unweight_4ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float alpha = encode[3];

#ifdef STBIR_SIMD
    stbir__simdf i,ia;
    STBIR_SIMD_NO_UNROLL(encode);
    if ( alpha >= stbir__small_float )
    {
      stbir__simdf_load1frep4( ia, 1.0f / alpha );
      stbir__simdf_load( i, encode );
      stbir__simdf_mult( i, i, ia );
      stbir__simdf_store( encode, i );
      encode[3] = alpha;
    }
#else
    if ( alpha >= stbir__small_float )
    {
      float ialpha = 1.0f / alpha;
      encode[0] *= ialpha;
      encode[1] *= ialpha;
      encode[2] *= ialpha;
    }
#endif
    encode += 4;
  } while ( encode < end_output );
}

static void stbir__simple_alpha_unweight_2ch( float * encode_buffer, int width_times_channels )
{
  float STBIR_SIMD_STREAMOUT_PTR(*) encode = encode_buffer;
  float const * end_output = encode_buffer + width_times_channels;

  do {
    float alpha = encode[1];
    if ( alpha >= stbir__small_float )
      encode[0] /= alpha;
    encode += 2;
  } while ( encode < end_output );
}


// only used in RGB->BGR or BGR->RGB
static void stbir__simple_flip_3ch( float * decode_buffer, int width_times_channels )
{
  float STBIR_STREAMOUT_PTR(*) decode = decode_buffer;
  float const * end_decode = decode_buffer + width_times_channels;

#ifdef STBIR_SIMD
    #ifdef stbir__simdf_swiz2 // do we have two argument swizzles?
      end_decode -= 12; 
      STBIR_NO_UNROLL_LOOP_START
      while( decode <= end_decode )
      {
        // on arm64 8 instructions, no overlapping stores
        stbir__simdf a,b,c,na,nb;
        STBIR_SIMD_NO_UNROLL(decode);
        stbir__simdf_load( a, decode );
        stbir__simdf_load( b, decode+4 );
        stbir__simdf_load( c, decode+8 );

        na = stbir__simdf_swiz2( a, b, 2, 1, 0, 5 );   
        b  = stbir__simdf_swiz2( a, b, 4, 3, 6, 7 );   
        nb = stbir__simdf_swiz2( b, c, 0, 1, 4, 3 );   
        c  = stbir__simdf_swiz2( b, c, 2, 7, 6, 5 );   

        stbir__simdf_store( decode, na );
        stbir__simdf_store( decode+4, nb ); 
        stbir__simdf_store( decode+8, c );
        decode += 12;
      }
      end_decode += 12;
    #else
      end_decode -= 24;
      STBIR_NO_UNROLL_LOOP_START
      while( decode <= end_decode )
      {
        // 26 instructions on x64
        stbir__simdf a,b,c,d,e,f,g;
        float i21, i23;
        STBIR_SIMD_NO_UNROLL(decode);
        stbir__simdf_load( a, decode );
        stbir__simdf_load( b, decode+3 );
        stbir__simdf_load( c, decode+6 );
        stbir__simdf_load( d, decode+9 );
        stbir__simdf_load( e, decode+12 );
        stbir__simdf_load( f, decode+15 );
        stbir__simdf_load( g, decode+18 );

        a = stbir__simdf_swiz( a, 2, 1, 0, 3 );   
        b = stbir__simdf_swiz( b, 2, 1, 0, 3 );   
        c = stbir__simdf_swiz( c, 2, 1, 0, 3 );   
        d = stbir__simdf_swiz( d, 2, 1, 0, 3 );   
        e = stbir__simdf_swiz( e, 2, 1, 0, 3 );   
        f = stbir__simdf_swiz( f, 2, 1, 0, 3 );   
        g = stbir__simdf_swiz( g, 2, 1, 0, 3 );   

        // stores overlap, need to be in order, 
        stbir__simdf_store( decode,    a );
        i21 = decode[21];
        stbir__simdf_store( decode+3,  b ); 
        i23 = decode[23];
        stbir__simdf_store( decode+6,  c );
        stbir__simdf_store( decode+9,  d );
        stbir__simdf_store( decode+12, e );
        stbir__simdf_store( decode+15, f );
        stbir__simdf_store( decode+18, g );
        decode[21] = i23;
        decode[23] = i21;
        decode += 24;
      }
      end_decode += 24;
    #endif
#else
  end_decode -= 12;
  STBIR_NO_UNROLL_LOOP_START
  while( decode <= end_decode )
  {
    // 16 instructions
    float t0,t1,t2,t3;
    STBIR_NO_UNROLL(decode);
    t0 = decode[0]; t1 = decode[3]; t2 = decode[6]; t3 = decode[9];
    decode[0] = decode[2]; decode[3] = decode[5]; decode[6] = decode[8]; decode[9] = decode[11];
    decode[2] = t0; decode[5] = t1; decode[8] = t2; decode[11] = t3;
    decode += 12;
  }
  end_decode += 12;
#endif

  STBIR_NO_UNROLL_LOOP_START
  while( decode < end_decode )
  {
    float t = decode[0];
    STBIR_NO_UNROLL(decode);
    decode[0] = decode[2];
    decode[2] = t;
    decode += 3;
  }
}
