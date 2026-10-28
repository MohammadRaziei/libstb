// Template: the scanline decoder and encoder of one pixel layout (the macros stbir__decode_* set the layout).
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

#ifdef stbir__decode_suffix
#define STBIR__CODER_NAME( name ) STBIR_strs_join1( name, _, stbir__decode_suffix )
#else
#define STBIR__CODER_NAME( name ) name
#endif

#ifdef stbir__decode_swizzle
#define stbir__decode_simdf8_flip(reg) STBIR_strs_join1( STBIR_strs_join1( STBIR_strs_join1( STBIR_strs_join1( stbir__simdf8_0123to,stbir__decode_order0,stbir__decode_order1),stbir__decode_order2,stbir__decode_order3),stbir__decode_order0,stbir__decode_order1),stbir__decode_order2,stbir__decode_order3)(reg, reg)
#define stbir__decode_simdf4_flip(reg) STBIR_strs_join1( STBIR_strs_join1( stbir__simdf_0123to,stbir__decode_order0,stbir__decode_order1),stbir__decode_order2,stbir__decode_order3)(reg, reg)
#define stbir__encode_simdf8_unflip(reg) STBIR_strs_join1( STBIR_strs_join1( STBIR_strs_join1( STBIR_strs_join1( stbir__simdf8_0123to,stbir__encode_order0,stbir__encode_order1),stbir__encode_order2,stbir__encode_order3),stbir__encode_order0,stbir__encode_order1),stbir__encode_order2,stbir__encode_order3)(reg, reg)
#define stbir__encode_simdf4_unflip(reg) STBIR_strs_join1( STBIR_strs_join1( stbir__simdf_0123to,stbir__encode_order0,stbir__encode_order1),stbir__encode_order2,stbir__encode_order3)(reg, reg)
#else
#define stbir__decode_order0 0
#define stbir__decode_order1 1
#define stbir__decode_order2 2
#define stbir__decode_order3 3
#define stbir__encode_order0 0
#define stbir__encode_order1 1
#define stbir__encode_order2 2
#define stbir__encode_order3 3
#define stbir__decode_simdf8_flip(reg)
#define stbir__decode_simdf4_flip(reg)
#define stbir__encode_simdf8_unflip(reg)
#define stbir__encode_simdf4_unflip(reg)
#endif

#ifdef STBIR_SIMD8
#define stbir__encode_simdfX_unflip  stbir__encode_simdf8_unflip
#else
#define stbir__encode_simdfX_unflip  stbir__encode_simdf4_unflip
#endif

