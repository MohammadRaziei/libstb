// Template: the vertical resamplers (gather and scatter) for STBIR__vertical_channels coefficients.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

#ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#define STBIR_chans( start, end ) STBIR_strs_join14(start,STBIR__vertical_channels,end,_cont)
#else
#define STBIR_chans( start, end ) STBIR_strs_join1(start,STBIR__vertical_channels,end)
#endif

#if STBIR__vertical_channels >= 1
#define stbIF0( code ) code
#else
#define stbIF0( code )
#endif
#if STBIR__vertical_channels >= 2
#define stbIF1( code ) code
#else
#define stbIF1( code )
#endif
#if STBIR__vertical_channels >= 3
#define stbIF2( code ) code
#else
#define stbIF2( code )
#endif
#if STBIR__vertical_channels >= 4
#define stbIF3( code ) code
#else
#define stbIF3( code )
#endif
#if STBIR__vertical_channels >= 5
#define stbIF4( code ) code
#else
#define stbIF4( code )
#endif
#if STBIR__vertical_channels >= 6
#define stbIF5( code ) code
#else
#define stbIF5( code )
#endif
#if STBIR__vertical_channels >= 7
#define stbIF6( code ) code
#else
#define stbIF6( code )
#endif
#if STBIR__vertical_channels >= 8
#define stbIF7( code ) code
#else
#define stbIF7( code )
#endif

