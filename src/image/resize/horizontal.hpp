// The horizontal resamplers for 1, 2, 3, 4 and 7 channels (instances of horizontals.inl).
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

// Do 1 channel horizontal routines

#ifdef STBIR_SIMD

#define stbir__1_coeff_only()          \
    stbir__simdf tot,c;                \
    STBIR_SIMD_NO_UNROLL(decode);      \
    stbir__simdf_load1( c, hc );       \
    stbir__simdf_mult1_mem( tot, c, decode );

#define stbir__2_coeff_only()          \
    stbir__simdf tot,c,d;              \
    STBIR_SIMD_NO_UNROLL(decode);      \
    stbir__simdf_load2z( c, hc );      \
    stbir__simdf_load2( d, decode );   \
    stbir__simdf_mult( tot, c, d );    \
    stbir__simdf_0123to1230( c, tot ); \
    stbir__simdf_add1( tot, tot, c );

#define stbir__3_coeff_only()                  \
    stbir__simdf tot,c,t;                      \
    STBIR_SIMD_NO_UNROLL(decode);              \
    stbir__simdf_load( c, hc );                \
    stbir__simdf_mult_mem( tot, c, decode );   \
    stbir__simdf_0123to1230( c, tot );         \
    stbir__simdf_0123to2301( t, tot );         \
    stbir__simdf_add1( tot, tot, c );          \
    stbir__simdf_add1( tot, tot, t );

#define stbir__store_output_tiny()                \
    stbir__simdf_store1( output, tot );           \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 1;

#define stbir__4_coeff_start()                 \
    stbir__simdf tot,c;                        \
    STBIR_SIMD_NO_UNROLL(decode);              \
    stbir__simdf_load( c, hc );                \
    stbir__simdf_mult_mem( tot, c, decode );   \

#define stbir__4_coeff_continue_from_4( ofs )  \
    STBIR_SIMD_NO_UNROLL(decode);              \
    stbir__simdf_load( c, hc + (ofs) );        \
    stbir__simdf_madd_mem( tot, tot, c, decode+(ofs) );

#define stbir__1_coeff_remnant( ofs )          \
    { stbir__simdf d;                          \
    stbir__simdf_load1z( c, hc + (ofs) );      \
    stbir__simdf_load1( d, decode + (ofs) );   \
    stbir__simdf_madd( tot, tot, d, c ); }

#define stbir__2_coeff_remnant( ofs )          \
    { stbir__simdf d;                          \
    stbir__simdf_load2z( c, hc+(ofs) );        \
    stbir__simdf_load2( d, decode+(ofs) );     \
    stbir__simdf_madd( tot, tot, d, c ); }

#define stbir__3_coeff_setup()                 \
    stbir__simdf mask;                         \
    stbir__simdf_load( mask, STBIR_mask + 3 );

#define stbir__3_coeff_remnant( ofs )                  \
    stbir__simdf_load( c, hc+(ofs) );                  \
    stbir__simdf_and( c, c, mask );                    \
    stbir__simdf_madd_mem( tot, tot, c, decode+(ofs) );

#define stbir__store_output()                     \
    stbir__simdf_0123to2301( c, tot );            \
    stbir__simdf_add( tot, tot, c );              \
    stbir__simdf_0123to1230( c, tot );            \
    stbir__simdf_add1( tot, tot, c );             \
    stbir__simdf_store1( output, tot );           \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 1;

#else

#define stbir__1_coeff_only()  \
    float tot;                 \
    tot = decode[0]*hc[0];

#define stbir__2_coeff_only()  \
    float tot;                 \
    tot = decode[0] * hc[0];   \
    tot += decode[1] * hc[1];

#define stbir__3_coeff_only()  \
    float tot;                 \
    tot = decode[0] * hc[0];   \
    tot += decode[1] * hc[1];  \
    tot += decode[2] * hc[2];

#define stbir__store_output_tiny()                \
    output[0] = tot;                              \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 1;

#define stbir__4_coeff_start()  \
    float tot0,tot1,tot2,tot3;  \
    tot0 = decode[0] * hc[0];   \
    tot1 = decode[1] * hc[1];   \
    tot2 = decode[2] * hc[2];   \
    tot3 = decode[3] * hc[3];

#define stbir__4_coeff_continue_from_4( ofs )  \
    tot0 += decode[0+(ofs)] * hc[0+(ofs)];     \
    tot1 += decode[1+(ofs)] * hc[1+(ofs)];     \
    tot2 += decode[2+(ofs)] * hc[2+(ofs)];     \
    tot3 += decode[3+(ofs)] * hc[3+(ofs)];

#define stbir__1_coeff_remnant( ofs )        \
    tot0 += decode[0+(ofs)] * hc[0+(ofs)];

#define stbir__2_coeff_remnant( ofs )        \
    tot0 += decode[0+(ofs)] * hc[0+(ofs)];   \
    tot1 += decode[1+(ofs)] * hc[1+(ofs)];   \

#define stbir__3_coeff_remnant( ofs )        \
    tot0 += decode[0+(ofs)] * hc[0+(ofs)];   \
    tot1 += decode[1+(ofs)] * hc[1+(ofs)];   \
    tot2 += decode[2+(ofs)] * hc[2+(ofs)];

#define stbir__store_output()                     \
    output[0] = (tot0+tot2)+(tot1+tot3);          \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 1;

#endif

#define STBIR__horizontal_channels 1
#include "image/resize/horizontals.inl"


//=================
// Do 2 channel horizontal routines

#ifdef STBIR_SIMD

#define stbir__1_coeff_only()         \
    stbir__simdf tot,c,d;             \
    STBIR_SIMD_NO_UNROLL(decode);     \
    stbir__simdf_load1z( c, hc );     \
    stbir__simdf_0123to0011( c, c );  \
    stbir__simdf_load2( d, decode );  \
    stbir__simdf_mult( tot, d, c );

#define stbir__2_coeff_only()         \
    stbir__simdf tot,c;               \
    STBIR_SIMD_NO_UNROLL(decode);     \
    stbir__simdf_load2( c, hc );      \
    stbir__simdf_0123to0011( c, c );  \
    stbir__simdf_mult_mem( tot, c, decode );

#define stbir__3_coeff_only()                \
    stbir__simdf tot,c,cs,d;                 \
    STBIR_SIMD_NO_UNROLL(decode);            \
    stbir__simdf_load( cs, hc );             \
    stbir__simdf_0123to0011( c, cs );        \
    stbir__simdf_mult_mem( tot, c, decode ); \
    stbir__simdf_0123to2222( c, cs );        \
    stbir__simdf_load2z( d, decode+4 );      \
    stbir__simdf_madd( tot, tot, d, c );

#define stbir__store_output_tiny()                \
    stbir__simdf_0123to2301( c, tot );            \
    stbir__simdf_add( tot, tot, c );              \
    stbir__simdf_store2( output, tot );           \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 2;

