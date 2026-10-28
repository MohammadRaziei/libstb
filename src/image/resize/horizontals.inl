// Template: the horizontal resamplers for STBIR__horizontal_channels channels.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

#define STBIR_chans( start, end ) STBIR_strs_join1(start,STBIR__horizontal_channels,end)

#ifndef stbir__2_coeff_only
#define stbir__2_coeff_only()             \
    stbir__1_coeff_only();                \
    stbir__1_coeff_remnant(1);
#endif

#ifndef stbir__2_coeff_remnant
#define stbir__2_coeff_remnant( ofs )     \
    stbir__1_coeff_remnant(ofs);          \
    stbir__1_coeff_remnant((ofs)+1);
#endif

#ifndef stbir__3_coeff_only
#define stbir__3_coeff_only()             \
    stbir__2_coeff_only();                \
    stbir__1_coeff_remnant(2);
#endif

#ifndef stbir__3_coeff_remnant
#define stbir__3_coeff_remnant( ofs )     \
    stbir__2_coeff_remnant(ofs);          \
    stbir__1_coeff_remnant((ofs)+2);
#endif

#ifndef stbir__3_coeff_setup
#define stbir__3_coeff_setup()
#endif

#ifndef stbir__4_coeff_start
#define stbir__4_coeff_start()            \
    stbir__2_coeff_only();                \
    stbir__2_coeff_remnant(2);
#endif

#ifndef stbir__4_coeff_continue_from_4
#define stbir__4_coeff_continue_from_4( ofs )     \
    stbir__2_coeff_remnant(ofs);                  \
    stbir__2_coeff_remnant((ofs)+2);
#endif

#ifndef stbir__store_output_tiny
#define stbir__store_output_tiny stbir__store_output
#endif

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_1_coeff)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__1_coeff_only();
    stbir__store_output_tiny();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_2_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__2_coeff_only();
    stbir__store_output_tiny();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_3_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__3_coeff_only();
    stbir__store_output_tiny();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_4_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_5_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__1_coeff_remnant(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_6_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__2_coeff_remnant(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_7_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  stbir__3_coeff_setup();
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    stbir__3_coeff_remnant(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_8_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_9_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__1_coeff_remnant(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_10_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__2_coeff_remnant(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_11_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  stbir__3_coeff_setup();
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__3_coeff_remnant(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_12_coeffs)( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    float const * hc = horizontal_coefficients;
    stbir__4_coeff_start();
    stbir__4_coeff_continue_from_4(4);
    stbir__4_coeff_continue_from_4(8);
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod0 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 4 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod1 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 5 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__1_coeff_remnant( 4 );
    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod2 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 6 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__2_coeff_remnant( 4 );

    stbir__store_output();
  } while ( output < output_end );
}

static void STBIR_chans( stbir__horizontal_gather_,_channels_with_n_coeffs_mod3 )( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer, stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width )
{
  float const * output_end = output_buffer + output_sub_size * STBIR__horizontal_channels;
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = output_buffer;
  stbir__3_coeff_setup();
  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float const * decode = decode_buffer + horizontal_contributors->n0 * STBIR__horizontal_channels;
    int n = ( ( horizontal_contributors->n1 - horizontal_contributors->n0 + 1 ) - 7 + 3 ) >> 2;
    float const * hc = horizontal_coefficients;

    stbir__4_coeff_start();
    STBIR_SIMD_NO_UNROLL_LOOP_START
    do {
      hc += 4;
      decode += STBIR__horizontal_channels * 4;
      stbir__4_coeff_continue_from_4( 0 );
      --n;
    } while ( n > 0 );
    stbir__3_coeff_remnant( 4 );

    stbir__store_output();
  } while ( output < output_end );
}

static stbir__horizontal_gather_channels_func * STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_funcs)[4]=
{
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod0),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod1),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod2),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_n_coeffs_mod3),
};

static stbir__horizontal_gather_channels_func * STBIR_chans(stbir__horizontal_gather_,_channels_funcs)[12]=
{
  STBIR_chans(stbir__horizontal_gather_,_channels_with_1_coeff),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_2_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_3_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_4_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_5_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_6_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_7_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_8_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_9_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_10_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_11_coeffs),
  STBIR_chans(stbir__horizontal_gather_,_channels_with_12_coeffs),
};

#undef STBIR__horizontal_channels
#undef STB_IMAGE_RESIZE_DO_HORIZONTALS
#undef stbir__1_coeff_only
#undef stbir__1_coeff_remnant
#undef stbir__2_coeff_only
#undef stbir__2_coeff_remnant
#undef stbir__3_coeff_only
#undef stbir__3_coeff_remnant
#undef stbir__3_coeff_setup
#undef stbir__4_coeff_start
#undef stbir__4_coeff_continue_from_4
#undef stbir__store_output
#undef stbir__store_output_tiny
#undef STBIR_chans