static void STBIR_chans( stbir__vertical_scatter_with_,_coeffs)( float ** outputs, float const * vertical_coefficients, float const * input, float const * input_end )
{
  stbIF0( float STBIR_SIMD_STREAMOUT_PTR( * ) output0 = outputs[0]; float c0s = vertical_coefficients[0]; )
  stbIF1( float STBIR_SIMD_STREAMOUT_PTR( * ) output1 = outputs[1]; float c1s = vertical_coefficients[1]; )
  stbIF2( float STBIR_SIMD_STREAMOUT_PTR( * ) output2 = outputs[2]; float c2s = vertical_coefficients[2]; )
  stbIF3( float STBIR_SIMD_STREAMOUT_PTR( * ) output3 = outputs[3]; float c3s = vertical_coefficients[3]; )
  stbIF4( float STBIR_SIMD_STREAMOUT_PTR( * ) output4 = outputs[4]; float c4s = vertical_coefficients[4]; )
  stbIF5( float STBIR_SIMD_STREAMOUT_PTR( * ) output5 = outputs[5]; float c5s = vertical_coefficients[5]; )
  stbIF6( float STBIR_SIMD_STREAMOUT_PTR( * ) output6 = outputs[6]; float c6s = vertical_coefficients[6]; )
  stbIF7( float STBIR_SIMD_STREAMOUT_PTR( * ) output7 = outputs[7]; float c7s = vertical_coefficients[7]; )

  #ifdef STBIR_SIMD
  {
    stbIF0(stbir__simdfX c0 = stbir__simdf_frepX( c0s ); )
    stbIF1(stbir__simdfX c1 = stbir__simdf_frepX( c1s ); )
    stbIF2(stbir__simdfX c2 = stbir__simdf_frepX( c2s ); )
    stbIF3(stbir__simdfX c3 = stbir__simdf_frepX( c3s ); )
    stbIF4(stbir__simdfX c4 = stbir__simdf_frepX( c4s ); )
    stbIF5(stbir__simdfX c5 = stbir__simdf_frepX( c5s ); )
    stbIF6(stbir__simdfX c6 = stbir__simdf_frepX( c6s ); )
    stbIF7(stbir__simdfX c7 = stbir__simdf_frepX( c7s ); )
    STBIR_SIMD_NO_UNROLL_LOOP_START
    while ( ( (char*)input_end - (char*) input ) >= (16*stbir__simdfX_float_count) )
    {
      stbir__simdfX o0, o1, o2, o3, r0, r1, r2, r3;
      STBIR_SIMD_NO_UNROLL(output0);

      stbir__simdfX_load( r0, input );               stbir__simdfX_load( r1, input+stbir__simdfX_float_count );     stbir__simdfX_load( r2, input+(2*stbir__simdfX_float_count) );      stbir__simdfX_load( r3, input+(3*stbir__simdfX_float_count) );

      #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
      stbIF0( stbir__simdfX_load( o0, output0 );     stbir__simdfX_load( o1, output0+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output0+(2*stbir__simdfX_float_count) );    stbir__simdfX_load( o3, output0+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c0 );  stbir__simdfX_madd( o1, o1, r1, c0 );  stbir__simdfX_madd( o2, o2, r2, c0 );   stbir__simdfX_madd( o3, o3, r3, c0 );
              stbir__simdfX_store( output0, o0 );    stbir__simdfX_store( output0+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output0+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output0+(3*stbir__simdfX_float_count), o3 ); )
      stbIF1( stbir__simdfX_load( o0, output1 );     stbir__simdfX_load( o1, output1+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output1+(2*stbir__simdfX_float_count) );    stbir__simdfX_load( o3, output1+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c1 );  stbir__simdfX_madd( o1, o1, r1, c1 );  stbir__simdfX_madd( o2, o2, r2, c1 );   stbir__simdfX_madd( o3, o3, r3, c1 );
              stbir__simdfX_store( output1, o0 );    stbir__simdfX_store( output1+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output1+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output1+(3*stbir__simdfX_float_count), o3 ); )
      stbIF2( stbir__simdfX_load( o0, output2 );     stbir__simdfX_load( o1, output2+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output2+(2*stbir__simdfX_float_count) );    stbir__simdfX_load( o3, output2+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c2 );  stbir__simdfX_madd( o1, o1, r1, c2 );  stbir__simdfX_madd( o2, o2, r2, c2 );   stbir__simdfX_madd( o3, o3, r3, c2 );
              stbir__simdfX_store( output2, o0 );    stbir__simdfX_store( output2+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output2+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output2+(3*stbir__simdfX_float_count), o3 ); )
      stbIF3( stbir__simdfX_load( o0, output3 );     stbir__simdfX_load( o1, output3+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output3+(2*stbir__simdfX_float_count) );    stbir__simdfX_load( o3, output3+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c3 );  stbir__simdfX_madd( o1, o1, r1, c3 );  stbir__simdfX_madd( o2, o2, r2, c3 );   stbir__simdfX_madd( o3, o3, r3, c3 );
              stbir__simdfX_store( output3, o0 );    stbir__simdfX_store( output3+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output3+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output3+(3*stbir__simdfX_float_count), o3 ); )
      stbIF4( stbir__simdfX_load( o0, output4 );     stbir__simdfX_load( o1, output4+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output4+(2*stbir__simdfX_float_count) );    stbir__simdfX_load( o3, output4+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c4 );  stbir__simdfX_madd( o1, o1, r1, c4 );  stbir__simdfX_madd( o2, o2, r2, c4 );   stbir__simdfX_madd( o3, o3, r3, c4 );
              stbir__simdfX_store( output4, o0 );    stbir__simdfX_store( output4+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output4+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output4+(3*stbir__simdfX_float_count), o3 ); )
      stbIF5( stbir__simdfX_load( o0, output5 );     stbir__simdfX_load( o1, output5+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output5+(2*stbir__simdfX_float_count));    stbir__simdfX_load( o3, output5+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c5 );  stbir__simdfX_madd( o1, o1, r1, c5 );  stbir__simdfX_madd( o2, o2, r2, c5 );   stbir__simdfX_madd( o3, o3, r3, c5 );
              stbir__simdfX_store( output5, o0 );    stbir__simdfX_store( output5+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output5+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output5+(3*stbir__simdfX_float_count), o3 ); )
      stbIF6( stbir__simdfX_load( o0, output6 );     stbir__simdfX_load( o1, output6+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output6+(2*stbir__simdfX_float_count) );    stbir__simdfX_load( o3, output6+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c6 );  stbir__simdfX_madd( o1, o1, r1, c6 );  stbir__simdfX_madd( o2, o2, r2, c6 );   stbir__simdfX_madd( o3, o3, r3, c6 );
              stbir__simdfX_store( output6, o0 );    stbir__simdfX_store( output6+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output6+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output6+(3*stbir__simdfX_float_count), o3 ); )
      stbIF7( stbir__simdfX_load( o0, output7 );     stbir__simdfX_load( o1, output7+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output7+(2*stbir__simdfX_float_count) );    stbir__simdfX_load( o3, output7+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c7 );  stbir__simdfX_madd( o1, o1, r1, c7 );  stbir__simdfX_madd( o2, o2, r2, c7 );   stbir__simdfX_madd( o3, o3, r3, c7 );
              stbir__simdfX_store( output7, o0 );    stbir__simdfX_store( output7+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output7+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output7+(3*stbir__simdfX_float_count), o3 ); )
      #else
      stbIF0( stbir__simdfX_mult( o0, r0, c0 );      stbir__simdfX_mult( o1, r1, c0 );      stbir__simdfX_mult( o2, r2, c0 );       stbir__simdfX_mult( o3, r3, c0 );
              stbir__simdfX_store( output0, o0 );    stbir__simdfX_store( output0+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output0+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output0+(3*stbir__simdfX_float_count), o3 ); )
      stbIF1( stbir__simdfX_mult( o0, r0, c1 );      stbir__simdfX_mult( o1, r1, c1 );      stbir__simdfX_mult( o2, r2, c1 );       stbir__simdfX_mult( o3, r3, c1 );
              stbir__simdfX_store( output1, o0 );    stbir__simdfX_store( output1+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output1+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output1+(3*stbir__simdfX_float_count), o3 ); )
      stbIF2( stbir__simdfX_mult( o0, r0, c2 );      stbir__simdfX_mult( o1, r1, c2 );      stbir__simdfX_mult( o2, r2, c2 );       stbir__simdfX_mult( o3, r3, c2 );
              stbir__simdfX_store( output2, o0 );    stbir__simdfX_store( output2+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output2+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output2+(3*stbir__simdfX_float_count), o3 ); )
      stbIF3( stbir__simdfX_mult( o0, r0, c3 );      stbir__simdfX_mult( o1, r1, c3 );      stbir__simdfX_mult( o2, r2, c3 );       stbir__simdfX_mult( o3, r3, c3 );
              stbir__simdfX_store( output3, o0 );    stbir__simdfX_store( output3+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output3+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output3+(3*stbir__simdfX_float_count), o3 ); )
      stbIF4( stbir__simdfX_mult( o0, r0, c4 );      stbir__simdfX_mult( o1, r1, c4 );      stbir__simdfX_mult( o2, r2, c4 );       stbir__simdfX_mult( o3, r3, c4 );
              stbir__simdfX_store( output4, o0 );    stbir__simdfX_store( output4+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output4+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output4+(3*stbir__simdfX_float_count), o3 ); )
      stbIF5( stbir__simdfX_mult( o0, r0, c5 );      stbir__simdfX_mult( o1, r1, c5 );      stbir__simdfX_mult( o2, r2, c5 );       stbir__simdfX_mult( o3, r3, c5 );
              stbir__simdfX_store( output5, o0 );    stbir__simdfX_store( output5+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output5+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output5+(3*stbir__simdfX_float_count), o3 ); )
      stbIF6( stbir__simdfX_mult( o0, r0, c6 );      stbir__simdfX_mult( o1, r1, c6 );      stbir__simdfX_mult( o2, r2, c6 );       stbir__simdfX_mult( o3, r3, c6 );
              stbir__simdfX_store( output6, o0 );    stbir__simdfX_store( output6+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output6+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output6+(3*stbir__simdfX_float_count), o3 ); )
      stbIF7( stbir__simdfX_mult( o0, r0, c7 );      stbir__simdfX_mult( o1, r1, c7 );      stbir__simdfX_mult( o2, r2, c7 );       stbir__simdfX_mult( o3, r3, c7 );
              stbir__simdfX_store( output7, o0 );    stbir__simdfX_store( output7+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output7+(2*stbir__simdfX_float_count), o2 );   stbir__simdfX_store( output7+(3*stbir__simdfX_float_count), o3 ); )
      #endif

      input += (4*stbir__simdfX_float_count);
      stbIF0( output0 += (4*stbir__simdfX_float_count); ) stbIF1( output1 += (4*stbir__simdfX_float_count); ) stbIF2( output2 += (4*stbir__simdfX_float_count); ) stbIF3( output3 += (4*stbir__simdfX_float_count); ) stbIF4( output4 += (4*stbir__simdfX_float_count); ) stbIF5( output5 += (4*stbir__simdfX_float_count); ) stbIF6( output6 += (4*stbir__simdfX_float_count); ) stbIF7( output7 += (4*stbir__simdfX_float_count); )
    }
    STBIR_SIMD_NO_UNROLL_LOOP_START
    while ( ( (char*)input_end - (char*) input ) >= 16 )
    {
      stbir__simdf o0, r0;
      STBIR_SIMD_NO_UNROLL(output0);

      stbir__simdf_load( r0, input );

      #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
      stbIF0( stbir__simdf_load( o0, output0 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c0 ) );  stbir__simdf_store( output0, o0 ); )
      stbIF1( stbir__simdf_load( o0, output1 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c1 ) );  stbir__simdf_store( output1, o0 ); )
      stbIF2( stbir__simdf_load( o0, output2 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c2 ) );  stbir__simdf_store( output2, o0 ); )
      stbIF3( stbir__simdf_load( o0, output3 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c3 ) );  stbir__simdf_store( output3, o0 ); )
      stbIF4( stbir__simdf_load( o0, output4 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c4 ) );  stbir__simdf_store( output4, o0 ); )
      stbIF5( stbir__simdf_load( o0, output5 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c5 ) );  stbir__simdf_store( output5, o0 ); )
      stbIF6( stbir__simdf_load( o0, output6 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c6 ) );  stbir__simdf_store( output6, o0 ); )
      stbIF7( stbir__simdf_load( o0, output7 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c7 ) );  stbir__simdf_store( output7, o0 ); )
      #else
      stbIF0( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c0 ) );   stbir__simdf_store( output0, o0 ); )
      stbIF1( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c1 ) );   stbir__simdf_store( output1, o0 ); )
      stbIF2( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c2 ) );   stbir__simdf_store( output2, o0 ); )
      stbIF3( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c3 ) );   stbir__simdf_store( output3, o0 ); )
      stbIF4( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c4 ) );   stbir__simdf_store( output4, o0 ); )
      stbIF5( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c5 ) );   stbir__simdf_store( output5, o0 ); )
      stbIF6( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c6 ) );   stbir__simdf_store( output6, o0 ); )
      stbIF7( stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c7 ) );   stbir__simdf_store( output7, o0 ); )
      #endif

      input += 4;
      stbIF0( output0 += 4; ) stbIF1( output1 += 4; ) stbIF2( output2 += 4; ) stbIF3( output3 += 4; ) stbIF4( output4 += 4; ) stbIF5( output5 += 4; ) stbIF6( output6 += 4; ) stbIF7( output7 += 4; )
    }
  }
  #else
  STBIR_NO_UNROLL_LOOP_START
  while ( ( (char*)input_end - (char*) input ) >= 16 )
  {
    float r0, r1, r2, r3;
    STBIR_NO_UNROLL(input);

    r0 = input[0], r1 = input[1], r2 = input[2], r3 = input[3];

    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( output0[0] += ( r0 * c0s ); output0[1] += ( r1 * c0s ); output0[2] += ( r2 * c0s ); output0[3] += ( r3 * c0s ); )
    stbIF1( output1[0] += ( r0 * c1s ); output1[1] += ( r1 * c1s ); output1[2] += ( r2 * c1s ); output1[3] += ( r3 * c1s ); )
    stbIF2( output2[0] += ( r0 * c2s ); output2[1] += ( r1 * c2s ); output2[2] += ( r2 * c2s ); output2[3] += ( r3 * c2s ); )
    stbIF3( output3[0] += ( r0 * c3s ); output3[1] += ( r1 * c3s ); output3[2] += ( r2 * c3s ); output3[3] += ( r3 * c3s ); )
    stbIF4( output4[0] += ( r0 * c4s ); output4[1] += ( r1 * c4s ); output4[2] += ( r2 * c4s ); output4[3] += ( r3 * c4s ); )
    stbIF5( output5[0] += ( r0 * c5s ); output5[1] += ( r1 * c5s ); output5[2] += ( r2 * c5s ); output5[3] += ( r3 * c5s ); )
    stbIF6( output6[0] += ( r0 * c6s ); output6[1] += ( r1 * c6s ); output6[2] += ( r2 * c6s ); output6[3] += ( r3 * c6s ); )
    stbIF7( output7[0] += ( r0 * c7s ); output7[1] += ( r1 * c7s ); output7[2] += ( r2 * c7s ); output7[3] += ( r3 * c7s ); )
    #else
    stbIF0( output0[0]  = ( r0 * c0s ); output0[1]  = ( r1 * c0s ); output0[2]  = ( r2 * c0s ); output0[3]  = ( r3 * c0s ); )
    stbIF1( output1[0]  = ( r0 * c1s ); output1[1]  = ( r1 * c1s ); output1[2]  = ( r2 * c1s ); output1[3]  = ( r3 * c1s ); )
    stbIF2( output2[0]  = ( r0 * c2s ); output2[1]  = ( r1 * c2s ); output2[2]  = ( r2 * c2s ); output2[3]  = ( r3 * c2s ); )
    stbIF3( output3[0]  = ( r0 * c3s ); output3[1]  = ( r1 * c3s ); output3[2]  = ( r2 * c3s ); output3[3]  = ( r3 * c3s ); )
    stbIF4( output4[0]  = ( r0 * c4s ); output4[1]  = ( r1 * c4s ); output4[2]  = ( r2 * c4s ); output4[3]  = ( r3 * c4s ); )
    stbIF5( output5[0]  = ( r0 * c5s ); output5[1]  = ( r1 * c5s ); output5[2]  = ( r2 * c5s ); output5[3]  = ( r3 * c5s ); )
    stbIF6( output6[0]  = ( r0 * c6s ); output6[1]  = ( r1 * c6s ); output6[2]  = ( r2 * c6s ); output6[3]  = ( r3 * c6s ); )
    stbIF7( output7[0]  = ( r0 * c7s ); output7[1]  = ( r1 * c7s ); output7[2]  = ( r2 * c7s ); output7[3]  = ( r3 * c7s ); )
    #endif

    input += 4;
    stbIF0( output0 += 4; ) stbIF1( output1 += 4; ) stbIF2( output2 += 4; ) stbIF3( output3 += 4; ) stbIF4( output4 += 4; ) stbIF5( output5 += 4; ) stbIF6( output6 += 4; ) stbIF7( output7 += 4; )
  }
  #endif
  STBIR_NO_UNROLL_LOOP_START
  while ( input < input_end )
  {
    float r = input[0];
    STBIR_NO_UNROLL(output0);

    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( output0[0] += ( r * c0s ); )
    stbIF1( output1[0] += ( r * c1s ); )
    stbIF2( output2[0] += ( r * c2s ); )
    stbIF3( output3[0] += ( r * c3s ); )
    stbIF4( output4[0] += ( r * c4s ); )
    stbIF5( output5[0] += ( r * c5s ); )
    stbIF6( output6[0] += ( r * c6s ); )
    stbIF7( output7[0] += ( r * c7s ); )
    #else
    stbIF0( output0[0]  = ( r * c0s ); )
    stbIF1( output1[0]  = ( r * c1s ); )
    stbIF2( output2[0]  = ( r * c2s ); )
    stbIF3( output3[0]  = ( r * c3s ); )
    stbIF4( output4[0]  = ( r * c4s ); )
    stbIF5( output5[0]  = ( r * c5s ); )
    stbIF6( output6[0]  = ( r * c6s ); )
    stbIF7( output7[0]  = ( r * c7s ); )
    #endif

    ++input;
    stbIF0( ++output0; ) stbIF1( ++output1; ) stbIF2( ++output2; ) stbIF3( ++output3; ) stbIF4( ++output4; ) stbIF5( ++output5; ) stbIF6( ++output6; ) stbIF7( ++output7; )
  }
}