#ifdef STBIR_SIMD8

#define stbir__4_coeff_start()                    \
    stbir__simdf8 tot0,c,cs;                      \
    STBIR_SIMD_NO_UNROLL(decode);                 \
    stbir__simdf8_load4b( cs, hc );               \
    stbir__simdf8_0123to00112233( c, cs );        \
    stbir__simdf8_mult_mem( tot0, c, decode );

#define stbir__4_coeff_continue_from_4( ofs )        \
    STBIR_SIMD_NO_UNROLL(decode);                    \
    stbir__simdf8_load4b( cs, hc + (ofs) );          \
    stbir__simdf8_0123to00112233( c, cs );           \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*2 );

#define stbir__1_coeff_remnant( ofs )                \
    { stbir__simdf t,d;                              \
    stbir__simdf_load1z( t, hc + (ofs) );            \
    stbir__simdf_load2( d, decode + (ofs) * 2 );     \
    stbir__simdf_0123to0011( t, t );                 \
    stbir__simdf_mult( t, t, d );                    \
    stbir__simdf8_add4( tot0, tot0, t ); }
 
#define stbir__2_coeff_remnant( ofs )                \
    { stbir__simdf t;                                \
    stbir__simdf_load2( t, hc + (ofs) );             \
    stbir__simdf_0123to0011( t, t );                 \
    stbir__simdf_mult_mem( t, t, decode+(ofs)*2 );   \
    stbir__simdf8_add4( tot0, tot0, t ); }

#define stbir__3_coeff_remnant( ofs )                \
    { stbir__simdf8 d;                               \
    stbir__simdf8_load4b( cs, hc + (ofs) );          \
    stbir__simdf8_0123to00112233( c, cs );           \
    stbir__simdf8_load6z( d, decode+(ofs)*2 );       \
    stbir__simdf8_madd( tot0, tot0, c, d ); }

#define stbir__store_output()                     \
    { stbir__simdf t,d;                           \
    stbir__simdf8_add4halves( t, stbir__if_simdf8_cast_to_simdf4(tot0), tot0 );    \
    stbir__simdf_0123to2301( d, t );              \
    stbir__simdf_add( t, t, d );                  \
    stbir__simdf_store2( output, t );             \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 2; }

#else

#define stbir__4_coeff_start()                   \
    stbir__simdf tot0,tot1,c,cs;                 \
    STBIR_SIMD_NO_UNROLL(decode);                \
    stbir__simdf_load( cs, hc );                 \
    stbir__simdf_0123to0011( c, cs );            \
    stbir__simdf_mult_mem( tot0, c, decode );    \
    stbir__simdf_0123to2233( c, cs );            \
    stbir__simdf_mult_mem( tot1, c, decode+4 );

#define stbir__4_coeff_continue_from_4( ofs )                \
    STBIR_SIMD_NO_UNROLL(decode);                            \
    stbir__simdf_load( cs, hc + (ofs) );                     \
    stbir__simdf_0123to0011( c, cs );                        \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*2 );  \
    stbir__simdf_0123to2233( c, cs );                        \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*2+4 );

#define stbir__1_coeff_remnant( ofs )            \
    { stbir__simdf d;                            \
    stbir__simdf_load1z( cs, hc + (ofs) );       \
    stbir__simdf_0123to0011( c, cs );            \
    stbir__simdf_load2( d, decode + (ofs) * 2 ); \
    stbir__simdf_madd( tot0, tot0, d, c ); }

#define stbir__2_coeff_remnant( ofs )                      \
    stbir__simdf_load2( cs, hc + (ofs) );                  \
    stbir__simdf_0123to0011( c, cs );                      \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*2 );

#define stbir__3_coeff_remnant( ofs )                       \
    { stbir__simdf d;                                       \
    stbir__simdf_load( cs, hc + (ofs) );                    \
    stbir__simdf_0123to0011( c, cs );                       \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*2 ); \
    stbir__simdf_0123to2222( c, cs );                       \
    stbir__simdf_load2z( d, decode + (ofs) * 2 + 4 );       \
    stbir__simdf_madd( tot1, tot1, d, c ); }

#define stbir__store_output()                     \
    stbir__simdf_add( tot0, tot0, tot1 );         \
    stbir__simdf_0123to2301( c, tot0 );           \
    stbir__simdf_add( tot0, tot0, c );            \
    stbir__simdf_store2( output, tot0 );          \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 2;

#endif

#else

#define stbir__1_coeff_only()  \
    float tota,totb,c;         \
    c = hc[0];                 \
    tota = decode[0]*c;        \
    totb = decode[1]*c;

#define stbir__2_coeff_only()  \
    float tota,totb,c;         \
    c = hc[0];                 \
    tota = decode[0]*c;        \
    totb = decode[1]*c;        \
    c = hc[1];                 \
    tota += decode[2]*c;       \
    totb += decode[3]*c;

// this weird order of add matches the simd
#define stbir__3_coeff_only()  \
    float tota,totb,c;         \
    c = hc[0];                 \
    tota = decode[0]*c;        \
    totb = decode[1]*c;        \
    c = hc[2];                 \
    tota += decode[4]*c;       \
    totb += decode[5]*c;       \
    c = hc[1];                 \
    tota += decode[2]*c;       \
    totb += decode[3]*c;

#define stbir__store_output_tiny()                \
    output[0] = tota;                             \
    output[1] = totb;                             \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 2;

#define stbir__4_coeff_start()      \
    float tota0,tota1,tota2,tota3,totb0,totb1,totb2,totb3,c;  \
    c = hc[0];                      \
    tota0 = decode[0]*c;            \
    totb0 = decode[1]*c;            \
    c = hc[1];                      \
    tota1 = decode[2]*c;            \
    totb1 = decode[3]*c;            \
    c = hc[2];                      \
    tota2 = decode[4]*c;            \
    totb2 = decode[5]*c;            \
    c = hc[3];                      \
    tota3 = decode[6]*c;            \
    totb3 = decode[7]*c;

#define stbir__4_coeff_continue_from_4( ofs )  \
    c = hc[0+(ofs)];                           \
    tota0 += decode[0+(ofs)*2]*c;              \
    totb0 += decode[1+(ofs)*2]*c;              \
    c = hc[1+(ofs)];                           \
    tota1 += decode[2+(ofs)*2]*c;              \
    totb1 += decode[3+(ofs)*2]*c;              \
    c = hc[2+(ofs)];                           \
    tota2 += decode[4+(ofs)*2]*c;              \
    totb2 += decode[5+(ofs)*2]*c;              \
    c = hc[3+(ofs)];                           \
    tota3 += decode[6+(ofs)*2]*c;              \
    totb3 += decode[7+(ofs)*2]*c;

#define stbir__1_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*2] * c;    \
    totb0 += decode[1+(ofs)*2] * c;