static float * STBIR__CODER_NAME( stbir__decode_uint8_linear_scaled )( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const*)inputp;

  #ifdef STBIR_SIMD
  unsigned char const * end_input_m16 = input + width_times_channels - 16;
  if ( width_times_channels >= 16 )
  {
    decode_end -= 16;
    STBIR_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      #ifdef STBIR_SIMD8
      stbir__simdi i; stbir__simdi8 o0,o1;
      stbir__simdf8 of0, of1;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi8_expand_u8_to_u32( o0, o1, i );
      stbir__simdi8_convert_i32_to_float( of0, o0 );
      stbir__simdi8_convert_i32_to_float( of1, o1 );
      stbir__simdf8_mult( of0, of0, STBIR_max_uint8_as_float_inverted8);
      stbir__simdf8_mult( of1, of1, STBIR_max_uint8_as_float_inverted8);
      stbir__decode_simdf8_flip( of0 );
      stbir__decode_simdf8_flip( of1 );
      stbir__simdf8_store( decode + 0, of0 );
      stbir__simdf8_store( decode + 8, of1 );
      #else
      stbir__simdi i, o0, o1, o2, o3;
      stbir__simdf of0, of1, of2, of3;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi_expand_u8_to_u32( o0,o1,o2,o3,i);
      stbir__simdi_convert_i32_to_float( of0, o0 );
      stbir__simdi_convert_i32_to_float( of1, o1 );
      stbir__simdi_convert_i32_to_float( of2, o2 );
      stbir__simdi_convert_i32_to_float( of3, o3 );
      stbir__simdf_mult( of0, of0, STBIR__CONSTF(STBIR_max_uint8_as_float_inverted) );
      stbir__simdf_mult( of1, of1, STBIR__CONSTF(STBIR_max_uint8_as_float_inverted) );
      stbir__simdf_mult( of2, of2, STBIR__CONSTF(STBIR_max_uint8_as_float_inverted) );
      stbir__simdf_mult( of3, of3, STBIR__CONSTF(STBIR_max_uint8_as_float_inverted) );
      stbir__decode_simdf4_flip( of0 );
      stbir__decode_simdf4_flip( of1 );
      stbir__decode_simdf4_flip( of2 );
      stbir__decode_simdf4_flip( of3 );
      stbir__simdf_store( decode + 0,  of0 );
      stbir__simdf_store( decode + 4,  of1 );
      stbir__simdf_store( decode + 8,  of2 );
      stbir__simdf_store( decode + 12, of3 );
      #endif
      decode += 16;
      input += 16;
      if ( decode <= decode_end )
        continue;
      if ( decode == ( decode_end + 16 ) )
        break;
      decode = decode_end; // backup and do last couple
      input = end_input_m16;
    }
    return decode_end + 16;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0])) * stbir__max_uint8_as_float_inverted;
    decode[1-4] = ((float)(input[stbir__decode_order1])) * stbir__max_uint8_as_float_inverted;
    decode[2-4] = ((float)(input[stbir__decode_order2])) * stbir__max_uint8_as_float_inverted;
    decode[3-4] = ((float)(input[stbir__decode_order3])) * stbir__max_uint8_as_float_inverted;
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0])) * stbir__max_uint8_as_float_inverted;
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1])) * stbir__max_uint8_as_float_inverted;
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2])) * stbir__max_uint8_as_float_inverted;
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif

  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_uint8_linear_scaled )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char *) outputp;
  unsigned char * end_output = ( (unsigned char *) output ) + width_times_channels;

  #ifdef STBIR_SIMD
  if ( width_times_channels >= stbir__simdfX_float_count*2 )
  {
    float const * end_encode_m8 = encode + width_times_channels - stbir__simdfX_float_count*2;
    end_output -= stbir__simdfX_float_count*2;
    STBIR_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      stbir__simdfX e0, e1;
      stbir__simdi i;
      STBIR_SIMD_NO_UNROLL(encode);
      stbir__simdfX_madd_mem( e0, STBIR_simd_point5X, STBIR_max_uint8_as_floatX, encode );
      stbir__simdfX_madd_mem( e1, STBIR_simd_point5X, STBIR_max_uint8_as_floatX, encode+stbir__simdfX_float_count );
      stbir__encode_simdfX_unflip( e0 );
      stbir__encode_simdfX_unflip( e1 );
      #ifdef STBIR_SIMD8
      stbir__simdf8_pack_to_16bytes( i, e0, e1 );
      stbir__simdi_store( output, i );
      #else
      stbir__simdf_pack_to_8bytes( i, e0, e1 );
      stbir__simdi_store2( output, i );
      #endif
      encode += stbir__simdfX_float_count*2;
      output += stbir__simdfX_float_count*2;
      if ( output <= end_output )
        continue;
      if ( output == ( end_output + stbir__simdfX_float_count*2 ) )
        break;
      output = end_output; // backup and do last couple
      encode = end_encode_m8;
    }
    return;
  }

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    stbir__simdf e0;
    stbir__simdi i0;
    STBIR_NO_UNROLL(encode);
    stbir__simdf_load( e0, encode );
    stbir__simdf_madd( e0, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint8_as_float), e0 );
    stbir__encode_simdf4_unflip( e0 );
    stbir__simdf_pack_to_8bytes( i0, e0, e0 );  // only use first 4
    *(int*)(output-4) = stbir__simdi_to_int( i0 );
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    stbir__simdf e0;
    STBIR_NO_UNROLL(encode);
    stbir__simdf_madd1_mem( e0, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint8_as_float), encode+stbir__encode_order0 ); output[0] = stbir__simdf_convert_float_to_uint8( e0 );
    #if stbir__coder_min_num >= 2
    stbir__simdf_madd1_mem( e0, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint8_as_float), encode+stbir__encode_order1 ); output[1] = stbir__simdf_convert_float_to_uint8( e0 );
    #endif
    #if stbir__coder_min_num >= 3
    stbir__simdf_madd1_mem( e0, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint8_as_float), encode+stbir__encode_order2 ); output[2] = stbir__simdf_convert_float_to_uint8( e0 );
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif

  #else

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  while( output <= end_output )
  {
    float f;
    f = encode[stbir__encode_order0] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[0-4] = (unsigned char)f;
    f = encode[stbir__encode_order1] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[1-4] = (unsigned char)f;
    f = encode[stbir__encode_order2] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[2-4] = (unsigned char)f;
    f = encode[stbir__encode_order3] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[3-4] = (unsigned char)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[0] = (unsigned char)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[1] = (unsigned char)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] * stbir__max_uint8_as_float + 0.5f; STBIR_CLAMP(f, 0, 255); output[2] = (unsigned char)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_uint8_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const*)inputp;

  #ifdef STBIR_SIMD
  unsigned char const * end_input_m16 = input + width_times_channels - 16;
  if ( width_times_channels >= 16 )
  {
    decode_end -= 16;
    STBIR_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      #ifdef STBIR_SIMD8
      stbir__simdi i; stbir__simdi8 o0,o1;
      stbir__simdf8 of0, of1;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi8_expand_u8_to_u32( o0, o1, i );
      stbir__simdi8_convert_i32_to_float( of0, o0 );
      stbir__simdi8_convert_i32_to_float( of1, o1 );
      stbir__decode_simdf8_flip( of0 );
      stbir__decode_simdf8_flip( of1 );
      stbir__simdf8_store( decode + 0, of0 );
      stbir__simdf8_store( decode + 8, of1 );
      #else
      stbir__simdi i, o0, o1, o2, o3;
      stbir__simdf of0, of1, of2, of3;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi_expand_u8_to_u32( o0,o1,o2,o3,i);
      stbir__simdi_convert_i32_to_float( of0, o0 );
      stbir__simdi_convert_i32_to_float( of1, o1 );
      stbir__simdi_convert_i32_to_float( of2, o2 );
      stbir__simdi_convert_i32_to_float( of3, o3 );
      stbir__decode_simdf4_flip( of0 );
      stbir__decode_simdf4_flip( of1 );
      stbir__decode_simdf4_flip( of2 );
      stbir__decode_simdf4_flip( of3 );
      stbir__simdf_store( decode + 0,  of0 );
      stbir__simdf_store( decode + 4,  of1 );
      stbir__simdf_store( decode + 8,  of2 );
      stbir__simdf_store( decode + 12, of3 );
#endif
      decode += 16;
      input += 16;
      if ( decode <= decode_end )
        continue;
      if ( decode == ( decode_end + 16 ) )
        break;
      decode = decode_end; // backup and do last couple
      input = end_input_m16;
    }
    return decode_end + 16;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0]));
    decode[1-4] = ((float)(input[stbir__decode_order1]));
    decode[2-4] = ((float)(input[stbir__decode_order2]));
    decode[3-4] = ((float)(input[stbir__decode_order3]));
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0]));
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1]));
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2]));
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_uint8_linear )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char *) outputp;
  unsigned char * end_output = ( (unsigned char *) output ) + width_times_channels;

  #ifdef STBIR_SIMD
  if ( width_times_channels >= stbir__simdfX_float_count*2 )
  {
    float const * end_encode_m8 = encode + width_times_channels - stbir__simdfX_float_count*2;
    end_output -= stbir__simdfX_float_count*2;
    STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      stbir__simdfX e0, e1;
      stbir__simdi i;
      STBIR_SIMD_NO_UNROLL(encode);
      stbir__simdfX_add_mem( e0, STBIR_simd_point5X, encode );
      stbir__simdfX_add_mem( e1, STBIR_simd_point5X, encode+stbir__simdfX_float_count );
      stbir__encode_simdfX_unflip( e0 );
      stbir__encode_simdfX_unflip( e1 );
      #ifdef STBIR_SIMD8
      stbir__simdf8_pack_to_16bytes( i, e0, e1 );
      stbir__simdi_store( output, i );
      #else
      stbir__simdf_pack_to_8bytes( i, e0, e1 );
      stbir__simdi_store2( output, i );
      #endif
      encode += stbir__simdfX_float_count*2;
      output += stbir__simdfX_float_count*2;
      if ( output <= end_output )
        continue;
      if ( output == ( end_output + stbir__simdfX_float_count*2 ) )
        break;
      output = end_output; // backup and do last couple
      encode = end_encode_m8;
    }
    return;
  }

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    stbir__simdf e0;
    stbir__simdi i0;
    STBIR_NO_UNROLL(encode);
    stbir__simdf_load( e0, encode );
    stbir__simdf_add( e0, STBIR__CONSTF(STBIR_simd_point5), e0 );
    stbir__encode_simdf4_unflip( e0 );
    stbir__simdf_pack_to_8bytes( i0, e0, e0 );  // only use first 4
    *(int*)(output-4) = stbir__simdi_to_int( i0 );
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  #else

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  while( output <= end_output )
  {
    float f;
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 255); output[0-4] = (unsigned char)f;
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 255); output[1-4] = (unsigned char)f;
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 255); output[2-4] = (unsigned char)f;
    f = encode[stbir__encode_order3] + 0.5f; STBIR_CLAMP(f, 0, 255); output[3-4] = (unsigned char)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 255); output[0] = (unsigned char)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 255); output[1] = (unsigned char)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 255); output[2] = (unsigned char)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_uint8_srgb)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const *)inputp;

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  while( decode <= decode_end )
  {
    decode[0-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order0 ] ];
    decode[1-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order1 ] ];
    decode[2-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order2 ] ];
    decode[3-4] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order3 ] ];
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order0 ] ];
    #if stbir__coder_min_num >= 2
    decode[1] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order1 ] ];
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = stbir__srgb_uchar_to_linear_float[ input[ stbir__decode_order2 ] ];
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

