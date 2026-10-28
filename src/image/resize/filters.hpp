// The filter kernels (box, triangle, cubic, Catmull-Rom, Mitchell, point sampling) and the edge modes.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

static float stbir__filter_trapezoid(float x, float scale, void * user_data)
{
  float halfscale = scale / 2;
  float t = 0.5f + halfscale;
  STBIR_ASSERT(scale <= 1);
  STBIR__UNUSED(user_data);

  if ( x < 0.0f ) x = -x;

  if (x >= t)
    return 0.0f;
  else
  {
    float r = 0.5f - halfscale;
    if (x <= r)
      return 1.0f;
    else
      return (t - x) / scale;
  }
}

static float stbir__support_trapezoid(float scale, void * user_data)
{
  STBIR__UNUSED(user_data);
  return 0.5f + scale / 2.0f;
}

static float stbir__filter_triangle(float x, float s, void * user_data)
{
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);

  if ( x < 0.0f ) x = -x;

  if (x <= 1.0f)
    return 1.0f - x;
  else
    return 0.0f;
}

static float stbir__filter_point(float x, float s, void * user_data)
{
  STBIR__UNUSED(x);
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);

  return 1.0f;
}

static float stbir__filter_cubic(float x, float s, void * user_data)
{
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);

  if ( x < 0.0f ) x = -x;

  if (x < 1.0f)
    return (4.0f + x*x*(3.0f*x - 6.0f))/6.0f;
  else if (x < 2.0f)
    return (8.0f + x*(-12.0f + x*(6.0f - x)))/6.0f;

  return (0.0f);
}

static float stbir__filter_catmullrom(float x, float s, void * user_data)
{
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);

  if ( x < 0.0f ) x = -x;

  if (x < 1.0f)
    return 1.0f - x*x*(2.5f - 1.5f*x);
  else if (x < 2.0f)
    return 2.0f - x*(4.0f + x*(0.5f*x - 2.5f));

  return (0.0f);
}

static float stbir__filter_mitchell(float x, float s, void * user_data)
{
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);

  if ( x < 0.0f ) x = -x;

  if (x < 1.0f)
    return (16.0f + x*x*(21.0f * x - 36.0f))/18.0f;
  else if (x < 2.0f)
    return (32.0f + x*(-60.0f + x*(36.0f - 7.0f*x)))/18.0f;

  return (0.0f);
}

static float stbir__support_zeropoint5(float s, void * user_data)
{
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);
  return 0.5f;
}

static float stbir__support_one(float s, void * user_data)
{
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);
  return 1;
}

static float stbir__support_two(float s, void * user_data)
{
  STBIR__UNUSED(s);
  STBIR__UNUSED(user_data);
  return 2;
}

// This is the maximum number of input samples that can affect an output sample
// with the given filter from the output pixel's perspective
static int stbir__get_filter_pixel_width(stbir__support_callback * support, float scale, void * user_data)
{
  STBIR_ASSERT(support != 0);

  if ( scale >= ( 1.0f-stbir__small_float ) ) // upscale
    return (int)STBIR_CEILF(support(1.0f/scale,user_data) * 2.0f);
  else
    return (int)STBIR_CEILF(support(scale,user_data) * 2.0f / scale);
}

// this is how many coefficents per run of the filter (which is different
//   from the filter_pixel_width depending on if we are scattering or gathering)
static int stbir__get_coefficient_width(stbir__sampler * samp, int is_gather, void * user_data)
{
  float scale = samp->scale_info.scale;
  stbir__support_callback * support = samp->filter_support;

  switch( is_gather )
  {
    case 1:
      return (int)STBIR_CEILF(support(1.0f / scale, user_data) * 2.0f);
    case 2:
      return (int)STBIR_CEILF(support(scale, user_data) * 2.0f / scale);
    case 0:
      return (int)STBIR_CEILF(support(scale, user_data) * 2.0f);
    default:
      STBIR_ASSERT( (is_gather >= 0 ) && (is_gather <= 2 ) );
      return 0;
  }
}

static int stbir__get_contributors(stbir__sampler * samp, int is_gather)
{
  if (is_gather)
      return samp->scale_info.output_sub_size;
  else
      return (samp->scale_info.input_full_size + samp->filter_pixel_margin * 2);
}

static int stbir__edge_zero_full( int n, int max )
{
  STBIR__UNUSED(n);
  STBIR__UNUSED(max);
  return 0; // NOTREACHED
}

static int stbir__edge_clamp_full( int n, int max )
{
  if (n < 0)
    return 0;

  if (n >= max)
    return max - 1;

  return n; // NOTREACHED
}

static int stbir__edge_reflect_full( int n, int max )
{
  if (n < 0)
  {
    if (n > -max)
      return -n;
    else
      return max - 1;
  }

  if (n >= max)
  {
    int max2 = max * 2;
    if (n >= max2)
      return 0;
    else
      return max2 - n - 1;
  }

  return n; // NOTREACHED
}

static int stbir__edge_wrap_full( int n, int max )
{
  if (n >= 0)
    return (n % max);
  else
  {
    int m = (-n) % max;

    if (m != 0)
      m = max - m;

    return (m);
  }
}

typedef int stbir__edge_wrap_func( int n, int max );
static stbir__edge_wrap_func * stbir__edge_wrap_slow[] =
{
  stbir__edge_clamp_full,    // STBIR_EDGE_CLAMP
  stbir__edge_reflect_full,  // STBIR_EDGE_REFLECT
  stbir__edge_wrap_full,     // STBIR_EDGE_WRAP
  stbir__edge_zero_full,     // STBIR_EDGE_ZERO
};

stbir__inline static int stbir__edge_wrap(stbir_edge edge, int n, int max)
{
  // avoid per-pixel switch
  if (n >= 0 && n < max)
      return n;
  return stbir__edge_wrap_slow[edge]( n, max );
}

#define STBIR__MERGE_RUNS_PIXEL_THRESHOLD 16

// get information on the extents of a sampler