#define stbir__2_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*2] * c;    \
    totb0 += decode[1+(ofs)*2] * c;    \
    c = hc[1+(ofs)];                   \
    tota1 += decode[2+(ofs)*2] * c;    \
    totb1 += decode[3+(ofs)*2] * c;

#define stbir__3_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*2] * c;    \
    totb0 += decode[1+(ofs)*2] * c;    \
    c = hc[1+(ofs)];                   \
    tota1 += decode[2+(ofs)*2] * c;    \
    totb1 += decode[3+(ofs)*2] * c;    \
    c = hc[2+(ofs)];                   \
    tota2 += decode[4+(ofs)*2] * c;    \
    totb2 += decode[5+(ofs)*2] * c;

#define stbir__store_output()                     \
    output[0] = (tota0+tota2)+(tota1+tota3);      \
    output[1] = (totb0+totb2)+(totb1+totb3);      \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 2;

#endif

#define STBIR__horizontal_channels 2
#include "image/resize/horizontals.inl"


//=================
// Do 3 channel horizontal routines

#ifdef STBIR_SIMD

#define stbir__1_coeff_only()         \
    stbir__simdf tot,c,d;             \
    STBIR_SIMD_NO_UNROLL(decode);     \
    stbir__simdf_load1z( c, hc );     \
    stbir__simdf_0123to0001( c, c );  \
    stbir__simdf_load( d, decode );   \
    stbir__simdf_mult( tot, d, c );

#define stbir__2_coeff_only()         \
    stbir__simdf tot,c,cs,d;          \
    STBIR_SIMD_NO_UNROLL(decode);     \
    stbir__simdf_load2( cs, hc );     \
    stbir__simdf_0123to0000( c, cs ); \
    stbir__simdf_load( d, decode );   \
    stbir__simdf_mult( tot, d, c );   \
    stbir__simdf_0123to1111( c, cs ); \
    stbir__simdf_load( d, decode+3 ); \
    stbir__simdf_madd( tot, tot, d, c );

#define stbir__3_coeff_only()            \
    stbir__simdf tot,c,d,cs;             \
    STBIR_SIMD_NO_UNROLL(decode);        \
    stbir__simdf_load( cs, hc );         \
    stbir__simdf_0123to0000( c, cs );    \
    stbir__simdf_load( d, decode );      \
    stbir__simdf_mult( tot, d, c );      \
    stbir__simdf_0123to1111( c, cs );    \
    stbir__simdf_load( d, decode+3 );    \
    stbir__simdf_madd( tot, tot, d, c ); \
    stbir__simdf_0123to2222( c, cs );    \
    stbir__simdf_load( d, decode+6 );    \
    stbir__simdf_madd( tot, tot, d, c );

#define stbir__store_output_tiny()                \
    stbir__simdf_store2( output, tot );           \
    stbir__simdf_0123to2301( tot, tot );          \
    stbir__simdf_store1( output+2, tot );         \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 3;

#ifdef STBIR_SIMD8

// we're loading from the XXXYYY decode by -1 to get the XXXYYY into different halves of the AVX reg fyi
#define stbir__4_coeff_start()                     \
    stbir__simdf8 tot0,tot1,c,cs; stbir__simdf t;  \
    STBIR_SIMD_NO_UNROLL(decode);                  \
    stbir__simdf8_load4b( cs, hc );                \
    stbir__simdf8_0123to00001111( c, cs );         \
    stbir__simdf8_mult_mem( tot0, c, decode - 1 ); \
    stbir__simdf8_0123to22223333( c, cs );         \
    stbir__simdf8_mult_mem( tot1, c, decode+6 - 1 );

#define stbir__4_coeff_continue_from_4( ofs )      \
    STBIR_SIMD_NO_UNROLL(decode);                  \
    stbir__simdf8_load4b( cs, hc + (ofs) );        \
    stbir__simdf8_0123to00001111( c, cs );         \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*3 - 1 ); \
    stbir__simdf8_0123to22223333( c, cs );         \
    stbir__simdf8_madd_mem( tot1, tot1, c, decode+(ofs)*3 + 6 - 1 );

#define stbir__1_coeff_remnant( ofs )                          \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf_load1rep4( t, hc + (ofs) );                   \
    stbir__simdf8_madd_mem4( tot0, tot0, t, decode+(ofs)*3 - 1 );

#define stbir__2_coeff_remnant( ofs )                          \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf8_load4b( cs, hc + (ofs) - 2 );                \
    stbir__simdf8_0123to22223333( c, cs );                     \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*3 - 1 );

 #define stbir__3_coeff_remnant( ofs )                           \
    STBIR_SIMD_NO_UNROLL(decode);                                \
    stbir__simdf8_load4b( cs, hc + (ofs) );                      \
    stbir__simdf8_0123to00001111( c, cs );                       \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*3 - 1 ); \
    stbir__simdf8_0123to2222( t, cs );                           \
    stbir__simdf8_madd_mem4( tot1, tot1, t, decode+(ofs)*3 + 6 - 1 );

#define stbir__store_output()                       \
    stbir__simdf8_add( tot0, tot0, tot1 );          \
    stbir__simdf_0123to1230( t, stbir__if_simdf8_cast_to_simdf4( tot0 ) ); \
    stbir__simdf8_add4halves( t, t, tot0 );         \
    horizontal_coefficients += coefficient_width;   \
    ++horizontal_contributors;                      \
    output += 3;                                    \
    if ( output < output_end )                      \
    {                                               \
      stbir__simdf_store( output-3, t );            \
      continue;                                     \
    }                                               \
    { stbir__simdf tt; stbir__simdf_0123to2301( tt, t ); \
    stbir__simdf_store2( output-3, t );             \
    stbir__simdf_store1( output+2-3, tt ); }        \
    break;


#else

#define stbir__4_coeff_start()                  \
    stbir__simdf tot0,tot1,tot2,c,cs;           \
    STBIR_SIMD_NO_UNROLL(decode);               \
    stbir__simdf_load( cs, hc );                \
    stbir__simdf_0123to0001( c, cs );           \
    stbir__simdf_mult_mem( tot0, c, decode );   \
    stbir__simdf_0123to1122( c, cs );           \
    stbir__simdf_mult_mem( tot1, c, decode+4 ); \
    stbir__simdf_0123to2333( c, cs );           \
    stbir__simdf_mult_mem( tot2, c, decode+8 );

#define stbir__4_coeff_continue_from_4( ofs )                 \
    STBIR_SIMD_NO_UNROLL(decode);                             \
    stbir__simdf_load( cs, hc + (ofs) );                      \
    stbir__simdf_0123to0001( c, cs );                         \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*3 );   \
    stbir__simdf_0123to1122( c, cs );                         \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*3+4 ); \
    stbir__simdf_0123to2333( c, cs );                         \
    stbir__simdf_madd_mem( tot2, tot2, c, decode+(ofs)*3+8 );