#define stbir__min_max_shift20( i, f ) \
    stbir__simdf_max( f, f, stbir_simdf_casti(STBIR__CONSTI( STBIR_almost_zero )) ); \
    stbir__simdf_min( f, f, stbir_simdf_casti(STBIR__CONSTI( STBIR_almost_one  )) ); \
    stbir__simdi_32shr( i, stbir_simdi_castf( f ), 20 );

#define stbir__scale_and_convert( i, f ) \
    stbir__simdf_madd( f, STBIR__CONSTF( STBIR_simd_point5 ), STBIR__CONSTF( STBIR_max_uint8_as_float ), f ); \
    stbir__simdf_max( f, f, stbir__simdf_zeroP() ); \
    stbir__simdf_min( f, f, STBIR__CONSTF( STBIR_max_uint8_as_float ) ); \
    stbir__simdf_convert_float_to_i32( i, f );

#define stbir__linear_to_srgb_finish( i, f ) \
{ \
    stbir__simdi temp;  \
    stbir__simdi_32shr( temp, stbir_simdi_castf( f ), 12 ) ; \
    stbir__simdi_and( temp, temp, STBIR__CONSTI(STBIR_mantissa_mask) ); \
    stbir__simdi_or( temp, temp, STBIR__CONSTI(STBIR_topscale) ); \
    stbir__simdi_16madd( i, i, temp ); \
    stbir__simdi_32shr( i, i, 16 ); \
}

#define stbir__simdi_table_lookup2( v0,v1, table ) \
{ \
  stbir__simdi_u32 temp0,temp1; \
  temp0.m128i_i128 = v0; \
  temp1.m128i_i128 = v1; \
  temp0.m128i_u32[0] = table[temp0.m128i_i32[0]]; temp0.m128i_u32[1] = table[temp0.m128i_i32[1]]; temp0.m128i_u32[2] = table[temp0.m128i_i32[2]]; temp0.m128i_u32[3] = table[temp0.m128i_i32[3]]; \
  temp1.m128i_u32[0] = table[temp1.m128i_i32[0]]; temp1.m128i_u32[1] = table[temp1.m128i_i32[1]]; temp1.m128i_u32[2] = table[temp1.m128i_i32[2]]; temp1.m128i_u32[3] = table[temp1.m128i_i32[3]]; \
  v0 = temp0.m128i_i128; \
  v1 = temp1.m128i_i128; \
}

#define stbir__simdi_table_lookup3( v0,v1,v2, table ) \
{ \
  stbir__simdi_u32 temp0,temp1,temp2; \
  temp0.m128i_i128 = v0; \
  temp1.m128i_i128 = v1; \
  temp2.m128i_i128 = v2; \
  temp0.m128i_u32[0] = table[temp0.m128i_i32[0]]; temp0.m128i_u32[1] = table[temp0.m128i_i32[1]]; temp0.m128i_u32[2] = table[temp0.m128i_i32[2]]; temp0.m128i_u32[3] = table[temp0.m128i_i32[3]]; \
  temp1.m128i_u32[0] = table[temp1.m128i_i32[0]]; temp1.m128i_u32[1] = table[temp1.m128i_i32[1]]; temp1.m128i_u32[2] = table[temp1.m128i_i32[2]]; temp1.m128i_u32[3] = table[temp1.m128i_i32[3]]; \
  temp2.m128i_u32[0] = table[temp2.m128i_i32[0]]; temp2.m128i_u32[1] = table[temp2.m128i_i32[1]]; temp2.m128i_u32[2] = table[temp2.m128i_i32[2]]; temp2.m128i_u32[3] = table[temp2.m128i_i32[3]]; \
  v0 = temp0.m128i_i128; \
  v1 = temp1.m128i_i128; \
  v2 = temp2.m128i_i128; \
}

#define stbir__simdi_table_lookup4( v0,v1,v2,v3, table ) \
{ \
  stbir__simdi_u32 temp0,temp1,temp2,temp3; \
  temp0.m128i_i128 = v0; \
  temp1.m128i_i128 = v1; \
  temp2.m128i_i128 = v2; \
  temp3.m128i_i128 = v3; \
  temp0.m128i_u32[0] = table[temp0.m128i_i32[0]]; temp0.m128i_u32[1] = table[temp0.m128i_i32[1]]; temp0.m128i_u32[2] = table[temp0.m128i_i32[2]]; temp0.m128i_u32[3] = table[temp0.m128i_i32[3]]; \
  temp1.m128i_u32[0] = table[temp1.m128i_i32[0]]; temp1.m128i_u32[1] = table[temp1.m128i_i32[1]]; temp1.m128i_u32[2] = table[temp1.m128i_i32[2]]; temp1.m128i_u32[3] = table[temp1.m128i_i32[3]]; \
  temp2.m128i_u32[0] = table[temp2.m128i_i32[0]]; temp2.m128i_u32[1] = table[temp2.m128i_i32[1]]; temp2.m128i_u32[2] = table[temp2.m128i_i32[2]]; temp2.m128i_u32[3] = table[temp2.m128i_i32[3]]; \
  temp3.m128i_u32[0] = table[temp3.m128i_i32[0]]; temp3.m128i_u32[1] = table[temp3.m128i_i32[1]]; temp3.m128i_u32[2] = table[temp3.m128i_i32[2]]; temp3.m128i_u32[3] = table[temp3.m128i_i32[3]]; \
  v0 = temp0.m128i_i128; \
  v1 = temp1.m128i_i128; \
  v2 = temp2.m128i_i128; \
  v3 = temp3.m128i_i128; \
}

