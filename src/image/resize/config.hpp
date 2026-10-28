// Configuration defaults, the internal enums and structs (sampler, info, extents), min / max.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).


#ifndef STBIR_ASSERT
#include <assert.h>
#define STBIR_ASSERT(x) assert(x)
#endif

#ifndef STBIR_MALLOC
#include <stdlib.h>
#define STBIR_MALLOC(size,user_data) ((void)(user_data), malloc(size))
#define STBIR_FREE(ptr,user_data)    ((void)(user_data), free(ptr))
// (we used the comma operator to evaluate user_data, to avoid "unused parameter" warnings)
#endif

#ifdef _MSC_VER

#define stbir__inline __forceinline

#else

#define stbir__inline __inline__

// Clang address sanitizer
#if defined(__has_feature)
  #if __has_feature(address_sanitizer) || __has_feature(memory_sanitizer)
    #ifndef STBIR__SEPARATE_ALLOCATIONS
      #define STBIR__SEPARATE_ALLOCATIONS
    #endif
  #endif
#endif

#endif

// GCC and MSVC
#if defined(__SANITIZE_ADDRESS__)
  #ifndef STBIR__SEPARATE_ALLOCATIONS
    #define STBIR__SEPARATE_ALLOCATIONS
  #endif
#endif

// Always turn off automatic FMA use - use STBIR_USE_FMA if you want.
// Otherwise, this is a determinism disaster.
#ifndef STBIR_DONT_CHANGE_FP_CONTRACT  // override in case you don't want this behavior
#if defined(_MSC_VER) && !defined(__clang__)
#if _MSC_VER > 1200
#pragma fp_contract(off)
#endif
#elif defined(__GNUC__) &&  !defined(__clang__)
#pragma GCC optimize("fp-contract=off")
#else
#pragma STDC FP_CONTRACT OFF
#endif
#endif

#ifdef _MSC_VER
#define STBIR__UNUSED(v)  (void)(v)
#else
#define STBIR__UNUSED(v)  (void)sizeof(v)
#endif

#define STBIR__ARRAY_SIZE(a) (sizeof((a))/sizeof((a)[0]))


#ifndef STBIR_DEFAULT_FILTER_UPSAMPLE
#define STBIR_DEFAULT_FILTER_UPSAMPLE    STBIR_FILTER_CATMULLROM
#endif

#ifndef STBIR_DEFAULT_FILTER_DOWNSAMPLE
#define STBIR_DEFAULT_FILTER_DOWNSAMPLE  STBIR_FILTER_MITCHELL
#endif


#ifndef STBIR__HEADER_FILENAME
#define STBIR__HEADER_FILENAME "stb_image_resize2.h"
#endif

// the internal pixel layout enums are in a different order, so we can easily do range comparisons of types
//   the public pixel layout is ordered in a way that if you cast num_channels (1-4) to the enum, you get something sensible
typedef enum
{
  STBIRI_1CHANNEL = 0,
  STBIRI_2CHANNEL = 1,
  STBIRI_RGB      = 2,
  STBIRI_BGR      = 3,
  STBIRI_4CHANNEL = 4,

  STBIRI_RGBA = 5,
  STBIRI_BGRA = 6,
  STBIRI_ARGB = 7,
  STBIRI_ABGR = 8,
  STBIRI_RA   = 9,
  STBIRI_AR   = 10,

  STBIRI_RGBA_PM = 11,
  STBIRI_BGRA_PM = 12,
  STBIRI_ARGB_PM = 13,
  STBIRI_ABGR_PM = 14,
  STBIRI_RA_PM   = 15,
  STBIRI_AR_PM   = 16,
} stbir_internal_pixel_layout;