#define stbir__1_coeff_remnant( ofs )         \
    STBIR_SIMD_NO_UNROLL(decode);             \
    stbir__simdf_load1z( c, hc + (ofs) );     \
    stbir__simdf_0123to0001( c, c );          \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*3 );

#define stbir__2_coeff_remnant( ofs )                       \
    { stbir__simdf d;                                       \
    STBIR_SIMD_NO_UNROLL(decode);                           \
    stbir__simdf_load2z( cs, hc + (ofs) );                  \
    stbir__simdf_0123to0001( c, cs );                       \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*3 ); \
    stbir__simdf_0123to1122( c, cs );                       \
    stbir__simdf_load2z( d, decode+(ofs)*3+4 );             \
    stbir__simdf_madd( tot1, tot1, c, d ); }

#define stbir__3_coeff_remnant( ofs )                         \
    { stbir__simdf d;                                         \
    STBIR_SIMD_NO_UNROLL(decode);                             \
    stbir__simdf_load( cs, hc + (ofs) );                      \
    stbir__simdf_0123to0001( c, cs );                         \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*3 );   \
    stbir__simdf_0123to1122( c, cs );                         \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*3+4 ); \
    stbir__simdf_0123to2222( c, cs );                         \
    stbir__simdf_load1z( d, decode+(ofs)*3+8 );               \
    stbir__simdf_madd( tot2, tot2, c, d );  }

#define stbir__store_output()                       \
    stbir__simdf_0123ABCDto3ABx( c, tot0, tot1 );   \
    stbir__simdf_0123ABCDto23Ax( cs, tot1, tot2 );  \
    stbir__simdf_0123to1230( tot2, tot2 );          \
    stbir__simdf_add( tot0, tot0, cs );             \
    stbir__simdf_add( c, c, tot2 );                 \
    stbir__simdf_add( tot0, tot0, c );              \
    horizontal_coefficients += coefficient_width;   \
    ++horizontal_contributors;                      \
    output += 3;                                    \
    if ( output < output_end )                      \
    {                                               \
      stbir__simdf_store( output-3, tot0 );         \
      continue;                                     \
    }                                               \
    stbir__simdf_0123to2301( tot1, tot0 );          \
    stbir__simdf_store2( output-3, tot0 );          \
    stbir__simdf_store1( output+2-3, tot1 );        \
    break;

#endif

#else

#define stbir__1_coeff_only()  \
    float tot0, tot1, tot2, c; \
    c = hc[0];                 \
    tot0 = decode[0]*c;        \
    tot1 = decode[1]*c;        \
    tot2 = decode[2]*c;

#define stbir__2_coeff_only()  \
    float tot0, tot1, tot2, c; \
    c = hc[0];                 \
    tot0 = decode[0]*c;        \
    tot1 = decode[1]*c;        \
    tot2 = decode[2]*c;        \
    c = hc[1];                 \
    tot0 += decode[3]*c;       \
    tot1 += decode[4]*c;       \
    tot2 += decode[5]*c;

#define stbir__3_coeff_only()  \
    float tot0, tot1, tot2, c; \
    c = hc[0];                 \
    tot0 = decode[0]*c;        \
    tot1 = decode[1]*c;        \
    tot2 = decode[2]*c;        \
    c = hc[1];                 \
    tot0 += decode[3]*c;       \
    tot1 += decode[4]*c;       \
    tot2 += decode[5]*c;       \
    c = hc[2];                 \
    tot0 += decode[6]*c;       \
    tot1 += decode[7]*c;       \
    tot2 += decode[8]*c;

#define stbir__store_output_tiny()                \
    output[0] = tot0;                             \
    output[1] = tot1;                             \
    output[2] = tot2;                             \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 3;

#define stbir__4_coeff_start()      \
    float tota0,tota1,tota2,totb0,totb1,totb2,totc0,totc1,totc2,totd0,totd1,totd2,c;  \
    c = hc[0];                      \
    tota0 = decode[0]*c;            \
    tota1 = decode[1]*c;            \
    tota2 = decode[2]*c;            \
    c = hc[1];                      \
    totb0 = decode[3]*c;            \
    totb1 = decode[4]*c;            \
    totb2 = decode[5]*c;            \
    c = hc[2];                      \
    totc0 = decode[6]*c;            \
    totc1 = decode[7]*c;            \
    totc2 = decode[8]*c;            \
    c = hc[3];                      \
    totd0 = decode[9]*c;            \
    totd1 = decode[10]*c;           \
    totd2 = decode[11]*c;

#define stbir__4_coeff_continue_from_4( ofs )  \
    c = hc[0+(ofs)];                           \
    tota0 += decode[0+(ofs)*3]*c;              \
    tota1 += decode[1+(ofs)*3]*c;              \
    tota2 += decode[2+(ofs)*3]*c;              \
    c = hc[1+(ofs)];                           \
    totb0 += decode[3+(ofs)*3]*c;              \
    totb1 += decode[4+(ofs)*3]*c;              \
    totb2 += decode[5+(ofs)*3]*c;              \
    c = hc[2+(ofs)];                           \
    totc0 += decode[6+(ofs)*3]*c;              \
    totc1 += decode[7+(ofs)*3]*c;              \
    totc2 += decode[8+(ofs)*3]*c;              \
    c = hc[3+(ofs)];                           \
    totd0 += decode[9+(ofs)*3]*c;              \
    totd1 += decode[10+(ofs)*3]*c;             \
    totd2 += decode[11+(ofs)*3]*c;

#define stbir__1_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*3]*c;      \
    tota1 += decode[1+(ofs)*3]*c;      \
    tota2 += decode[2+(ofs)*3]*c;

#define stbir__2_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*3]*c;      \
    tota1 += decode[1+(ofs)*3]*c;      \
    tota2 += decode[2+(ofs)*3]*c;      \
    c = hc[1+(ofs)];                   \
    totb0 += decode[3+(ofs)*3]*c;      \
    totb1 += decode[4+(ofs)*3]*c;      \
    totb2 += decode[5+(ofs)*3]*c;      \

#define stbir__3_coeff_remnant( ofs )  \
    c = hc[0+(ofs)];                   \
    tota0 += decode[0+(ofs)*3]*c;      \
    tota1 += decode[1+(ofs)*3]*c;      \
    tota2 += decode[2+(ofs)*3]*c;      \
    c = hc[1+(ofs)];                   \
    totb0 += decode[3+(ofs)*3]*c;      \
    totb1 += decode[4+(ofs)*3]*c;      \
    totb2 += decode[5+(ofs)*3]*c;      \
    c = hc[2+(ofs)];                   \
    totc0 += decode[6+(ofs)*3]*c;      \
    totc1 += decode[7+(ofs)*3]*c;      \
    totc2 += decode[8+(ofs)*3]*c;