static void STBIR__CODER_NAME( stbir__encode_uint8_srgb )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char*) outputp;
  unsigned char * end_output = ( (unsigned char*) output ) + width_times_channels;

  #ifdef STBIR_SIMD

  if ( width_times_channels >= 16 )
  {
    float const * end_encode_m16 = encode + width_times_channels - 16;
    end_output -= 16;
    STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      stbir__simdf f0, f1, f2, f3;
      stbir__simdi i0, i1, i2, i3;
      STBIR_SIMD_NO_UNROLL(encode);

      stbir__simdf_load4_transposed( f0, f1, f2, f3, encode );

      stbir__min_max_shift20( i0, f0 );
      stbir__min_max_shift20( i1, f1 );
      stbir__min_max_shift20( i2, f2 );
      stbir__min_max_shift20( i3, f3 );

      stbir__simdi_table_lookup4( i0, i1, i2, i3, ( fp32_to_srgb8_tab4 - (127-13)*8 ) );

      stbir__linear_to_srgb_finish( i0, f0 );
      stbir__linear_to_srgb_finish( i1, f1 );
      stbir__linear_to_srgb_finish( i2, f2 );
      stbir__linear_to_srgb_finish( i3, f3 );

      stbir__interleave_pack_and_store_16_u8( output,  STBIR_strs_join1(i, ,stbir__encode_order0), STBIR_strs_join1(i, ,stbir__encode_order1), STBIR_strs_join1(i, ,stbir__encode_order2), STBIR_strs_join1(i, ,stbir__encode_order3) );

      encode += 16;
      output += 16;
      if ( output <= end_output )
        continue;
      if ( output == ( end_output + 16 ) )
        break;
      output = end_output; // backup and do last couple
      encode = end_encode_m16;
    }
    return;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while ( output <= end_output )
  {
    STBIR_SIMD_NO_UNROLL(encode);

    output[0-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order0] );
    output[1-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order1] );
    output[2-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order2] );
    output[3-4] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order3] );

    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    STBIR_NO_UNROLL(encode);
    output[0] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order0] );
    #if stbir__coder_min_num >= 2
    output[1] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order1] );
    #endif
    #if stbir__coder_min_num >= 3
    output[2] = stbir__linear_to_srgb_uchar( encode[stbir__encode_order2] );
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

#if ( stbir__coder_min_num == 4 ) || ( ( stbir__coder_min_num == 1 ) && ( !defined(stbir__decode_swizzle) ) )

static float * STBIR__CODER_NAME(stbir__decode_uint8_srgb4_linearalpha)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const *)inputp;

  do {
    decode[0] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order0] ];
    decode[1] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order1] ];
    decode[2] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order2] ];
    decode[3] = ( (float) input[stbir__decode_order3] ) * stbir__max_uint8_as_float_inverted;
    input += 4;
    decode += 4;
  } while( decode < decode_end );
  return decode_end;
}


static void STBIR__CODER_NAME( stbir__encode_uint8_srgb4_linearalpha )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char*) outputp;
  unsigned char * end_output = ( (unsigned char*) output ) + width_times_channels;

  #ifdef STBIR_SIMD

  if ( width_times_channels >= 16 )
  {
    float const * end_encode_m16 = encode + width_times_channels - 16;
    end_output -= 16;
    STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      stbir__simdf f0, f1, f2, f3;
      stbir__simdi i0, i1, i2, i3;

      STBIR_SIMD_NO_UNROLL(encode);
      stbir__simdf_load4_transposed( f0, f1, f2, f3, encode );

      stbir__min_max_shift20( i0, f0 );
      stbir__min_max_shift20( i1, f1 );
      stbir__min_max_shift20( i2, f2 );
      stbir__scale_and_convert( i3, f3 );

      stbir__simdi_table_lookup3( i0, i1, i2, ( fp32_to_srgb8_tab4 - (127-13)*8 ) );

      stbir__linear_to_srgb_finish( i0, f0 );
      stbir__linear_to_srgb_finish( i1, f1 );
      stbir__linear_to_srgb_finish( i2, f2 );

      stbir__interleave_pack_and_store_16_u8( output,  STBIR_strs_join1(i, ,stbir__encode_order0), STBIR_strs_join1(i, ,stbir__encode_order1), STBIR_strs_join1(i, ,stbir__encode_order2), STBIR_strs_join1(i, ,stbir__encode_order3) );

      output += 16;
      encode += 16;

      if ( output <= end_output )
        continue;
      if ( output == ( end_output + 16 ) )
        break;
      output = end_output; // backup and do last couple
      encode = end_encode_m16;
    }
    return;
  }
  #endif

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);

    output[stbir__decode_order0] = stbir__linear_to_srgb_uchar( encode[0] );
    output[stbir__decode_order1] = stbir__linear_to_srgb_uchar( encode[1] );
    output[stbir__decode_order2] = stbir__linear_to_srgb_uchar( encode[2] );

    f = encode[3] * stbir__max_uint8_as_float + 0.5f;
    STBIR_CLAMP(f, 0, 255);
    output[stbir__decode_order3] = (unsigned char) f;

    output += 4;
    encode += 4;
  } while( output < end_output );
}

#endif

#if ( stbir__coder_min_num == 2 ) || ( ( stbir__coder_min_num == 1 ) && ( !defined(stbir__decode_swizzle) ) )

