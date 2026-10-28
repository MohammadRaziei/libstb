// The vertical resamplers (instances of verticals.inl) and their function tables.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

// include all of the vertical resamplers (both scatter and gather versions)

#define STBIR__vertical_channels 1
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 1
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 2
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 2
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 3
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 3
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 4
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 4
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 5
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 5
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 6
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 6
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 7
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 7
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 8
#include "image/resize/verticals.inl"

#define STBIR__vertical_channels 8
#define STB_IMAGE_RESIZE_VERTICAL_CONTINUE
#include "image/resize/verticals.inl"

typedef void STBIR_VERTICAL_GATHERFUNC( float * output, float const * coeffs, float const ** inputs, float const * input0_end );

static STBIR_VERTICAL_GATHERFUNC * stbir__vertical_gathers[ 8 ] =
{
  stbir__vertical_gather_with_1_coeffs,stbir__vertical_gather_with_2_coeffs,stbir__vertical_gather_with_3_coeffs,stbir__vertical_gather_with_4_coeffs,stbir__vertical_gather_with_5_coeffs,stbir__vertical_gather_with_6_coeffs,stbir__vertical_gather_with_7_coeffs,stbir__vertical_gather_with_8_coeffs
};

static STBIR_VERTICAL_GATHERFUNC * stbir__vertical_gathers_continues[ 8 ] =
{
  stbir__vertical_gather_with_1_coeffs_cont,stbir__vertical_gather_with_2_coeffs_cont,stbir__vertical_gather_with_3_coeffs_cont,stbir__vertical_gather_with_4_coeffs_cont,stbir__vertical_gather_with_5_coeffs_cont,stbir__vertical_gather_with_6_coeffs_cont,stbir__vertical_gather_with_7_coeffs_cont,stbir__vertical_gather_with_8_coeffs_cont
};

typedef void STBIR_VERTICAL_SCATTERFUNC( float ** outputs, float const * coeffs, float const * input, float const * input_end );

static STBIR_VERTICAL_SCATTERFUNC * stbir__vertical_scatter_sets[ 8 ] =
{
  stbir__vertical_scatter_with_1_coeffs,stbir__vertical_scatter_with_2_coeffs,stbir__vertical_scatter_with_3_coeffs,stbir__vertical_scatter_with_4_coeffs,stbir__vertical_scatter_with_5_coeffs,stbir__vertical_scatter_with_6_coeffs,stbir__vertical_scatter_with_7_coeffs,stbir__vertical_scatter_with_8_coeffs
};

static STBIR_VERTICAL_SCATTERFUNC * stbir__vertical_scatter_blends[ 8 ] =
{
  stbir__vertical_scatter_with_1_coeffs_cont,stbir__vertical_scatter_with_2_coeffs_cont,stbir__vertical_scatter_with_3_coeffs_cont,stbir__vertical_scatter_with_4_coeffs_cont,stbir__vertical_scatter_with_5_coeffs_cont,stbir__vertical_scatter_with_6_coeffs_cont,stbir__vertical_scatter_with_7_coeffs_cont,stbir__vertical_scatter_with_8_coeffs_cont
};