// define the public pixel layouts to not compile inside the implementation (to avoid accidental use)
#define STBIR_BGR bad_dont_use_in_implementation
#define STBIR_1CHANNEL STBIR_BGR
#define STBIR_2CHANNEL STBIR_BGR
#define STBIR_RGB STBIR_BGR
#define STBIR_RGBA STBIR_BGR
#define STBIR_4CHANNEL STBIR_BGR
#define STBIR_BGRA STBIR_BGR
#define STBIR_ARGB STBIR_BGR
#define STBIR_ABGR STBIR_BGR
#define STBIR_RA STBIR_BGR
#define STBIR_AR STBIR_BGR
#define STBIR_RGBA_PM STBIR_BGR
#define STBIR_BGRA_PM STBIR_BGR
#define STBIR_ARGB_PM STBIR_BGR
#define STBIR_ABGR_PM STBIR_BGR
#define STBIR_RA_PM STBIR_BGR
#define STBIR_AR_PM STBIR_BGR

// must match stbir_datatype
static unsigned char stbir__type_size[] = {
  1,1,1,2,4,2 // STBIR_TYPE_UINT8,STBIR_TYPE_UINT8_SRGB,STBIR_TYPE_UINT8_SRGB_ALPHA,STBIR_TYPE_UINT16,STBIR_TYPE_FLOAT,STBIR_TYPE_HALF_FLOAT
};

// When gathering, the contributors are which source pixels contribute.
// When scattering, the contributors are which destination pixels are contributed to.
typedef struct
{
  int n0; // First contributing pixel
  int n1; // Last contributing pixel
} stbir__contributors;

typedef struct
{
  int lowest;    // First sample index for whole filter
  int highest;   // Last sample index for whole filter
  int widest;    // widest single set of samples for an output
} stbir__filter_extent_info;

typedef struct
{
  int n0; // First pixel of decode buffer to write to
  int n1; // Last pixel of decode that will be written to
  int pixel_offset_for_input;  // Pixel offset into input_scanline
} stbir__span;

typedef struct stbir__scale_info
{
  int input_full_size;
  int output_sub_size;
  float scale;
  float inv_scale;
  float pixel_shift; // starting shift in output pixel space (in pixels)
  int scale_is_rational;
  stbir_uint32 scale_numerator, scale_denominator;
} stbir__scale_info;

typedef struct
{
  stbir__contributors * contributors;
  float* coefficients;
  stbir__contributors * gather_prescatter_contributors;
  float * gather_prescatter_coefficients;
  stbir__scale_info scale_info;
  float support;
  stbir_filter filter_enum;
  stbir__kernel_callback * filter_kernel;
  stbir__support_callback * filter_support;
  stbir_edge edge;
  int coefficient_width;
  int filter_pixel_width;
  int filter_pixel_margin;
  int num_contributors;
  int contributors_size;
  int coefficients_size;
  stbir__filter_extent_info extent_info;
  int is_gather;  // 0 = scatter, 1 = gather with scale >= 1, 2 = gather with scale < 1
  int gather_prescatter_num_contributors;
  int gather_prescatter_coefficient_width;
  int gather_prescatter_contributors_size;
  int gather_prescatter_coefficients_size;
} stbir__sampler;

typedef struct
{
  stbir__contributors conservative;
  int edge_sizes[2];    // this can be less than filter_pixel_margin, if the filter and scaling falls off
  stbir__span spans[2]; // can be two spans, if doing input subrect with clamp mode WRAP
} stbir__extents;

typedef struct
{
#ifdef STBIR_PROFILE
  union
  {
    struct { stbir_uint64 total, looping, vertical, horizontal, decode, encode, alpha, unalpha; } named;
    stbir_uint64 array[8];
  } profile;
  stbir_uint64 * current_zone_excluded_ptr;
#endif
  float* decode_buffer;

  int ring_buffer_first_scanline;
  int ring_buffer_last_scanline;
  int ring_buffer_begin_index;    // first_scanline is at this index in the ring buffer
  int start_output_y, end_output_y;
  int start_input_y, end_input_y;  // used in scatter only

  #ifdef STBIR__SEPARATE_ALLOCATIONS
    float** ring_buffers; // one pointer for each ring buffer
  #else
    float* ring_buffer;  // one big buffer that we index into
  #endif

  float* vertical_buffer;

  char no_cache_straddle[64];
} stbir__per_split_info;