#define stbir__store_output()                     \
    output[0] = (tota0+totc0)+(totb0+totd0);      \
    output[1] = (tota1+totc1)+(totb1+totd1);      \
    output[2] = (tota2+totc2)+(totb2+totd2);      \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 3;

#endif

#define STBIR__horizontal_channels 3
#include "image/resize/horizontals.inl"

//=================
// Do 4 channel horizontal routines

#ifdef STBIR_SIMD

#define stbir__1_coeff_only()             \
    stbir__simdf tot,c;                   \
    STBIR_SIMD_NO_UNROLL(decode);         \
    stbir__simdf_load1( c, hc );          \
    stbir__simdf_0123to0000( c, c );      \
    stbir__simdf_mult_mem( tot, c, decode );

#define stbir__2_coeff_only()                       \
    stbir__simdf tot,c,cs;                          \
    STBIR_SIMD_NO_UNROLL(decode);                   \
    stbir__simdf_load2( cs, hc );                   \
    stbir__simdf_0123to0000( c, cs );               \
    stbir__simdf_mult_mem( tot, c, decode );        \
    stbir__simdf_0123to1111( c, cs );               \
    stbir__simdf_madd_mem( tot, tot, c, decode+4 );

#define stbir__3_coeff_only()                       \
    stbir__simdf tot,c,cs;                          \
    STBIR_SIMD_NO_UNROLL(decode);                   \
    stbir__simdf_load( cs, hc );                    \
    stbir__simdf_0123to0000( c, cs );               \
    stbir__simdf_mult_mem( tot, c, decode );        \
    stbir__simdf_0123to1111( c, cs );               \
    stbir__simdf_madd_mem( tot, tot, c, decode+4 ); \
    stbir__simdf_0123to2222( c, cs );               \
    stbir__simdf_madd_mem( tot, tot, c, decode+8 );

#define stbir__store_output_tiny()                \
    stbir__simdf_store( output, tot );            \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 4;

#ifdef STBIR_SIMD8

#define stbir__4_coeff_start()                     \
    stbir__simdf8 tot0,c,cs; stbir__simdf t;  \
    STBIR_SIMD_NO_UNROLL(decode);                  \
    stbir__simdf8_load4b( cs, hc );                \
    stbir__simdf8_0123to00001111( c, cs );         \
    stbir__simdf8_mult_mem( tot0, c, decode );     \
    stbir__simdf8_0123to22223333( c, cs );         \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+8 );

#define stbir__4_coeff_continue_from_4( ofs )                  \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf8_load4b( cs, hc + (ofs) );                    \
    stbir__simdf8_0123to00001111( c, cs );                     \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*4 );   \
    stbir__simdf8_0123to22223333( c, cs );                     \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*4+8 );

#define stbir__1_coeff_remnant( ofs )                          \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf_load1rep4( t, hc + (ofs) );                   \
    stbir__simdf8_madd_mem4( tot0, tot0, t, decode+(ofs)*4 );

#define stbir__2_coeff_remnant( ofs )                          \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf8_load4b( cs, hc + (ofs) - 2 );                \
    stbir__simdf8_0123to22223333( c, cs );                     \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*4 );

 #define stbir__3_coeff_remnant( ofs )                         \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf8_load4b( cs, hc + (ofs) );                    \
    stbir__simdf8_0123to00001111( c, cs );                     \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*4 );   \
    stbir__simdf8_0123to2222( t, cs );                         \
    stbir__simdf8_madd_mem4( tot0, tot0, t, decode+(ofs)*4+8 );

#define stbir__store_output()                      \
    stbir__simdf8_add4halves( t, stbir__if_simdf8_cast_to_simdf4(tot0), tot0 );     \
    stbir__simdf_store( output, t );               \
    horizontal_coefficients += coefficient_width;  \
    ++horizontal_contributors;                     \
    output += 4;

#else

#define stbir__4_coeff_start()                        \
    stbir__simdf tot0,tot1,c,cs;                      \
    STBIR_SIMD_NO_UNROLL(decode);                     \
    stbir__simdf_load( cs, hc );                      \
    stbir__simdf_0123to0000( c, cs );                 \
    stbir__simdf_mult_mem( tot0, c, decode );         \
    stbir__simdf_0123to1111( c, cs );                 \
    stbir__simdf_mult_mem( tot1, c, decode+4 );       \
    stbir__simdf_0123to2222( c, cs );                 \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+8 ); \
    stbir__simdf_0123to3333( c, cs );                 \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+12 );

#define stbir__4_coeff_continue_from_4( ofs )                  \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf_load( cs, hc + (ofs) );                       \
    stbir__simdf_0123to0000( c, cs );                          \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*4 );    \
    stbir__simdf_0123to1111( c, cs );                          \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*4+4 );  \
    stbir__simdf_0123to2222( c, cs );                          \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*4+8 );  \
    stbir__simdf_0123to3333( c, cs );                          \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*4+12 );

#define stbir__1_coeff_remnant( ofs )                       \
    STBIR_SIMD_NO_UNROLL(decode);                           \
    stbir__simdf_load1( c, hc + (ofs) );                    \
    stbir__simdf_0123to0000( c, c );                        \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*4 );

#define stbir__2_coeff_remnant( ofs )                         \
    STBIR_SIMD_NO_UNROLL(decode);                             \
    stbir__simdf_load2( cs, hc + (ofs) );                     \
    stbir__simdf_0123to0000( c, cs );                         \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*4 );   \
    stbir__simdf_0123to1111( c, cs );                         \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*4+4 );

#define stbir__3_coeff_remnant( ofs )                          \
    STBIR_SIMD_NO_UNROLL(decode);                              \
    stbir__simdf_load( cs, hc + (ofs) );                       \
    stbir__simdf_0123to0000( c, cs );                          \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*4 );    \
    stbir__simdf_0123to1111( c, cs );                          \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*4+4 );  \
    stbir__simdf_0123to2222( c, cs );                          \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*4+8 );

#define stbir__store_output()                     \
    stbir__simdf_add( tot0, tot0, tot1 );         \
    stbir__simdf_store( output, tot0 );           \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 4;

#endif

#else

#define stbir__1_coeff_only()         \
    float p0,p1,p2,p3,c;              \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    p0 = decode[0] * c;               \
    p1 = decode[1] * c;               \
    p2 = decode[2] * c;               \
    p3 = decode[3] * c;

#define stbir__2_coeff_only()         \
    float p0,p1,p2,p3,c;              \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    p0 = decode[0] * c;               \
    p1 = decode[1] * c;               \
    p2 = decode[2] * c;               \
    p3 = decode[3] * c;               \
    c = hc[1];                        \
    p0 += decode[4] * c;              \
    p1 += decode[5] * c;              \
    p2 += decode[6] * c;              \
    p3 += decode[7] * c;