static float * STBIR__CODER_NAME(stbir__decode_uint8_srgb2_linearalpha)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned char const * input = (unsigned char const *)inputp;

  decode += 4;
  while( decode <= decode_end )
  {
    decode[0-4] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order0] ];
    decode[1-4] = ( (float) input[stbir__decode_order1] ) * stbir__max_uint8_as_float_inverted;
    decode[2-4] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order0+2] ];
    decode[3-4] = ( (float) input[stbir__decode_order1+2] ) * stbir__max_uint8_as_float_inverted;
    input += 4;
    decode += 4;
  }
  decode -= 4;
  if( decode < decode_end )
  {
    decode[0] = stbir__srgb_uchar_to_linear_float[ input[stbir__decode_order0] ];
    decode[1] = ( (float) input[stbir__decode_order1] ) * stbir__max_uint8_as_float_inverted;
  }
  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_uint8_srgb2_linearalpha )( void * outputp, int width_times_channels, float const * encode )
{
  unsigned char STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned char*) outputp;
  unsigned char * end_output = ( (unsigned char*) output ) + width_times_channels;

  #ifdef STBIR_SIMD

  if ( width_times_channels >= 16 )
  {
    float const * end_encode_m16 = encode + width_times_channels - 16;
    end_output -= 16;
    STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      stbir__simdf f0, f1, f2, f3;
      stbir__simdi i0, i1, i2, i3;

      STBIR_SIMD_NO_UNROLL(encode);
      stbir__simdf_load4_transposed( f0, f1, f2, f3, encode );

      stbir__min_max_shift20( i0, f0 );
      stbir__scale_and_convert( i1, f1 );
      stbir__min_max_shift20( i2, f2 );
      stbir__scale_and_convert( i3, f3 );

      stbir__simdi_table_lookup2( i0, i2, ( fp32_to_srgb8_tab4 - (127-13)*8 ) );

      stbir__linear_to_srgb_finish( i0, f0 );
      stbir__linear_to_srgb_finish( i2, f2 );

      stbir__interleave_pack_and_store_16_u8( output,  STBIR_strs_join1(i, ,stbir__encode_order0), STBIR_strs_join1(i, ,stbir__encode_order1), STBIR_strs_join1(i, ,stbir__encode_order2), STBIR_strs_join1(i, ,stbir__encode_order3) );

      output += 16;
      encode += 16;
      if ( output <= end_output )
        continue;
      if ( output == ( end_output + 16 ) )
        break;
      output = end_output; // backup and do last couple
      encode = end_encode_m16;
    }
    return;
  }
  #endif

  STBIR_SIMD_NO_UNROLL_LOOP_START
  do {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);

    output[stbir__decode_order0] = stbir__linear_to_srgb_uchar( encode[0] );

    f = encode[1] * stbir__max_uint8_as_float + 0.5f;
    STBIR_CLAMP(f, 0, 255);
    output[stbir__decode_order1] = (unsigned char) f;

    output += 2;
    encode += 2;
  } while( output < end_output );
}

#endif

static float * STBIR__CODER_NAME(stbir__decode_uint16_linear_scaled)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned short const * input = (unsigned short const *)inputp;

  #ifdef STBIR_SIMD
  unsigned short const * end_input_m8 = input + width_times_channels - 8;
  if ( width_times_channels >= 8 )
  {
    decode_end -= 8;
    STBIR_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      #ifdef STBIR_SIMD8
      stbir__simdi i; stbir__simdi8 o;
      stbir__simdf8 of;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi8_expand_u16_to_u32( o, i );
      stbir__simdi8_convert_i32_to_float( of, o );
      stbir__simdf8_mult( of, of, STBIR_max_uint16_as_float_inverted8);
      stbir__decode_simdf8_flip( of );
      stbir__simdf8_store( decode + 0, of );
      #else
      stbir__simdi i, o0, o1;
      stbir__simdf of0, of1;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi_expand_u16_to_u32( o0,o1,i );
      stbir__simdi_convert_i32_to_float( of0, o0 );
      stbir__simdi_convert_i32_to_float( of1, o1 );
      stbir__simdf_mult( of0, of0, STBIR__CONSTF(STBIR_max_uint16_as_float_inverted) );
      stbir__simdf_mult( of1, of1, STBIR__CONSTF(STBIR_max_uint16_as_float_inverted));
      stbir__decode_simdf4_flip( of0 );
      stbir__decode_simdf4_flip( of1 );
      stbir__simdf_store( decode + 0,  of0 );
      stbir__simdf_store( decode + 4,  of1 );
      #endif
      decode += 8;
      input += 8;
      if ( decode <= decode_end )
        continue;
      if ( decode == ( decode_end + 8 ) )
        break;
      decode = decode_end; // backup and do last couple
      input = end_input_m8;
    }
    return decode_end + 8;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0])) * stbir__max_uint16_as_float_inverted;
    decode[1-4] = ((float)(input[stbir__decode_order1])) * stbir__max_uint16_as_float_inverted;
    decode[2-4] = ((float)(input[stbir__decode_order2])) * stbir__max_uint16_as_float_inverted;
    decode[3-4] = ((float)(input[stbir__decode_order3])) * stbir__max_uint16_as_float_inverted;
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0])) * stbir__max_uint16_as_float_inverted;
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1])) * stbir__max_uint16_as_float_inverted;
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2])) * stbir__max_uint16_as_float_inverted;
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}