static void STBIR_chans( stbir__vertical_gather_with_,_coeffs)( float * outputp, float const * vertical_coefficients, float const ** inputs, float const * input0_end )
{
  float STBIR_SIMD_STREAMOUT_PTR( * ) output = outputp;

  stbIF0( float const * input0 = inputs[0]; float c0s = vertical_coefficients[0]; )
  stbIF1( float const * input1 = inputs[1]; float c1s = vertical_coefficients[1]; )
  stbIF2( float const * input2 = inputs[2]; float c2s = vertical_coefficients[2]; )
  stbIF3( float const * input3 = inputs[3]; float c3s = vertical_coefficients[3]; )
  stbIF4( float const * input4 = inputs[4]; float c4s = vertical_coefficients[4]; )
  stbIF5( float const * input5 = inputs[5]; float c5s = vertical_coefficients[5]; )
  stbIF6( float const * input6 = inputs[6]; float c6s = vertical_coefficients[6]; )
  stbIF7( float const * input7 = inputs[7]; float c7s = vertical_coefficients[7]; )

#if ( STBIR__vertical_channels == 1 ) && !defined(STB_IMAGE_RESIZE_VERTICAL_CONTINUE)
  // check single channel one weight
  if ( ( c0s >= (1.0f-0.000001f) ) && ( c0s <= (1.0f+0.000001f) ) )
  {
    STBIR_MEMCPY( output, input0, (char*)input0_end - (char*)input0 );
    return;
  }
#endif

  #ifdef STBIR_SIMD
  {
    stbIF0(stbir__simdfX c0 = stbir__simdf_frepX( c0s ); )
    stbIF1(stbir__simdfX c1 = stbir__simdf_frepX( c1s ); )
    stbIF2(stbir__simdfX c2 = stbir__simdf_frepX( c2s ); )
    stbIF3(stbir__simdfX c3 = stbir__simdf_frepX( c3s ); )
    stbIF4(stbir__simdfX c4 = stbir__simdf_frepX( c4s ); )
    stbIF5(stbir__simdfX c5 = stbir__simdf_frepX( c5s ); )
    stbIF6(stbir__simdfX c6 = stbir__simdf_frepX( c6s ); )
    stbIF7(stbir__simdfX c7 = stbir__simdf_frepX( c7s ); )

    STBIR_SIMD_NO_UNROLL_LOOP_START
    while ( ( (char*)input0_end - (char*) input0 ) >= (16*stbir__simdfX_float_count) )
    {
      stbir__simdfX o0, o1, o2, o3, r0, r1, r2, r3;
      STBIR_SIMD_NO_UNROLL(output);

      // prefetch four loop iterations ahead (doesn't affect much for small resizes, but helps with big ones)
      stbIF0( stbir__prefetch( input0 + (16*stbir__simdfX_float_count) ); )
      stbIF1( stbir__prefetch( input1 + (16*stbir__simdfX_float_count) ); )
      stbIF2( stbir__prefetch( input2 + (16*stbir__simdfX_float_count) ); )
      stbIF3( stbir__prefetch( input3 + (16*stbir__simdfX_float_count) ); )
      stbIF4( stbir__prefetch( input4 + (16*stbir__simdfX_float_count) ); )
      stbIF5( stbir__prefetch( input5 + (16*stbir__simdfX_float_count) ); )
      stbIF6( stbir__prefetch( input6 + (16*stbir__simdfX_float_count) ); )
      stbIF7( stbir__prefetch( input7 + (16*stbir__simdfX_float_count) ); )

      #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
      stbIF0( stbir__simdfX_load( o0, output );      stbir__simdfX_load( o1, output+stbir__simdfX_float_count );   stbir__simdfX_load( o2, output+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( o3, output+(3*stbir__simdfX_float_count) );
              stbir__simdfX_load( r0, input0 );      stbir__simdfX_load( r1, input0+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input0+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input0+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c0 );  stbir__simdfX_madd( o1, o1, r1, c0 );                         stbir__simdfX_madd( o2, o2, r2, c0 );                             stbir__simdfX_madd( o3, o3, r3, c0 ); )
      #else
      stbIF0( stbir__simdfX_load( r0, input0 );      stbir__simdfX_load( r1, input0+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input0+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input0+(3*stbir__simdfX_float_count) );
              stbir__simdfX_mult( o0, r0, c0 );      stbir__simdfX_mult( o1, r1, c0 );                             stbir__simdfX_mult( o2, r2, c0 );                                 stbir__simdfX_mult( o3, r3, c0 );  )
      #endif

      stbIF1( stbir__simdfX_load( r0, input1 );      stbir__simdfX_load( r1, input1+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input1+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input1+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c1 );  stbir__simdfX_madd( o1, o1, r1, c1 );                         stbir__simdfX_madd( o2, o2, r2, c1 );                             stbir__simdfX_madd( o3, o3, r3, c1 ); )
      stbIF2( stbir__simdfX_load( r0, input2 );      stbir__simdfX_load( r1, input2+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input2+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input2+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c2 );  stbir__simdfX_madd( o1, o1, r1, c2 );                         stbir__simdfX_madd( o2, o2, r2, c2 );                             stbir__simdfX_madd( o3, o3, r3, c2 ); )
      stbIF3( stbir__simdfX_load( r0, input3 );      stbir__simdfX_load( r1, input3+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input3+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input3+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c3 );  stbir__simdfX_madd( o1, o1, r1, c3 );                         stbir__simdfX_madd( o2, o2, r2, c3 );                             stbir__simdfX_madd( o3, o3, r3, c3 ); )
      stbIF4( stbir__simdfX_load( r0, input4 );      stbir__simdfX_load( r1, input4+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input4+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input4+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c4 );  stbir__simdfX_madd( o1, o1, r1, c4 );                         stbir__simdfX_madd( o2, o2, r2, c4 );                             stbir__simdfX_madd( o3, o3, r3, c4 ); )
      stbIF5( stbir__simdfX_load( r0, input5 );      stbir__simdfX_load( r1, input5+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input5+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input5+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c5 );  stbir__simdfX_madd( o1, o1, r1, c5 );                         stbir__simdfX_madd( o2, o2, r2, c5 );                             stbir__simdfX_madd( o3, o3, r3, c5 ); )
      stbIF6( stbir__simdfX_load( r0, input6 );      stbir__simdfX_load( r1, input6+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input6+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input6+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c6 );  stbir__simdfX_madd( o1, o1, r1, c6 );                         stbir__simdfX_madd( o2, o2, r2, c6 );                             stbir__simdfX_madd( o3, o3, r3, c6 ); )
      stbIF7( stbir__simdfX_load( r0, input7 );      stbir__simdfX_load( r1, input7+stbir__simdfX_float_count );   stbir__simdfX_load( r2, input7+(2*stbir__simdfX_float_count) );   stbir__simdfX_load( r3, input7+(3*stbir__simdfX_float_count) );
              stbir__simdfX_madd( o0, o0, r0, c7 );  stbir__simdfX_madd( o1, o1, r1, c7 );                         stbir__simdfX_madd( o2, o2, r2, c7 );                             stbir__simdfX_madd( o3, o3, r3, c7 ); )

      stbir__simdfX_store( output, o0 );             stbir__simdfX_store( output+stbir__simdfX_float_count, o1 );  stbir__simdfX_store( output+(2*stbir__simdfX_float_count), o2 );  stbir__simdfX_store( output+(3*stbir__simdfX_float_count), o3 );
      output += (4*stbir__simdfX_float_count);
      stbIF0( input0 += (4*stbir__simdfX_float_count); ) stbIF1( input1 += (4*stbir__simdfX_float_count); ) stbIF2( input2 += (4*stbir__simdfX_float_count); ) stbIF3( input3 += (4*stbir__simdfX_float_count); ) stbIF4( input4 += (4*stbir__simdfX_float_count); ) stbIF5( input5 += (4*stbir__simdfX_float_count); ) stbIF6( input6 += (4*stbir__simdfX_float_count); ) stbIF7( input7 += (4*stbir__simdfX_float_count); )
    }

    STBIR_SIMD_NO_UNROLL_LOOP_START
    while ( ( (char*)input0_end - (char*) input0 ) >= 16 )
    {
      stbir__simdf o0, r0;
      STBIR_SIMD_NO_UNROLL(output);

      #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
      stbIF0( stbir__simdf_load( o0, output );   stbir__simdf_load( r0, input0 ); stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c0 ) ); )
      #else
      stbIF0( stbir__simdf_load( r0, input0 );  stbir__simdf_mult( o0, r0, stbir__if_simdf8_cast_to_simdf4( c0 ) ); )
      #endif
      stbIF1( stbir__simdf_load( r0, input1 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c1 ) ); )
      stbIF2( stbir__simdf_load( r0, input2 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c2 ) ); )
      stbIF3( stbir__simdf_load( r0, input3 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c3 ) ); )
      stbIF4( stbir__simdf_load( r0, input4 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c4 ) ); )
      stbIF5( stbir__simdf_load( r0, input5 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c5 ) ); )
      stbIF6( stbir__simdf_load( r0, input6 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c6 ) ); )
      stbIF7( stbir__simdf_load( r0, input7 );  stbir__simdf_madd( o0, o0, r0, stbir__if_simdf8_cast_to_simdf4( c7 ) ); )

      stbir__simdf_store( output, o0 );
      output += 4;
      stbIF0( input0 += 4; ) stbIF1( input1 += 4; ) stbIF2( input2 += 4; ) stbIF3( input3 += 4; ) stbIF4( input4 += 4; ) stbIF5( input5 += 4; ) stbIF6( input6 += 4; ) stbIF7( input7 += 4; )
    }
  }
  #else
  STBIR_NO_UNROLL_LOOP_START
  while ( ( (char*)input0_end - (char*) input0 ) >= 16 )
  {
    float o0, o1, o2, o3;
    STBIR_NO_UNROLL(output);
    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( o0 = output[0] + input0[0] * c0s; o1 = output[1] + input0[1] * c0s; o2 = output[2] + input0[2] * c0s; o3 = output[3] + input0[3] * c0s; )
    #else
    stbIF0( o0  = input0[0] * c0s; o1  = input0[1] * c0s; o2  = input0[2] * c0s; o3  = input0[3] * c0s; )
    #endif
    stbIF1( o0 += input1[0] * c1s; o1 += input1[1] * c1s; o2 += input1[2] * c1s; o3 += input1[3] * c1s; )
    stbIF2( o0 += input2[0] * c2s; o1 += input2[1] * c2s; o2 += input2[2] * c2s; o3 += input2[3] * c2s; )
    stbIF3( o0 += input3[0] * c3s; o1 += input3[1] * c3s; o2 += input3[2] * c3s; o3 += input3[3] * c3s; )
    stbIF4( o0 += input4[0] * c4s; o1 += input4[1] * c4s; o2 += input4[2] * c4s; o3 += input4[3] * c4s; )
    stbIF5( o0 += input5[0] * c5s; o1 += input5[1] * c5s; o2 += input5[2] * c5s; o3 += input5[3] * c5s; )
    stbIF6( o0 += input6[0] * c6s; o1 += input6[1] * c6s; o2 += input6[2] * c6s; o3 += input6[3] * c6s; )
    stbIF7( o0 += input7[0] * c7s; o1 += input7[1] * c7s; o2 += input7[2] * c7s; o3 += input7[3] * c7s; )
    output[0] = o0; output[1] = o1; output[2] = o2; output[3] = o3;
    output += 4;
    stbIF0( input0 += 4; ) stbIF1( input1 += 4; ) stbIF2( input2 += 4; ) stbIF3( input3 += 4; ) stbIF4( input4 += 4; ) stbIF5( input5 += 4; ) stbIF6( input6 += 4; ) stbIF7( input7 += 4; )
  }
  #endif
  STBIR_NO_UNROLL_LOOP_START
  while ( input0 < input0_end )
  {
    float o0;
    STBIR_NO_UNROLL(output);
    #ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
    stbIF0( o0 = output[0] + input0[0] * c0s; )
    #else
    stbIF0( o0  = input0[0] * c0s; )
    #endif
    stbIF1( o0 += input1[0] * c1s; )
    stbIF2( o0 += input2[0] * c2s; )
    stbIF3( o0 += input3[0] * c3s; )
    stbIF4( o0 += input4[0] * c4s; )
    stbIF5( o0 += input5[0] * c5s; )
    stbIF6( o0 += input6[0] * c6s; )
    stbIF7( o0 += input7[0] * c7s; )
    output[0] = o0;
    ++output;
    stbIF0( ++input0; ) stbIF1( ++input1; ) stbIF2( ++input2; ) stbIF3( ++input3; ) stbIF4( ++input4; ) stbIF5( ++input5; ) stbIF6( ++input6; ) stbIF7( ++input7; )
  }
}

#undef stbIF0
#undef stbIF1
#undef stbIF2
#undef stbIF3
#undef stbIF4
#undef stbIF5
#undef stbIF6
#undef stbIF7
#undef STB_IMAGE_RESIZE_DO_VERTICALS
#undef STBIR__vertical_channels
#undef STB_IMAGE_RESIZE_DO_HORIZONTALS
#undef STBIR_chans
#ifdef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#undef STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#endif