#define stbir__3_coeff_only()         \
    float p0,p1,p2,p3,c;              \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    p0 = decode[0] * c;               \
    p1 = decode[1] * c;               \
    p2 = decode[2] * c;               \
    p3 = decode[3] * c;               \
    c = hc[1];                        \
    p0 += decode[4] * c;              \
    p1 += decode[5] * c;              \
    p2 += decode[6] * c;              \
    p3 += decode[7] * c;              \
    c = hc[2];                        \
    p0 += decode[8] * c;              \
    p1 += decode[9] * c;              \
    p2 += decode[10] * c;             \
    p3 += decode[11] * c;

#define stbir__store_output_tiny()                \
    output[0] = p0;                               \
    output[1] = p1;                               \
    output[2] = p2;                               \
    output[3] = p3;                               \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 4;

#define stbir__4_coeff_start()        \
    float x0,x1,x2,x3,y0,y1,y2,y3,c;  \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0];                        \
    x0 = decode[0] * c;               \
    x1 = decode[1] * c;               \
    x2 = decode[2] * c;               \
    x3 = decode[3] * c;               \
    c = hc[1];                        \
    y0 = decode[4] * c;               \
    y1 = decode[5] * c;               \
    y2 = decode[6] * c;               \
    y3 = decode[7] * c;               \
    c = hc[2];                        \
    x0 += decode[8] * c;              \
    x1 += decode[9] * c;              \
    x2 += decode[10] * c;             \
    x3 += decode[11] * c;             \
    c = hc[3];                        \
    y0 += decode[12] * c;             \
    y1 += decode[13] * c;             \
    y2 += decode[14] * c;             \
    y3 += decode[15] * c;

#define stbir__4_coeff_continue_from_4( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;      \
    c = hc[1+(ofs)];                  \
    y0 += decode[4+(ofs)*4] * c;      \
    y1 += decode[5+(ofs)*4] * c;      \
    y2 += decode[6+(ofs)*4] * c;      \
    y3 += decode[7+(ofs)*4] * c;      \
    c = hc[2+(ofs)];                  \
    x0 += decode[8+(ofs)*4] * c;      \
    x1 += decode[9+(ofs)*4] * c;      \
    x2 += decode[10+(ofs)*4] * c;     \
    x3 += decode[11+(ofs)*4] * c;     \
    c = hc[3+(ofs)];                  \
    y0 += decode[12+(ofs)*4] * c;     \
    y1 += decode[13+(ofs)*4] * c;     \
    y2 += decode[14+(ofs)*4] * c;     \
    y3 += decode[15+(ofs)*4] * c;

#define stbir__1_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;

#define stbir__2_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;      \
    c = hc[1+(ofs)];                  \
    y0 += decode[4+(ofs)*4] * c;      \
    y1 += decode[5+(ofs)*4] * c;      \
    y2 += decode[6+(ofs)*4] * c;      \
    y3 += decode[7+(ofs)*4] * c;

#define stbir__3_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);     \
    c = hc[0+(ofs)];                  \
    x0 += decode[0+(ofs)*4] * c;      \
    x1 += decode[1+(ofs)*4] * c;      \
    x2 += decode[2+(ofs)*4] * c;      \
    x3 += decode[3+(ofs)*4] * c;      \
    c = hc[1+(ofs)];                  \
    y0 += decode[4+(ofs)*4] * c;      \
    y1 += decode[5+(ofs)*4] * c;      \
    y2 += decode[6+(ofs)*4] * c;      \
    y3 += decode[7+(ofs)*4] * c;      \
    c = hc[2+(ofs)];                  \
    x0 += decode[8+(ofs)*4] * c;      \
    x1 += decode[9+(ofs)*4] * c;      \
    x2 += decode[10+(ofs)*4] * c;     \
    x3 += decode[11+(ofs)*4] * c;

#define stbir__store_output()                     \
    output[0] = x0 + y0;                          \
    output[1] = x1 + y1;                          \
    output[2] = x2 + y2;                          \
    output[3] = x3 + y3;                          \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 4;

#endif

#define STBIR__horizontal_channels 4
#include "image/resize/horizontals.inl"



//=================
// Do 7 channel horizontal routines

#ifdef STBIR_SIMD

#define stbir__1_coeff_only()                   \
    stbir__simdf tot0,tot1,c;                   \
    STBIR_SIMD_NO_UNROLL(decode);               \
    stbir__simdf_load1( c, hc );                \
    stbir__simdf_0123to0000( c, c );            \
    stbir__simdf_mult_mem( tot0, c, decode );   \
    stbir__simdf_mult_mem( tot1, c, decode+3 );

#define stbir__2_coeff_only()                         \
    stbir__simdf tot0,tot1,c,cs;                      \
    STBIR_SIMD_NO_UNROLL(decode);                     \
    stbir__simdf_load2( cs, hc );                     \
    stbir__simdf_0123to0000( c, cs );                 \
    stbir__simdf_mult_mem( tot0, c, decode );         \
    stbir__simdf_mult_mem( tot1, c, decode+3 );       \
    stbir__simdf_0123to1111( c, cs );                 \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+7 ); \
    stbir__simdf_madd_mem( tot1, tot1, c,decode+10 );

#define stbir__3_coeff_only()                           \
    stbir__simdf tot0,tot1,c,cs;                        \
    STBIR_SIMD_NO_UNROLL(decode);                       \
    stbir__simdf_load( cs, hc );                        \
    stbir__simdf_0123to0000( c, cs );                   \
    stbir__simdf_mult_mem( tot0, c, decode );           \
    stbir__simdf_mult_mem( tot1, c, decode+3 );         \
    stbir__simdf_0123to1111( c, cs );                   \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+7 );   \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+10 );  \
    stbir__simdf_0123to2222( c, cs );                   \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+14 );  \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+17 );

#define stbir__store_output_tiny()                \
    stbir__simdf_store( output+3, tot1 );         \
    stbir__simdf_store( output, tot0 );           \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 7;

#ifdef STBIR_SIMD8

#define stbir__4_coeff_start()                     \
    stbir__simdf8 tot0,tot1,c,cs;                  \
    STBIR_SIMD_NO_UNROLL(decode);                  \
    stbir__simdf8_load4b( cs, hc );                \
    stbir__simdf8_0123to00000000( c, cs );         \
    stbir__simdf8_mult_mem( tot0, c, decode );     \
    stbir__simdf8_0123to11111111( c, cs );         \
    stbir__simdf8_mult_mem( tot1, c, decode+7 );   \
    stbir__simdf8_0123to22222222( c, cs );         \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+14 );  \
    stbir__simdf8_0123to33333333( c, cs );         \
    stbir__simdf8_madd_mem( tot1, tot1, c, decode+21 );