static void STBIR__CODER_NAME(stbir__encode_uint16_linear_scaled)( void * outputp, int width_times_channels, float const * encode )
{
  unsigned short STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned short*) outputp;
  unsigned short * end_output = ( (unsigned short*) output ) + width_times_channels;

  #ifdef STBIR_SIMD
  {
    if ( width_times_channels >= stbir__simdfX_float_count*2 )
    {
      float const * end_encode_m8 = encode + width_times_channels - stbir__simdfX_float_count*2;
      end_output -= stbir__simdfX_float_count*2;
      STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
      for(;;)
      {
        stbir__simdfX e0, e1;
        stbir__simdiX i;
        STBIR_SIMD_NO_UNROLL(encode);
        stbir__simdfX_madd_mem( e0, STBIR_simd_point5X, STBIR_max_uint16_as_floatX, encode );
        stbir__simdfX_madd_mem( e1, STBIR_simd_point5X, STBIR_max_uint16_as_floatX, encode+stbir__simdfX_float_count );
        stbir__encode_simdfX_unflip( e0 );
        stbir__encode_simdfX_unflip( e1 );
        stbir__simdfX_pack_to_words( i, e0, e1 );
        stbir__simdiX_store( output, i );
        encode += stbir__simdfX_float_count*2;
        output += stbir__simdfX_float_count*2;
        if ( output <= end_output )
          continue;
        if ( output == ( end_output + stbir__simdfX_float_count*2 ) )
          break;
        output = end_output;     // backup and do last couple
        encode = end_encode_m8;
      }
      return;
    }
  }

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    stbir__simdf e;
    stbir__simdi i;
    STBIR_NO_UNROLL(encode);
    stbir__simdf_load( e, encode );
    stbir__simdf_madd( e, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint16_as_float), e );
    stbir__encode_simdf4_unflip( e );
    stbir__simdf_pack_to_8words( i, e, e );  // only use first 4
    stbir__simdi_store2( output-4, i );
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    stbir__simdf e;
    STBIR_NO_UNROLL(encode);
    stbir__simdf_madd1_mem( e, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint16_as_float), encode+stbir__encode_order0 ); output[0] = stbir__simdf_convert_float_to_short( e );
    #if stbir__coder_min_num >= 2
    stbir__simdf_madd1_mem( e, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint16_as_float), encode+stbir__encode_order1 ); output[1] = stbir__simdf_convert_float_to_short( e );
    #endif
    #if stbir__coder_min_num >= 3
    stbir__simdf_madd1_mem( e, STBIR__CONSTF(STBIR_simd_point5), STBIR__CONSTF(STBIR_max_uint16_as_float), encode+stbir__encode_order2 ); output[2] = stbir__simdf_convert_float_to_short( e );
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif

  #else

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0-4] = (unsigned short)f;
    f = encode[stbir__encode_order1] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1-4] = (unsigned short)f;
    f = encode[stbir__encode_order2] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2-4] = (unsigned short)f;
    f = encode[stbir__encode_order3] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[3-4] = (unsigned short)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0] = (unsigned short)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1] = (unsigned short)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] * stbir__max_uint16_as_float + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2] = (unsigned short)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_uint16_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  unsigned short const * input = (unsigned short const *)inputp;

  #ifdef STBIR_SIMD
  unsigned short const * end_input_m8 = input + width_times_channels - 8;
  if ( width_times_channels >= 8 )
  {
    decode_end -= 8;
    STBIR_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      #ifdef STBIR_SIMD8
      stbir__simdi i; stbir__simdi8 o;
      stbir__simdf8 of;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi8_expand_u16_to_u32( o, i );
      stbir__simdi8_convert_i32_to_float( of, o );
      stbir__decode_simdf8_flip( of );
      stbir__simdf8_store( decode + 0, of );
      #else
      stbir__simdi i, o0, o1;
      stbir__simdf of0, of1;
      STBIR_NO_UNROLL(decode);
      stbir__simdi_load( i, input );
      stbir__simdi_expand_u16_to_u32( o0, o1, i );
      stbir__simdi_convert_i32_to_float( of0, o0 );
      stbir__simdi_convert_i32_to_float( of1, o1 );
      stbir__decode_simdf4_flip( of0 );
      stbir__decode_simdf4_flip( of1 );
      stbir__simdf_store( decode + 0,  of0 );
      stbir__simdf_store( decode + 4,  of1 );
      #endif
      decode += 8;
      input += 8;
      if ( decode <= decode_end )
        continue;
      if ( decode == ( decode_end + 8 ) )
        break;
      decode = decode_end; // backup and do last couple
      input = end_input_m8;
    }
    return decode_end + 8;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = ((float)(input[stbir__decode_order0]));
    decode[1-4] = ((float)(input[stbir__decode_order1]));
    decode[2-4] = ((float)(input[stbir__decode_order2]));
    decode[3-4] = ((float)(input[stbir__decode_order3]));
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = ((float)(input[stbir__decode_order0]));
    #if stbir__coder_min_num >= 2
    decode[1] = ((float)(input[stbir__decode_order1]));
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = ((float)(input[stbir__decode_order2]));
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

static void STBIR__CODER_NAME(stbir__encode_uint16_linear)( void * outputp, int width_times_channels, float const * encode )
{
  unsigned short STBIR_SIMD_STREAMOUT_PTR( * ) output = (unsigned short*) outputp;
  unsigned short * end_output = ( (unsigned short*) output ) + width_times_channels;

  #ifdef STBIR_SIMD
  {
    if ( width_times_channels >= stbir__simdfX_float_count*2 )
    {
      float const * end_encode_m8 = encode + width_times_channels - stbir__simdfX_float_count*2;
      end_output -= stbir__simdfX_float_count*2;
      STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
      for(;;)
      {
        stbir__simdfX e0, e1;
        stbir__simdiX i;
        STBIR_SIMD_NO_UNROLL(encode);
        stbir__simdfX_add_mem( e0, STBIR_simd_point5X, encode );
        stbir__simdfX_add_mem( e1, STBIR_simd_point5X, encode+stbir__simdfX_float_count );
        stbir__encode_simdfX_unflip( e0 );
        stbir__encode_simdfX_unflip( e1 );
        stbir__simdfX_pack_to_words( i, e0, e1 );
        stbir__simdiX_store( output, i );
        encode += stbir__simdfX_float_count*2;
        output += stbir__simdfX_float_count*2;
        if ( output <= end_output )
          continue;
        if ( output == ( end_output + stbir__simdfX_float_count*2 ) )
          break;
        output = end_output; // backup and do last couple
        encode = end_encode_m8;
      }
      return;
    }
  }

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    stbir__simdf e;
    stbir__simdi i;
    STBIR_NO_UNROLL(encode);
    stbir__simdf_load( e, encode );
    stbir__simdf_add( e, STBIR__CONSTF(STBIR_simd_point5), e );
    stbir__encode_simdf4_unflip( e );
    stbir__simdf_pack_to_8words( i, e, e );  // only use first 4
    stbir__simdi_store2( output-4, i );
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  #else

  // try to do blocks of 4 when you can
  #if  stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    float f;
    STBIR_SIMD_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0-4] = (unsigned short)f;
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1-4] = (unsigned short)f;
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2-4] = (unsigned short)f;
    f = encode[stbir__encode_order3] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[3-4] = (unsigned short)f;
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float f;
    STBIR_NO_UNROLL(encode);
    f = encode[stbir__encode_order0] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[0] = (unsigned short)f;
    #if stbir__coder_min_num >= 2
    f = encode[stbir__encode_order1] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[1] = (unsigned short)f;
    #endif
    #if stbir__coder_min_num >= 3
    f = encode[stbir__encode_order2] + 0.5f; STBIR_CLAMP(f, 0, 65535); output[2] = (unsigned short)f;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_half_float_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  stbir__FP16 const * input = (stbir__FP16 const *)inputp;

  #ifdef STBIR_SIMD
  if ( width_times_channels >= 8 )
  {
    stbir__FP16 const * end_input_m8 = input + width_times_channels - 8;
    decode_end -= 8;
    STBIR_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      STBIR_NO_UNROLL(decode);

      stbir__half_to_float_SIMD( decode, input );
      #ifdef stbir__decode_swizzle
      #ifdef STBIR_SIMD8
      {
        stbir__simdf8 of;
        stbir__simdf8_load( of, decode );
        stbir__decode_simdf8_flip( of );
        stbir__simdf8_store( decode, of );
      }
      #else
      {
        stbir__simdf of0,of1;
        stbir__simdf_load( of0, decode );
        stbir__simdf_load( of1, decode+4 );
        stbir__decode_simdf4_flip( of0 );
        stbir__decode_simdf4_flip( of1 );
        stbir__simdf_store( decode, of0 );
        stbir__simdf_store( decode+4, of1 );
      }
      #endif
      #endif
      decode += 8;
      input += 8;
      if ( decode <= decode_end )
        continue;
      if ( decode == ( decode_end + 8 ) )
        break;
      decode = decode_end; // backup and do last couple
      input = end_input_m8;
    }
    return decode_end + 8;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = stbir__half_to_float(input[stbir__decode_order0]);
    decode[1-4] = stbir__half_to_float(input[stbir__decode_order1]);
    decode[2-4] = stbir__half_to_float(input[stbir__decode_order2]);
    decode[3-4] = stbir__half_to_float(input[stbir__decode_order3]);
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = stbir__half_to_float(input[stbir__decode_order0]);
    #if stbir__coder_min_num >= 2
    decode[1] = stbir__half_to_float(input[stbir__decode_order1]);
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = stbir__half_to_float(input[stbir__decode_order2]);
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;
}