typedef float * stbir__decode_pixels_func( float * decode, int width_times_channels, void const * input );
typedef void stbir__alpha_weight_func( float * decode_buffer, int width_times_channels );
typedef void stbir__horizontal_gather_channels_func( float * output_buffer, unsigned int output_sub_size, float const * decode_buffer,
  stbir__contributors const * horizontal_contributors, float const * horizontal_coefficients, int coefficient_width );
typedef void stbir__alpha_unweight_func(float * encode_buffer, int width_times_channels );
typedef void stbir__encode_pixels_func( void * output, int width_times_channels, float const * encode );

struct stbir__info
{
#ifdef STBIR_PROFILE
  union
  {
    struct { stbir_uint64 total, build, alloc, horizontal, vertical, cleanup, pivot; } named;
    stbir_uint64 array[7];
  } profile;
  stbir_uint64 * current_zone_excluded_ptr;
#endif
  stbir__sampler horizontal;
  stbir__sampler vertical;

  void const * input_data;
  void * output_data;

  int input_stride_bytes;
  int output_stride_bytes;
  int ring_buffer_length_bytes;   // The length of an individual entry in the ring buffer. The total number of ring buffers is stbir__get_filter_pixel_width(filter)
  int ring_buffer_num_entries;    // Total number of entries in the ring buffer.

  stbir_datatype input_type;
  stbir_datatype output_type;

  stbir_input_callback * in_pixels_cb;
  void * user_data;
  stbir_output_callback * out_pixels_cb;

  stbir__extents scanline_extents;

  void * alloced_mem;
  stbir__per_split_info * split_info;  // by default 1, but there will be N of these allocated based on the thread init you did

  stbir__decode_pixels_func * decode_pixels;
  stbir__alpha_weight_func * alpha_weight;
  stbir__horizontal_gather_channels_func * horizontal_gather_channels;
  stbir__alpha_unweight_func * alpha_unweight;
  stbir__encode_pixels_func * encode_pixels;

  int alloc_ring_buffer_num_entries;    // Number of entries in the ring buffer that will be allocated
  int splits; // count of splits

  stbir_internal_pixel_layout input_pixel_layout_internal;
  stbir_internal_pixel_layout output_pixel_layout_internal;

  int input_color_and_type;
  int offset_x, offset_y; // offset within output_data
  int vertical_first;
  int channels;
  int effective_channels; // same as channels, except on RGBA/ARGB (7), or XA/AX (3)
  size_t alloced_total;
};


#define stbir__max_uint8_as_float             255.0f
#define stbir__max_uint16_as_float            65535.0f
#define stbir__max_uint8_as_float_inverted    3.9215689e-03f     // (1.0f/255.0f)
#define stbir__max_uint16_as_float_inverted   1.5259022e-05f     // (1.0f/65535.0f)
#define stbir__small_float ((float)1 / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20) / (1 << 20))

// min/max friendly
#define STBIR_CLAMP(x, xmin, xmax) for(;;) { \
  if ( (x) < (xmin) ) (x) = (xmin);     \
  if ( (x) > (xmax) ) (x) = (xmax);     \
  break;                                \
}

static stbir__inline int stbir__min(int a, int b)
{
  return a < b ? a : b;
}

static stbir__inline int stbir__max(int a, int b)
{
  return a > b ? a : b;
}


#define STBIR_strs_join2( start, mid, end ) start##mid##end
#define STBIR_strs_join1( start, mid, end ) STBIR_strs_join2( start, mid, end )

#define STBIR_strs_join24( start, mid1, mid2, end ) start##mid1##mid2##end
#define STBIR_strs_join14( start, mid1, mid2, end ) STBIR_strs_join24( start, mid1, mid2, end )