#define stbir__4_coeff_continue_from_4( ofs )                   \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf8_load4b( cs, hc + (ofs) );                     \
    stbir__simdf8_0123to00000000( c, cs );                      \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*7 );    \
    stbir__simdf8_0123to11111111( c, cs );                      \
    stbir__simdf8_madd_mem( tot1, tot1, c, decode+(ofs)*7+7 );  \
    stbir__simdf8_0123to22222222( c, cs );                      \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*7+14 ); \
    stbir__simdf8_0123to33333333( c, cs );                      \
    stbir__simdf8_madd_mem( tot1, tot1, c, decode+(ofs)*7+21 );

#define stbir__1_coeff_remnant( ofs )                           \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf8_load1b( c, hc + (ofs) );                      \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*7 );

#define stbir__2_coeff_remnant( ofs )                           \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf8_load1b( c, hc + (ofs) );                      \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*7 );    \
    stbir__simdf8_load1b( c, hc + (ofs)+1 );                    \
    stbir__simdf8_madd_mem( tot1, tot1, c, decode+(ofs)*7+7 );

#define stbir__3_coeff_remnant( ofs )                           \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf8_load4b( cs, hc + (ofs) );                     \
    stbir__simdf8_0123to00000000( c, cs );                      \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*7 );    \
    stbir__simdf8_0123to11111111( c, cs );                      \
    stbir__simdf8_madd_mem( tot1, tot1, c, decode+(ofs)*7+7 );  \
    stbir__simdf8_0123to22222222( c, cs );                      \
    stbir__simdf8_madd_mem( tot0, tot0, c, decode+(ofs)*7+14 );

#define stbir__store_output()                     \
    stbir__simdf8_add( tot0, tot0, tot1 );        \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 7;                                  \
    if ( output < output_end )                    \
    {                                             \
      stbir__simdf8_store( output-7, tot0 );      \
      continue;                                   \
    }                                             \
    stbir__simdf_store( output-7+3, stbir__simdf_swiz(stbir__simdf8_gettop4(tot0),0,0,1,2) ); \
    stbir__simdf_store( output-7, stbir__if_simdf8_cast_to_simdf4(tot0) );           \
    break;

#else

#define stbir__4_coeff_start()                    \
    stbir__simdf tot0,tot1,tot2,tot3,c,cs;        \
    STBIR_SIMD_NO_UNROLL(decode);                 \
    stbir__simdf_load( cs, hc );                  \
    stbir__simdf_0123to0000( c, cs );             \
    stbir__simdf_mult_mem( tot0, c, decode );     \
    stbir__simdf_mult_mem( tot1, c, decode+3 );   \
    stbir__simdf_0123to1111( c, cs );             \
    stbir__simdf_mult_mem( tot2, c, decode+7 );   \
    stbir__simdf_mult_mem( tot3, c, decode+10 );  \
    stbir__simdf_0123to2222( c, cs );             \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+14 );  \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+17 );  \
    stbir__simdf_0123to3333( c, cs );                   \
    stbir__simdf_madd_mem( tot2, tot2, c, decode+21 );  \
    stbir__simdf_madd_mem( tot3, tot3, c, decode+24 );

#define stbir__4_coeff_continue_from_4( ofs )                   \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf_load( cs, hc + (ofs) );                        \
    stbir__simdf_0123to0000( c, cs );                           \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*7 );     \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*7+3 );   \
    stbir__simdf_0123to1111( c, cs );                           \
    stbir__simdf_madd_mem( tot2, tot2, c, decode+(ofs)*7+7 );   \
    stbir__simdf_madd_mem( tot3, tot3, c, decode+(ofs)*7+10 );  \
    stbir__simdf_0123to2222( c, cs );                           \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*7+14 );  \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*7+17 );  \
    stbir__simdf_0123to3333( c, cs );                           \
    stbir__simdf_madd_mem( tot2, tot2, c, decode+(ofs)*7+21 );  \
    stbir__simdf_madd_mem( tot3, tot3, c, decode+(ofs)*7+24 );

#define stbir__1_coeff_remnant( ofs )                           \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf_load1( c, hc + (ofs) );                        \
    stbir__simdf_0123to0000( c, c );                            \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*7 );     \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*7+3 );   \

#define stbir__2_coeff_remnant( ofs )                           \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf_load2( cs, hc + (ofs) );                       \
    stbir__simdf_0123to0000( c, cs );                           \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*7 );     \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*7+3 );   \
    stbir__simdf_0123to1111( c, cs );                           \
    stbir__simdf_madd_mem( tot2, tot2, c, decode+(ofs)*7+7 );   \
    stbir__simdf_madd_mem( tot3, tot3, c, decode+(ofs)*7+10 );

#define stbir__3_coeff_remnant( ofs )                           \
    STBIR_SIMD_NO_UNROLL(decode);                               \
    stbir__simdf_load( cs, hc + (ofs) );                        \
    stbir__simdf_0123to0000( c, cs );                           \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*7 );     \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*7+3 );   \
    stbir__simdf_0123to1111( c, cs );                           \
    stbir__simdf_madd_mem( tot2, tot2, c, decode+(ofs)*7+7 );   \
    stbir__simdf_madd_mem( tot3, tot3, c, decode+(ofs)*7+10 );  \
    stbir__simdf_0123to2222( c, cs );                           \
    stbir__simdf_madd_mem( tot0, tot0, c, decode+(ofs)*7+14 );  \
    stbir__simdf_madd_mem( tot1, tot1, c, decode+(ofs)*7+17 );

#define stbir__store_output()                     \
    stbir__simdf_add( tot0, tot0, tot2 );         \
    stbir__simdf_add( tot1, tot1, tot3 );         \
    stbir__simdf_store( output+3, tot1 );         \
    stbir__simdf_store( output, tot0 );           \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 7;

#endif

#else

#define stbir__1_coeff_only()        \
    float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
    c = hc[0];                       \
    tot0 = decode[0]*c;              \
    tot1 = decode[1]*c;              \
    tot2 = decode[2]*c;              \
    tot3 = decode[3]*c;              \
    tot4 = decode[4]*c;              \
    tot5 = decode[5]*c;              \
    tot6 = decode[6]*c;

#define stbir__2_coeff_only()        \
    float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
    c = hc[0];                       \
    tot0 = decode[0]*c;              \
    tot1 = decode[1]*c;              \
    tot2 = decode[2]*c;              \
    tot3 = decode[3]*c;              \
    tot4 = decode[4]*c;              \
    tot5 = decode[5]*c;              \
    tot6 = decode[6]*c;              \
    c = hc[1];                       \
    tot0 += decode[7]*c;             \
    tot1 += decode[8]*c;             \
    tot2 += decode[9]*c;             \
    tot3 += decode[10]*c;            \
    tot4 += decode[11]*c;            \
    tot5 += decode[12]*c;            \
    tot6 += decode[13]*c;            \