static void STBIR__CODER_NAME( stbir__encode_half_float_linear )( void * outputp, int width_times_channels, float const * encode )
{
  stbir__FP16 STBIR_SIMD_STREAMOUT_PTR( * ) output = (stbir__FP16*) outputp;
  stbir__FP16 * end_output = ( (stbir__FP16*) output ) + width_times_channels;

  #ifdef STBIR_SIMD
  if ( width_times_channels >= 8 )
  {
    float const * end_encode_m8 = encode + width_times_channels - 8;
    end_output -= 8;
    STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      STBIR_SIMD_NO_UNROLL(encode);
      #ifdef stbir__decode_swizzle
      #ifdef STBIR_SIMD8
      {
        stbir__simdf8 of;
        stbir__simdf8_load( of, encode );
        stbir__encode_simdf8_unflip( of );
        stbir__float_to_half_SIMD( output, (float*)&of );
      }
      #else
      {
        stbir__simdf of[2];
        stbir__simdf_load( of[0], encode );
        stbir__simdf_load( of[1], encode+4 );
        stbir__encode_simdf4_unflip( of[0] );
        stbir__encode_simdf4_unflip( of[1] );
        stbir__float_to_half_SIMD( output, (float*)of );
      }
      #endif
      #else
      stbir__float_to_half_SIMD( output, encode );
      #endif
      encode += 8;
      output += 8;
      if ( output <= end_output )
        continue;
      if ( output == ( end_output + 8 ) )
        break;
      output = end_output; // backup and do last couple
      encode = end_encode_m8;
    }
    return;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    STBIR_SIMD_NO_UNROLL(output);
    output[0-4] = stbir__float_to_half(encode[stbir__encode_order0]);
    output[1-4] = stbir__float_to_half(encode[stbir__encode_order1]);
    output[2-4] = stbir__float_to_half(encode[stbir__encode_order2]);
    output[3-4] = stbir__float_to_half(encode[stbir__encode_order3]);
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    STBIR_NO_UNROLL(output);
    output[0] = stbir__float_to_half(encode[stbir__encode_order0]);
    #if stbir__coder_min_num >= 2
    output[1] = stbir__float_to_half(encode[stbir__encode_order1]);
    #endif
    #if stbir__coder_min_num >= 3
    output[2] = stbir__float_to_half(encode[stbir__encode_order2]);
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif
}

static float * STBIR__CODER_NAME(stbir__decode_float_linear)( float * decodep, int width_times_channels, void const * inputp )
{
  #ifdef stbir__decode_swizzle
  float STBIR_STREAMOUT_PTR( * ) decode = decodep;
  float * decode_end = (float*) decode + width_times_channels;
  float const * input = (float const *)inputp;

  #ifdef STBIR_SIMD
  if ( width_times_channels >= 16 )
  {
    float const * end_input_m16 = input + width_times_channels - 16;
    decode_end -= 16;
    STBIR_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      STBIR_NO_UNROLL(decode);
      #ifdef stbir__decode_swizzle
      #ifdef STBIR_SIMD8
      {
        stbir__simdf8 of0,of1;
        stbir__simdf8_load( of0, input );
        stbir__simdf8_load( of1, input+8 );
        stbir__decode_simdf8_flip( of0 );
        stbir__decode_simdf8_flip( of1 );
        stbir__simdf8_store( decode, of0 );
        stbir__simdf8_store( decode+8, of1 );
      }
      #else
      {
        stbir__simdf of0,of1,of2,of3;
        stbir__simdf_load( of0, input );
        stbir__simdf_load( of1, input+4 );
        stbir__simdf_load( of2, input+8 );
        stbir__simdf_load( of3, input+12 );
        stbir__decode_simdf4_flip( of0 );
        stbir__decode_simdf4_flip( of1 );
        stbir__decode_simdf4_flip( of2 );
        stbir__decode_simdf4_flip( of3 );
        stbir__simdf_store( decode, of0 );
        stbir__simdf_store( decode+4, of1 );
        stbir__simdf_store( decode+8, of2 );
        stbir__simdf_store( decode+12, of3 );
      }
      #endif
      #endif
      decode += 16;
      input += 16;
      if ( decode <= decode_end )
        continue;
      if ( decode == ( decode_end + 16 ) )
        break;
      decode = decode_end; // backup and do last couple
      input = end_input_m16;
    }
    return decode_end + 16;
  }
  #endif

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  decode += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( decode <= decode_end )
  {
    STBIR_SIMD_NO_UNROLL(decode);
    decode[0-4] = input[stbir__decode_order0];
    decode[1-4] = input[stbir__decode_order1];
    decode[2-4] = input[stbir__decode_order2];
    decode[3-4] = input[stbir__decode_order3];
    decode += 4;
    input += 4;
  }
  decode -= 4;
  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( decode < decode_end )
  {
    STBIR_NO_UNROLL(decode);
    decode[0] = input[stbir__decode_order0];
    #if stbir__coder_min_num >= 2
    decode[1] = input[stbir__decode_order1];
    #endif
    #if stbir__coder_min_num >= 3
    decode[2] = input[stbir__decode_order2];
    #endif
    decode += stbir__coder_min_num;
    input += stbir__coder_min_num;
  }
  #endif
  return decode_end;

  #else

  if ( (void*)decodep != inputp )
    STBIR_MEMCPY( decodep, inputp, width_times_channels * sizeof( float ) );

  return decodep + width_times_channels;

  #endif
}