#define stbir__3_coeff_only()        \
    float tot0, tot1, tot2, tot3, tot4, tot5, tot6, c; \
    c = hc[0];                       \
    tot0 = decode[0]*c;              \
    tot1 = decode[1]*c;              \
    tot2 = decode[2]*c;              \
    tot3 = decode[3]*c;              \
    tot4 = decode[4]*c;              \
    tot5 = decode[5]*c;              \
    tot6 = decode[6]*c;              \
    c = hc[1];                       \
    tot0 += decode[7]*c;             \
    tot1 += decode[8]*c;             \
    tot2 += decode[9]*c;             \
    tot3 += decode[10]*c;            \
    tot4 += decode[11]*c;            \
    tot5 += decode[12]*c;            \
    tot6 += decode[13]*c;            \
    c = hc[2];                       \
    tot0 += decode[14]*c;            \
    tot1 += decode[15]*c;            \
    tot2 += decode[16]*c;            \
    tot3 += decode[17]*c;            \
    tot4 += decode[18]*c;            \
    tot5 += decode[19]*c;            \
    tot6 += decode[20]*c;            \

#define stbir__store_output_tiny()                \
    output[0] = tot0;                             \
    output[1] = tot1;                             \
    output[2] = tot2;                             \
    output[3] = tot3;                             \
    output[4] = tot4;                             \
    output[5] = tot5;                             \
    output[6] = tot6;                             \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 7;

#define stbir__4_coeff_start()    \
    float x0,x1,x2,x3,x4,x5,x6,y0,y1,y2,y3,y4,y5,y6,c; \
    STBIR_SIMD_NO_UNROLL(decode); \
    c = hc[0];                    \
    x0 = decode[0] * c;           \
    x1 = decode[1] * c;           \
    x2 = decode[2] * c;           \
    x3 = decode[3] * c;           \
    x4 = decode[4] * c;           \
    x5 = decode[5] * c;           \
    x6 = decode[6] * c;           \
    c = hc[1];                    \
    y0 = decode[7] * c;           \
    y1 = decode[8] * c;           \
    y2 = decode[9] * c;           \
    y3 = decode[10] * c;          \
    y4 = decode[11] * c;          \
    y5 = decode[12] * c;          \
    y6 = decode[13] * c;          \
    c = hc[2];                    \
    x0 += decode[14] * c;         \
    x1 += decode[15] * c;         \
    x2 += decode[16] * c;         \
    x3 += decode[17] * c;         \
    x4 += decode[18] * c;         \
    x5 += decode[19] * c;         \
    x6 += decode[20] * c;         \
    c = hc[3];                    \
    y0 += decode[21] * c;         \
    y1 += decode[22] * c;         \
    y2 += decode[23] * c;         \
    y3 += decode[24] * c;         \
    y4 += decode[25] * c;         \
    y5 += decode[26] * c;         \
    y6 += decode[27] * c;

#define stbir__4_coeff_continue_from_4( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \
    c = hc[1+(ofs)];               \
    y0 += decode[7+(ofs)*7] * c;   \
    y1 += decode[8+(ofs)*7] * c;   \
    y2 += decode[9+(ofs)*7] * c;   \
    y3 += decode[10+(ofs)*7] * c;  \
    y4 += decode[11+(ofs)*7] * c;  \
    y5 += decode[12+(ofs)*7] * c;  \
    y6 += decode[13+(ofs)*7] * c;  \
    c = hc[2+(ofs)];               \
    x0 += decode[14+(ofs)*7] * c;  \
    x1 += decode[15+(ofs)*7] * c;  \
    x2 += decode[16+(ofs)*7] * c;  \
    x3 += decode[17+(ofs)*7] * c;  \
    x4 += decode[18+(ofs)*7] * c;  \
    x5 += decode[19+(ofs)*7] * c;  \
    x6 += decode[20+(ofs)*7] * c;  \
    c = hc[3+(ofs)];               \
    y0 += decode[21+(ofs)*7] * c;  \
    y1 += decode[22+(ofs)*7] * c;  \
    y2 += decode[23+(ofs)*7] * c;  \
    y3 += decode[24+(ofs)*7] * c;  \
    y4 += decode[25+(ofs)*7] * c;  \
    y5 += decode[26+(ofs)*7] * c;  \
    y6 += decode[27+(ofs)*7] * c;

#define stbir__1_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \

#define stbir__2_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \
    c = hc[1+(ofs)];               \
    y0 += decode[7+(ofs)*7] * c;   \
    y1 += decode[8+(ofs)*7] * c;   \
    y2 += decode[9+(ofs)*7] * c;   \
    y3 += decode[10+(ofs)*7] * c;  \
    y4 += decode[11+(ofs)*7] * c;  \
    y5 += decode[12+(ofs)*7] * c;  \
    y6 += decode[13+(ofs)*7] * c;  \

#define stbir__3_coeff_remnant( ofs ) \
    STBIR_SIMD_NO_UNROLL(decode);  \
    c = hc[0+(ofs)];               \
    x0 += decode[0+(ofs)*7] * c;   \
    x1 += decode[1+(ofs)*7] * c;   \
    x2 += decode[2+(ofs)*7] * c;   \
    x3 += decode[3+(ofs)*7] * c;   \
    x4 += decode[4+(ofs)*7] * c;   \
    x5 += decode[5+(ofs)*7] * c;   \
    x6 += decode[6+(ofs)*7] * c;   \
    c = hc[1+(ofs)];               \
    y0 += decode[7+(ofs)*7] * c;   \
    y1 += decode[8+(ofs)*7] * c;   \
    y2 += decode[9+(ofs)*7] * c;   \
    y3 += decode[10+(ofs)*7] * c;  \
    y4 += decode[11+(ofs)*7] * c;  \
    y5 += decode[12+(ofs)*7] * c;  \
    y6 += decode[13+(ofs)*7] * c;  \
    c = hc[2+(ofs)];               \
    x0 += decode[14+(ofs)*7] * c;  \
    x1 += decode[15+(ofs)*7] * c;  \
    x2 += decode[16+(ofs)*7] * c;  \
    x3 += decode[17+(ofs)*7] * c;  \
    x4 += decode[18+(ofs)*7] * c;  \
    x5 += decode[19+(ofs)*7] * c;  \
    x6 += decode[20+(ofs)*7] * c;  \

#define stbir__store_output()                     \
    output[0] = x0 + y0;                          \
    output[1] = x1 + y1;                          \
    output[2] = x2 + y2;                          \
    output[3] = x3 + y3;                          \
    output[4] = x4 + y4;                          \
    output[5] = x5 + y5;                          \
    output[6] = x6 + y6;                          \
    horizontal_coefficients += coefficient_width; \
    ++horizontal_contributors;                    \
    output += 7;

#endif

#define STBIR__horizontal_channels 7
#include "image/resize/horizontals.inl"