static void STBIR__CODER_NAME( stbir__encode_float_linear )( void * outputp, int width_times_channels, float const * encode )
{
  #if !defined( STBIR_FLOAT_HIGH_CLAMP ) && !defined(STBIR_FLOAT_LOW_CLAMP) && !defined(stbir__decode_swizzle)

  if ( (void*)outputp != (void*) encode )
    STBIR_MEMCPY( outputp, encode, width_times_channels * sizeof( float ) );

  #else

  float STBIR_SIMD_STREAMOUT_PTR( * ) output = (float*) outputp;
  float * end_output = ( (float*) output ) + width_times_channels;

  #ifdef STBIR_FLOAT_HIGH_CLAMP
  #define stbir_scalar_hi_clamp( v ) if ( v > STBIR_FLOAT_HIGH_CLAMP ) v = STBIR_FLOAT_HIGH_CLAMP;
  #else
  #define stbir_scalar_hi_clamp( v )
  #endif
  #ifdef STBIR_FLOAT_LOW_CLAMP
  #define stbir_scalar_lo_clamp( v ) if ( v < STBIR_FLOAT_LOW_CLAMP ) v = STBIR_FLOAT_LOW_CLAMP;
  #else
  #define stbir_scalar_lo_clamp( v )
  #endif

  #ifdef STBIR_SIMD

  #ifdef STBIR_FLOAT_HIGH_CLAMP
  const stbir__simdfX high_clamp = stbir__simdf_frepX(STBIR_FLOAT_HIGH_CLAMP);
  #endif
  #ifdef STBIR_FLOAT_LOW_CLAMP
  const stbir__simdfX low_clamp = stbir__simdf_frepX(STBIR_FLOAT_LOW_CLAMP);
  #endif

  if ( width_times_channels >= ( stbir__simdfX_float_count * 2 ) )
  {
    float const * end_encode_m8 = encode + width_times_channels - ( stbir__simdfX_float_count * 2 );
    end_output -= ( stbir__simdfX_float_count * 2 );
    STBIR_SIMD_NO_UNROLL_LOOP_START_INF_FOR
    for(;;)
    {
      stbir__simdfX e0, e1;
      STBIR_SIMD_NO_UNROLL(encode);
      stbir__simdfX_load( e0, encode );
      stbir__simdfX_load( e1, encode+stbir__simdfX_float_count );
#ifdef STBIR_FLOAT_HIGH_CLAMP
      stbir__simdfX_min( e0, e0, high_clamp );
      stbir__simdfX_min( e1, e1, high_clamp );
#endif
#ifdef STBIR_FLOAT_LOW_CLAMP
      stbir__simdfX_max( e0, e0, low_clamp );
      stbir__simdfX_max( e1, e1, low_clamp );
#endif
      stbir__encode_simdfX_unflip( e0 );
      stbir__encode_simdfX_unflip( e1 );
      stbir__simdfX_store( output, e0 );
      stbir__simdfX_store( output+stbir__simdfX_float_count, e1 );
      encode += stbir__simdfX_float_count * 2;
      output += stbir__simdfX_float_count * 2;
      if ( output <= end_output )
        continue;
      if ( output == ( end_output + ( stbir__simdfX_float_count * 2 ) ) )
        break;
      output = end_output; // backup and do last couple
      encode = end_encode_m8;
    }
    return;
  }

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    stbir__simdf e0;
    STBIR_NO_UNROLL(encode);
    stbir__simdf_load( e0, encode );
#ifdef STBIR_FLOAT_HIGH_CLAMP
    stbir__simdf_min( e0, e0, high_clamp );
#endif
#ifdef STBIR_FLOAT_LOW_CLAMP
    stbir__simdf_max( e0, e0, low_clamp );
#endif
    stbir__encode_simdf4_unflip( e0 );
    stbir__simdf_store( output-4, e0 );
    output += 4;
    encode += 4;
  }
  output -= 4;
  #endif

  #else

  // try to do blocks of 4 when you can
  #if stbir__coder_min_num != 3 // doesn't divide cleanly by four
  output += 4;
  STBIR_SIMD_NO_UNROLL_LOOP_START
  while( output <= end_output )
  {
    float e;
    STBIR_SIMD_NO_UNROLL(encode);
    e = encode[ stbir__encode_order0 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[0-4] = e;
    e = encode[ stbir__encode_order1 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[1-4] = e;
    e = encode[ stbir__encode_order2 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[2-4] = e;
    e = encode[ stbir__encode_order3 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[3-4] = e;
    output += 4;
    encode += 4;
  }
  output -= 4;

  #endif

  #endif

  // do the remnants
  #if stbir__coder_min_num < 4
  STBIR_NO_UNROLL_LOOP_START
  while( output < end_output )
  {
    float e;
    STBIR_NO_UNROLL(encode);
    e = encode[ stbir__encode_order0 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[0] = e;
    #if stbir__coder_min_num >= 2
    e = encode[ stbir__encode_order1 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[1] = e;
    #endif
    #if stbir__coder_min_num >= 3
    e = encode[ stbir__encode_order2 ]; stbir_scalar_hi_clamp( e ); stbir_scalar_lo_clamp( e ); output[2] = e;
    #endif
    output += stbir__coder_min_num;
    encode += stbir__coder_min_num;
  }
  #endif

  #endif
}

#undef stbir__decode_suffix
#undef stbir__decode_simdf8_flip
#undef stbir__decode_simdf4_flip
#undef stbir__decode_order0
#undef stbir__decode_order1
#undef stbir__decode_order2
#undef stbir__decode_order3
#undef stbir__encode_order0
#undef stbir__encode_order1
#undef stbir__encode_order2
#undef stbir__encode_order3
#undef stbir__encode_simdf8_unflip
#undef stbir__encode_simdf4_unflip
#undef stbir__encode_simdfX_unflip
#undef STBIR__CODER_NAME
#undef stbir__coder_min_num
#undef stbir__decode_swizzle
#undef stbir_scalar_hi_clamp
#undef stbir_scalar_lo_clamp
#undef STB_IMAGE_RESIZE_DO_CODERS
