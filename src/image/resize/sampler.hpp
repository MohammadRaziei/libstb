// Building the samplers (filter, edges, coefficients), split info, memory layout, vertical-first heuristic.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

static void stbir__set_sampler(stbir__sampler * samp, stbir_filter filter, stbir__kernel_callback * kernel, stbir__support_callback * support, stbir_edge edge, stbir__scale_info * scale_info, int always_gather, void * user_data )
{
  // set filter
  if (filter == 0)
  {
    filter = STBIR_DEFAULT_FILTER_DOWNSAMPLE; // default to downsample
    if (scale_info->scale >= ( 1.0f - stbir__small_float ) )
    {
      if ( (scale_info->scale <= ( 1.0f + stbir__small_float ) ) && ( STBIR_CEILF(scale_info->pixel_shift) == scale_info->pixel_shift ) )
        filter = STBIR_FILTER_POINT_SAMPLE;
      else
        filter = STBIR_DEFAULT_FILTER_UPSAMPLE;
    }
  }
  samp->filter_enum = filter;

  STBIR_ASSERT(samp->filter_enum != 0);
  STBIR_ASSERT((unsigned)samp->filter_enum < STBIR_FILTER_OTHER);
  samp->filter_kernel = stbir__builtin_kernels[ filter ];
  samp->filter_support = stbir__builtin_supports[ filter ];

  if ( kernel && support )
  {
    samp->filter_kernel = kernel;
    samp->filter_support = support;
    samp->filter_enum = STBIR_FILTER_OTHER;
  }

  samp->edge = edge;
  samp->filter_pixel_width  = stbir__get_filter_pixel_width (samp->filter_support, scale_info->scale, user_data );
  // Gather is always better, but in extreme downsamples, you have to most or all of the data in memory
  //    For horizontal, we always have all the pixels, so we always use gather here (always_gather==1).
  //    For vertical, we use gather if scaling up (which means we will have samp->filter_pixel_width
  //    scanlines in memory at once).
  samp->is_gather = 0;
  if ( scale_info->scale >= ( 1.0f - stbir__small_float ) )
    samp->is_gather = 1;
  else if ( ( always_gather ) || ( samp->filter_pixel_width <= STBIR_FORCE_GATHER_FILTER_SCANLINES_AMOUNT ) )
    samp->is_gather = 2;

  // pre calculate stuff based on the above
  samp->coefficient_width = stbir__get_coefficient_width(samp, samp->is_gather, user_data);

  // filter_pixel_width is the conservative size in pixels of input that affect an output pixel.
  //   In rare cases (only with 2 pix to 1 pix with the default filters), it's possible that the 
  //   filter will extend before or after the scanline beyond just one extra entire copy of the 
  //   scanline (we would hit the edge twice). We don't let you do that, so we clamp the total 
  //   width to 3x the total of input pixel (once for the scanline, once for the left side 
  //   overhang, and once for the right side). We only do this for edge mode, since the other 
  //   modes can just re-edge clamp back in again.
  if ( edge == STBIR_EDGE_WRAP )
    if ( samp->filter_pixel_width > ( scale_info->input_full_size * 3 ) )
      samp->filter_pixel_width = scale_info->input_full_size * 3;

  // This is how much to expand buffers to account for filters seeking outside
  // the image boundaries.
  samp->filter_pixel_margin = samp->filter_pixel_width / 2;
  
  // filter_pixel_margin is the amount that this filter can overhang on just one side of either 
  //   end of the scanline (left or the right). Since we only allow you to overhang 1 scanline's 
  //   worth of pixels, we clamp this one side of overhang to the input scanline size. Again, 
  //   this clamping only happens in rare cases with the default filters (2 pix to 1 pix). 
  if ( edge == STBIR_EDGE_WRAP )
    if ( samp->filter_pixel_margin > scale_info->input_full_size )
      samp->filter_pixel_margin = scale_info->input_full_size;

  samp->num_contributors = stbir__get_contributors(samp, samp->is_gather);

  samp->contributors_size = samp->num_contributors * sizeof(stbir__contributors);
  samp->coefficients_size = samp->num_contributors * samp->coefficient_width * sizeof(float) + sizeof(float)*STBIR_INPUT_CALLBACK_PADDING; // extra sizeof(float) is padding

  samp->gather_prescatter_contributors = 0;
  samp->gather_prescatter_coefficients = 0;
  if ( samp->is_gather == 0 )
  {
    samp->gather_prescatter_coefficient_width = samp->filter_pixel_width;
    samp->gather_prescatter_num_contributors  = stbir__get_contributors(samp, 2);
    samp->gather_prescatter_contributors_size = samp->gather_prescatter_num_contributors * sizeof(stbir__contributors);
    samp->gather_prescatter_coefficients_size = samp->gather_prescatter_num_contributors * samp->gather_prescatter_coefficient_width * sizeof(float);
  }
}

static void stbir__get_conservative_extents( stbir__sampler * samp, stbir__contributors * range, void * user_data )
{
  float scale = samp->scale_info.scale;
  float out_shift = samp->scale_info.pixel_shift;
  stbir__support_callback * support = samp->filter_support;
  int input_full_size = samp->scale_info.input_full_size;
  stbir_edge edge = samp->edge;
  float inv_scale = samp->scale_info.inv_scale;

  STBIR_ASSERT( samp->is_gather != 0 );

  if ( samp->is_gather == 1 )
  {
    int in_first_pixel, in_last_pixel;
    float out_filter_radius = support(inv_scale, user_data) * scale;

    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, 0.5, out_filter_radius, inv_scale, out_shift, input_full_size, edge );
    range->n0 = in_first_pixel;
    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, ( (float)(samp->scale_info.output_sub_size-1) ) + 0.5f, out_filter_radius, inv_scale, out_shift, input_full_size, edge );
    range->n1 = in_last_pixel;
  }
  else if ( samp->is_gather == 2 ) // downsample gather, refine
  {
    float in_pixels_radius = support(scale, user_data) * inv_scale;
    int filter_pixel_margin = samp->filter_pixel_margin;
    int output_sub_size = samp->scale_info.output_sub_size;
    int input_end;
    int n;
    int in_first_pixel, in_last_pixel;

    // get a conservative area of the input range
    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, 0, 0, inv_scale, out_shift, input_full_size, edge );
    range->n0 = in_first_pixel;
    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, (float)output_sub_size, 0, inv_scale, out_shift, input_full_size, edge );
    range->n1 = in_last_pixel;

    // now go through the margin to the start of area to find bottom
    n = range->n0 + 1;
    input_end = -filter_pixel_margin;
    while( n >= input_end )
    {
      int out_first_pixel, out_last_pixel;
      stbir__calculate_out_pixel_range( &out_first_pixel, &out_last_pixel, ((float)n)+0.5f, in_pixels_radius, scale, out_shift, output_sub_size );
      if ( out_first_pixel > out_last_pixel )
        break;

      if ( ( out_first_pixel < output_sub_size ) || ( out_last_pixel >= 0 ) )
        range->n0 = n;
      --n;
    }

    // now go through the end of the area through the margin to find top
    n = range->n1 - 1;
    input_end = n + 1 + filter_pixel_margin;
    while( n <= input_end )
    {
      int out_first_pixel, out_last_pixel;
      stbir__calculate_out_pixel_range( &out_first_pixel, &out_last_pixel, ((float)n)+0.5f, in_pixels_radius, scale, out_shift, output_sub_size );
      if ( out_first_pixel > out_last_pixel )
        break;
      if ( ( out_first_pixel < output_sub_size ) || ( out_last_pixel >= 0 ) )
        range->n1 = n;
      ++n;
    }
  }

  if ( samp->edge == STBIR_EDGE_WRAP )
  {
    // if we are wrapping, and we are very close to the image size (so the edges might merge), just use the scanline up to the edge
    if ( ( range->n0 > 0 ) && ( range->n1 >= input_full_size ) )
    {
      int marg = range->n1 - input_full_size + 1;
      if ( ( marg + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= range->n0 )
        range->n0 = 0;
    }
    if ( ( range->n0 < 0 ) && ( range->n1 < (input_full_size-1) ) )
    {
      int marg = -range->n0;
      if ( ( input_full_size - marg - STBIR__MERGE_RUNS_PIXEL_THRESHOLD - 1 ) <= range->n1 )
        range->n1 = input_full_size - 1;
    }
  }
  else
  {
    // for non-edge-wrap modes, we never read over the edge, so clamp
    if ( range->n0 < 0 )
      range->n0 = 0;
    if ( range->n1 >= input_full_size )
      range->n1 = input_full_size - 1;
  }
}

static void stbir__get_split_info( stbir__per_split_info* split_info, int splits, int output_height, int vertical_pixel_margin, int input_full_height, int is_gather, stbir__contributors * contribs )
{
  int i, cur;
  int left = output_height;

  cur = 0;
  for( i = 0 ; i < splits ; i++ )
  {
    int each;

    split_info[i].start_output_y = cur;
    each = left / ( splits - i );
    split_info[i].end_output_y = cur + each;

    // ok, when we are gathering, we need to make sure we are starting on a y offset that doesn't have
    //   a "special" set of coefficients. Basically, with exactly the right filter at exactly the right
    //   resize at exactly the right phase, some of the coefficents can be zero. When they are zero, we
    //   don't process them at all.  But this leads to a tricky thing with the thread splits, where we
    //   might have a set of two coeffs like this for example: (4,4) and (3,6).  The 4,4 means there was
    //   just one single coeff because things worked out perfectly (normally, they all have 4 coeffs
    //   like the range 3,6.  The problem is that if we start right on the (4,4) on a brand new thread,
    //   then when we get to (3,6), we don't have the "3" sample in memory (because we didn't load
    //   it on the initial (4,4) range because it didn't have a 3 (we only add new samples that are 
    //   larger than our existing samples - it's just how the eviction works). So, our solution here
    //   is pretty simple, if we start right on a range that has samples that start earlier, then we 
    //   simply bump up our previous thread split range to include it, and then start this threads
    //   range with the smaller sample. It just moves one scanline from one thread split to another,
    //   so that we end with the unusual one, instead of start with it. To do this, we check 2-4 
    //   sample at each thread split start and then occassionally move them.
    
    if ( ( is_gather ) && ( i ) )
    {
      stbir__contributors * small_contribs;
      int j, smallest, stop, start_n0;
      stbir__contributors * split_contribs = contribs + cur;

      // scan for a max of 3x the filter width or until the next thread split
      stop = vertical_pixel_margin * 3;
      if ( each < stop )
        stop = each;

      // loops a few times before early out
      smallest = 0;
      small_contribs = split_contribs;
      start_n0 = small_contribs->n0;
      for( j = 1 ; j <= stop ; j++ )
      {
        ++split_contribs;
        if ( split_contribs->n0 > start_n0 )
          break;
        if ( split_contribs->n0 < small_contribs->n0 )
        {
          small_contribs = split_contribs;
          smallest = j;
        }
      }

      split_info[i-1].end_output_y += smallest;
      split_info[i].start_output_y += smallest;
    }

    cur += each;
    left -= each;

    // scatter range (updated to minimum as you run it)
    split_info[i].start_input_y = -vertical_pixel_margin;
    split_info[i].end_input_y = input_full_height + vertical_pixel_margin;
  }
}

static void stbir__free_internal_mem( stbir__info *info )
{
  #define STBIR__FREE_AND_CLEAR( ptr ) { if ( ptr ) { void * p = (ptr); (ptr) = 0; STBIR_FREE( p, info->user_data); } }

  if ( info )
  {
  #ifndef STBIR__SEPARATE_ALLOCATIONS
    STBIR__FREE_AND_CLEAR( info->alloced_mem );
  #else
    int i,j;

    if ( ( info->vertical.gather_prescatter_contributors ) && ( (void*)info->vertical.gather_prescatter_contributors != (void*)info->split_info[0].decode_buffer ) )
    {
      STBIR__FREE_AND_CLEAR( info->vertical.gather_prescatter_coefficients );
      STBIR__FREE_AND_CLEAR( info->vertical.gather_prescatter_contributors );
    }
    for( i = 0 ; i < info->splits ; i++ )
    {
      for( j = 0 ; j < info->alloc_ring_buffer_num_entries ; j++ )
      {
        #ifdef STBIR_SIMD8
        if ( info->effective_channels == 3 )
          --info->split_info[i].ring_buffers[j]; // avx in 3 channel mode needs one float at the start of the buffer
        #endif
        STBIR__FREE_AND_CLEAR( info->split_info[i].ring_buffers[j] );
      }

      #ifdef STBIR_SIMD8
      if ( info->effective_channels == 3 )
        --info->split_info[i].decode_buffer; // avx in 3 channel mode needs one float at the start of the buffer
      #endif
      STBIR__FREE_AND_CLEAR( info->split_info[i].decode_buffer );
      STBIR__FREE_AND_CLEAR( info->split_info[i].ring_buffers );
      STBIR__FREE_AND_CLEAR( info->split_info[i].vertical_buffer );
    }
    STBIR__FREE_AND_CLEAR( info->split_info );
    if ( info->vertical.coefficients != info->horizontal.coefficients )
    {
      STBIR__FREE_AND_CLEAR( info->vertical.coefficients );
      STBIR__FREE_AND_CLEAR( info->vertical.contributors );
    }
    STBIR__FREE_AND_CLEAR( info->horizontal.coefficients );
    STBIR__FREE_AND_CLEAR( info->horizontal.contributors );
    STBIR__FREE_AND_CLEAR( info->alloced_mem );
    STBIR_FREE( info, info->user_data );
  #endif
  }

  #undef STBIR__FREE_AND_CLEAR
}

static int stbir__get_max_split( int splits, int height )
{
  int i;
  int max = 0;

  for( i = 0 ; i < splits ; i++ )
  {
    int each = height / ( splits - i );
    if ( each > max )
      max = each;
    height -= each;
  }
  return max;
}

static stbir__horizontal_gather_channels_func ** stbir__horizontal_gather_n_coeffs_funcs[8] =
{
  0, stbir__horizontal_gather_1_channels_with_n_coeffs_funcs, stbir__horizontal_gather_2_channels_with_n_coeffs_funcs, stbir__horizontal_gather_3_channels_with_n_coeffs_funcs, stbir__horizontal_gather_4_channels_with_n_coeffs_funcs, 0,0, stbir__horizontal_gather_7_channels_with_n_coeffs_funcs
};

static stbir__horizontal_gather_channels_func ** stbir__horizontal_gather_channels_funcs[8] =
{
  0, stbir__horizontal_gather_1_channels_funcs, stbir__horizontal_gather_2_channels_funcs, stbir__horizontal_gather_3_channels_funcs, stbir__horizontal_gather_4_channels_funcs, 0,0, stbir__horizontal_gather_7_channels_funcs
};

// there are six resize classifications: 0 == vertical scatter, 1 == vertical gather < 1x scale, 2 == vertical gather 1x-2x scale, 4 == vertical gather < 3x scale, 4 == vertical gather > 3x scale, 5 == <=4 pixel height, 6 == <=4 pixel wide column
#define STBIR_RESIZE_CLASSIFICATIONS 8

static float stbir__compute_weights[5][STBIR_RESIZE_CLASSIFICATIONS][4]=  // 5 = 0=1chan, 1=2chan, 2=3chan, 3=4chan, 4=7chan
{
  {
    { 1.00000f, 1.00000f, 0.31250f, 1.00000f },
    { 0.56250f, 0.59375f, 0.00000f, 0.96875f },
    { 1.00000f, 0.06250f, 0.00000f, 1.00000f },
    { 0.00000f, 0.09375f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 0.31250f, 1.00000f },
    { 0.03125f, 0.12500f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 0.06250f, 1.00000f },
    { 0.00000f, 1.00000f, 0.00000f, 0.03125f },
  }, {
    { 0.00000f, 0.84375f, 0.00000f, 0.03125f },
    { 0.09375f, 0.93750f, 0.00000f, 0.78125f },
    { 0.87500f, 0.21875f, 0.00000f, 0.96875f },
    { 0.09375f, 0.09375f, 1.00000f, 1.00000f },
    { 0.00000f, 0.84375f, 0.00000f, 0.03125f },
    { 0.03125f, 0.12500f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 0.06250f, 1.00000f },
    { 0.00000f, 1.00000f, 0.00000f, 0.53125f },
  }, {
    { 0.00000f, 0.53125f, 0.00000f, 0.03125f },
    { 0.06250f, 0.96875f, 0.00000f, 0.53125f },
    { 0.87500f, 0.18750f, 0.00000f, 0.93750f },
    { 0.00000f, 0.09375f, 1.00000f, 1.00000f },
    { 0.00000f, 0.53125f, 0.00000f, 0.03125f },
    { 0.03125f, 0.12500f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 0.06250f, 1.00000f },
    { 0.00000f, 1.00000f, 0.00000f, 0.56250f },
  }, {
    { 0.00000f, 0.50000f, 0.00000f, 0.71875f },
    { 0.06250f, 0.84375f, 0.00000f, 0.87500f },
    { 1.00000f, 0.50000f, 0.50000f, 0.96875f },
    { 1.00000f, 0.09375f, 0.31250f, 0.50000f },
    { 0.00000f, 0.50000f, 0.00000f, 0.71875f },
    { 1.00000f, 0.03125f, 0.03125f, 0.53125f },
    { 1.00000f, 1.00000f, 0.06250f, 1.00000f },
    { 0.00000f, 1.00000f, 0.03125f, 0.18750f },
  }, {
    { 0.00000f, 0.59375f, 0.00000f, 0.96875f },
    { 0.06250f, 0.81250f, 0.06250f, 0.59375f },
    { 0.75000f, 0.43750f, 0.12500f, 0.96875f },
    { 0.87500f, 0.06250f, 0.18750f, 0.43750f },
    { 0.00000f, 0.59375f, 0.00000f, 0.96875f },
    { 0.15625f, 0.12500f, 1.00000f, 1.00000f },
    { 1.00000f, 1.00000f, 0.06250f, 1.00000f },
    { 0.00000f, 1.00000f, 0.03125f, 0.34375f },
  }
};

// structure that allow us to query and override info for training the costs
typedef struct STBIR__V_FIRST_INFO
{
  double v_cost, h_cost;
  int control_v_first; // 0 = no control, 1 = force hori, 2 = force vert
  int v_first;
  int v_resize_classification;
  int is_gather;
} STBIR__V_FIRST_INFO;

#ifdef STBIR__V_FIRST_INFO_BUFFER
static STBIR__V_FIRST_INFO STBIR__V_FIRST_INFO_BUFFER = {0};
#define STBIR__V_FIRST_INFO_POINTER &STBIR__V_FIRST_INFO_BUFFER
#else
#define STBIR__V_FIRST_INFO_POINTER 0
#endif

// Figure out whether to scale along the horizontal or vertical first.
//   This only *super* important when you are scaling by a massively
//   different amount in the vertical vs the horizontal (for example, if
//   you are scaling by 2x in the width, and 0.5x in the height, then you
//   want to do the vertical scale first, because it's around 3x faster
//   in that order.
//
//   In more normal circumstances, this makes a 20-40% differences, so
//     it's good to get right, but not critical. The normal way that you
//     decide which direction goes first is just figuring out which
//     direction does more multiplies. But with modern CPUs with their
//     fancy caches and SIMD and high IPC abilities, so there's just a lot
//     more that goes into it.
//
//   My handwavy sort of solution is to have an app that does a whole
//     bunch of timing for both vertical and horizontal first modes,
//     and then another app that can read lots of these timing files
//     and try to search for the best weights to use. Dotimings.c
//     is the app that does a bunch of timings, and vf_train.c is the
//     app that solves for the best weights (and shows how well it
//     does currently).

static int stbir__should_do_vertical_first( float weights_table[STBIR_RESIZE_CLASSIFICATIONS][4], int horizontal_filter_pixel_width, float horizontal_scale, int horizontal_output_size, int vertical_filter_pixel_width, float vertical_scale, int vertical_output_size, int is_gather, STBIR__V_FIRST_INFO * info )
{
  double v_cost, h_cost;
  float * weights;
  int vertical_first;
  int v_classification;

  // categorize the resize into buckets
  if ( ( vertical_output_size <= 4 ) || ( horizontal_output_size <= 4 ) )
    v_classification = ( vertical_output_size < horizontal_output_size ) ? 6 : 7;
  else if ( ( !is_gather ) && ( ( vertical_output_size <= 16 ) || ( horizontal_output_size <= 16 ) ) )
    v_classification = 4;
  else if ( vertical_scale <= 1.0f )
    v_classification = ( is_gather ) ? 1 : 0;
  else if ( vertical_scale <= 2.0f)
    v_classification = 2;
  else if ( vertical_scale <= 3.0f)
    v_classification = 3;
  else 
    v_classification = 5; // everything bigger than 3x

  // use the right weights
  weights = weights_table[ v_classification ];

  // this is the costs when you don't take into account modern CPUs with high ipc and simd and caches - wish we had a better estimate
  h_cost = (float)horizontal_filter_pixel_width * weights[0] + horizontal_scale * (float)vertical_filter_pixel_width * weights[1];
  v_cost = (float)vertical_filter_pixel_width  * weights[2] + vertical_scale * (float)horizontal_filter_pixel_width * weights[3];

  // use computation estimate to decide vertical first or not
  vertical_first = ( v_cost <= h_cost ) ? 1 : 0;

  // save these, if requested
  if ( info )
  {
    info->h_cost = h_cost;
    info->v_cost = v_cost;
    info->v_resize_classification = v_classification;
    info->v_first = vertical_first;
    info->is_gather = is_gather;
  }

  // and this allows us to override everything for testing (see dotiming.c)
  if ( ( info ) && ( info->control_v_first ) )
    vertical_first = ( info->control_v_first == 2 ) ? 1 : 0;

  return vertical_first;
}

// layout lookups - must match stbir_internal_pixel_layout
static unsigned char stbir__pixel_channels[] = {
  1,2,3,3,4,   // 1ch, 2ch, rgb, bgr, 4ch
  4,4,4,4,2,2, // RGBA,BGRA,ARGB,ABGR,RA,AR
  4,4,4,4,2,2, // RGBA_PM,BGRA_PM,ARGB_PM,ABGR_PM,RA_PM,AR_PM
};

// the internal pixel layout enums are in a different order, so we can easily do range comparisons of types
//   the public pixel layout is ordered in a way that if you cast num_channels (1-4) to the enum, you get something sensible
static stbir_internal_pixel_layout stbir__pixel_layout_convert_public_to_internal[] = {
  STBIRI_BGR, STBIRI_1CHANNEL, STBIRI_2CHANNEL, STBIRI_RGB, STBIRI_RGBA,
  STBIRI_4CHANNEL, STBIRI_BGRA, STBIRI_ARGB, STBIRI_ABGR, STBIRI_RA, STBIRI_AR,
  STBIRI_RGBA_PM, STBIRI_BGRA_PM, STBIRI_ARGB_PM, STBIRI_ABGR_PM, STBIRI_RA_PM, STBIRI_AR_PM,
};

static stbir__info * stbir__alloc_internal_mem_and_build_samplers( stbir__sampler * horizontal, stbir__sampler * vertical, stbir__contributors * conservative, stbir_pixel_layout input_pixel_layout_public, stbir_pixel_layout output_pixel_layout_public, int splits, int new_x, int new_y, int fast_alpha, void * user_data STBIR_ONLY_PROFILE_BUILD_GET_INFO )
{
  static char stbir_channel_count_index[8]={ 9,0,1,2, 3,9,9,4 };

  stbir__info * info = 0;
  void * alloced = 0;
  size_t alloced_total = 0;
  int vertical_first;
  size_t decode_buffer_size, ring_buffer_length_bytes, ring_buffer_size, vertical_buffer_size;
  int alloc_ring_buffer_num_entries;

  int alpha_weighting_type = 0; // 0=none, 1=simple, 2=fancy
  int conservative_split_output_size = stbir__get_max_split( splits, vertical->scale_info.output_sub_size );
  stbir_internal_pixel_layout input_pixel_layout = stbir__pixel_layout_convert_public_to_internal[ input_pixel_layout_public ];
  stbir_internal_pixel_layout output_pixel_layout = stbir__pixel_layout_convert_public_to_internal[ output_pixel_layout_public ];
  int channels = stbir__pixel_channels[ input_pixel_layout ];
  int effective_channels = channels;

  // first figure out what type of alpha weighting to use (if any)
  if ( ( horizontal->filter_enum != STBIR_FILTER_POINT_SAMPLE ) || ( vertical->filter_enum != STBIR_FILTER_POINT_SAMPLE ) ) // no alpha weighting on point sampling
  {
    if ( ( input_pixel_layout >= STBIRI_RGBA ) && ( input_pixel_layout <= STBIRI_AR ) && ( output_pixel_layout >= STBIRI_RGBA ) && ( output_pixel_layout <= STBIRI_AR ) )
    {
      if ( fast_alpha )
      {
        alpha_weighting_type = 4;
      }
      else
      {
        static int fancy_alpha_effective_cnts[6] = { 7, 7, 7, 7, 3, 3 };
        alpha_weighting_type = 2;
        effective_channels = fancy_alpha_effective_cnts[ input_pixel_layout - STBIRI_RGBA ];
      }
    }
    else if ( ( input_pixel_layout >= STBIRI_RGBA_PM ) && ( input_pixel_layout <= STBIRI_AR_PM ) && ( output_pixel_layout >= STBIRI_RGBA ) && ( output_pixel_layout <= STBIRI_AR ) )
    {
      // input premult, output non-premult
      alpha_weighting_type = 3;
    }
    else if ( ( input_pixel_layout >= STBIRI_RGBA ) && ( input_pixel_layout <= STBIRI_AR ) && ( output_pixel_layout >= STBIRI_RGBA_PM ) && ( output_pixel_layout <= STBIRI_AR_PM ) )
    {
      // input non-premult, output premult
      alpha_weighting_type = 1;
    }
  }

  // channel in and out count must match currently
  if ( channels != stbir__pixel_channels[ output_pixel_layout ] )
    return 0;

  // get vertical first
  vertical_first = stbir__should_do_vertical_first( stbir__compute_weights[ (int)stbir_channel_count_index[ effective_channels ] ], horizontal->filter_pixel_width, horizontal->scale_info.scale, horizontal->scale_info.output_sub_size, vertical->filter_pixel_width, vertical->scale_info.scale, vertical->scale_info.output_sub_size, vertical->is_gather, STBIR__V_FIRST_INFO_POINTER );

  // sometimes read one float off in some of the unrolled loops (with a weight of zero coeff, so it doesn't have an effect)
  //   we use a few extra floats instead of just 1, so that input callback buffer can overlap with the decode buffer without
  //   the conversion routines overwriting the callback input data.
  decode_buffer_size = ( conservative->n1 - conservative->n0 + 1 ) * effective_channels * sizeof(float) + sizeof(float)*STBIR_INPUT_CALLBACK_PADDING; // extra floats for input callback stagger

#if defined( STBIR__SEPARATE_ALLOCATIONS ) && defined(STBIR_SIMD8)
  if ( effective_channels == 3 )
    decode_buffer_size += sizeof(float); // avx in 3 channel mode needs one float at the start of the buffer (only with separate allocations)
#endif

  ring_buffer_length_bytes = (size_t)horizontal->scale_info.output_sub_size * (size_t)effective_channels * sizeof(float) + sizeof(float)*STBIR_INPUT_CALLBACK_PADDING; // extra floats for padding

  // if we do vertical first, the ring buffer holds a whole decoded line
  if ( vertical_first )
    ring_buffer_length_bytes = ( decode_buffer_size + 15 ) & ~15;

  if ( ( ring_buffer_length_bytes & 4095 ) == 0 ) ring_buffer_length_bytes += 64*3; // avoid 4k alias

  // One extra entry because floating point precision problems sometimes cause an extra to be necessary.
  alloc_ring_buffer_num_entries = vertical->filter_pixel_width + 1;

  // we never need more ring buffer entries than the scanlines we're outputting when in scatter mode
  if ( ( !vertical->is_gather ) && ( alloc_ring_buffer_num_entries > conservative_split_output_size ) )
    alloc_ring_buffer_num_entries = conservative_split_output_size;

  ring_buffer_size = (size_t)alloc_ring_buffer_num_entries * (size_t)ring_buffer_length_bytes;

  // The vertical buffer is used differently, depending on whether we are scattering
  //   the vertical scanlines, or gathering them.
  //   If scattering, it's used at the temp buffer to accumulate each output.
  //   If gathering, it's just the output buffer.
  vertical_buffer_size = (size_t)horizontal->scale_info.output_sub_size * (size_t)effective_channels * sizeof(float) + sizeof(float);  // extra float for padding

  // we make two passes through this loop, 1st to add everything up, 2nd to allocate and init
  for(;;)
  {
    int i;
    void * advance_mem = alloced;
    int copy_horizontal = 0;
    stbir__sampler * possibly_use_horizontal_for_pivot = 0;

#ifdef STBIR__SEPARATE_ALLOCATIONS
    #define STBIR__NEXT_PTR( ptr, size, ntype ) if ( alloced ) { void * p = STBIR_MALLOC( size, user_data); if ( p == 0 ) { stbir__free_internal_mem( info ); return 0; } (ptr) = (ntype*)p; }
#else
    #define STBIR__NEXT_PTR( ptr, size, ntype ) advance_mem = (void*) ( ( ((size_t)advance_mem) + 15 ) & ~15 ); if ( alloced ) ptr = (ntype*)advance_mem; advance_mem = (char*)(((size_t)advance_mem) + (size));
#endif

    STBIR__NEXT_PTR( info, sizeof( stbir__info ), stbir__info );

    STBIR__NEXT_PTR( info->split_info, sizeof( stbir__per_split_info ) * splits, stbir__per_split_info );

    if ( info )
    {
      static stbir__alpha_weight_func * fancy_alpha_weights[6]  =    { stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_4ch,   stbir__fancy_alpha_weight_2ch,   stbir__fancy_alpha_weight_2ch };
      static stbir__alpha_unweight_func * fancy_alpha_unweights[6] = { stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_4ch, stbir__fancy_alpha_unweight_2ch, stbir__fancy_alpha_unweight_2ch };
      static stbir__alpha_weight_func * simple_alpha_weights[6] = { stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_4ch, stbir__simple_alpha_weight_2ch, stbir__simple_alpha_weight_2ch };
      static stbir__alpha_unweight_func * simple_alpha_unweights[6] = { stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_4ch, stbir__simple_alpha_unweight_2ch, stbir__simple_alpha_unweight_2ch };

      // initialize info fields
      info->alloced_mem = alloced;
      info->alloced_total = alloced_total;

      info->channels = channels;
      info->effective_channels = effective_channels;

      info->offset_x = new_x;
      info->offset_y = new_y;
      info->alloc_ring_buffer_num_entries = (int)alloc_ring_buffer_num_entries;
      info->ring_buffer_num_entries = 0;
      info->ring_buffer_length_bytes = (int)ring_buffer_length_bytes;
      info->splits = splits;
      info->vertical_first = vertical_first;

      info->input_pixel_layout_internal = input_pixel_layout;
      info->output_pixel_layout_internal = output_pixel_layout;

      // setup alpha weight functions
      info->alpha_weight = 0;
      info->alpha_unweight = 0;

      // handle alpha weighting functions and overrides
      if ( alpha_weighting_type == 2 )
      {
        // high quality alpha multiplying on the way in, dividing on the way out
        info->alpha_weight = fancy_alpha_weights[ input_pixel_layout - STBIRI_RGBA ];
        info->alpha_unweight = fancy_alpha_unweights[ output_pixel_layout - STBIRI_RGBA ];
      }
      else if ( alpha_weighting_type == 4 )
      {
        // fast alpha multiplying on the way in, dividing on the way out
        info->alpha_weight = simple_alpha_weights[ input_pixel_layout - STBIRI_RGBA ];
        info->alpha_unweight = simple_alpha_unweights[ output_pixel_layout - STBIRI_RGBA ];
      }
      else if ( alpha_weighting_type == 1 )
      {
        // fast alpha on the way in, leave in premultiplied form on way out
        info->alpha_weight = simple_alpha_weights[ input_pixel_layout - STBIRI_RGBA ];
      }
      else if ( alpha_weighting_type == 3 )
      {
        // incoming is premultiplied, fast alpha dividing on the way out - non-premultiplied output
        info->alpha_unweight = simple_alpha_unweights[ output_pixel_layout - STBIRI_RGBA ];
      }

      // handle 3-chan color flipping, using the alpha weight path
      if ( ( ( input_pixel_layout == STBIRI_RGB ) && ( output_pixel_layout == STBIRI_BGR ) ) ||
           ( ( input_pixel_layout == STBIRI_BGR ) && ( output_pixel_layout == STBIRI_RGB ) ) )
      {
        // do the flipping on the smaller of the two ends
        if ( horizontal->scale_info.scale < 1.0f )
          info->alpha_unweight = stbir__simple_flip_3ch;
        else
          info->alpha_weight = stbir__simple_flip_3ch;
      }

    }

    // get all the per-split buffers
    for( i = 0 ; i < splits ; i++ )
    {
      STBIR__NEXT_PTR( info->split_info[i].decode_buffer, decode_buffer_size, float );

#ifdef STBIR__SEPARATE_ALLOCATIONS

      #ifdef STBIR_SIMD8
      if ( ( info ) && ( effective_channels == 3 ) )
        ++info->split_info[i].decode_buffer; // avx in 3 channel mode needs one float at the start of the buffer
      #endif

      STBIR__NEXT_PTR( info->split_info[i].ring_buffers, alloc_ring_buffer_num_entries * sizeof(float*), float* );
      {
        int j;
        for( j = 0 ; j < alloc_ring_buffer_num_entries ; j++ )
        {
          STBIR__NEXT_PTR( info->split_info[i].ring_buffers[j], ring_buffer_length_bytes, float );
          #ifdef STBIR_SIMD8
          if ( ( info ) && ( effective_channels == 3 ) )
            ++info->split_info[i].ring_buffers[j]; // avx in 3 channel mode needs one float at the start of the buffer
          #endif
        }
      }
#else
      STBIR__NEXT_PTR( info->split_info[i].ring_buffer, ring_buffer_size, float );
#endif
      STBIR__NEXT_PTR( info->split_info[i].vertical_buffer, vertical_buffer_size, float );
    }

    // alloc memory for to-be-pivoted coeffs (if necessary)
    if ( vertical->is_gather == 0 )
    {
      size_t both;
      size_t temp_mem_amt;

      // when in vertical scatter mode, we first build the coefficients in gather mode, and then pivot after,
      //   that means we need two buffers, so we try to use the decode buffer and ring buffer for this. if that
      //   is too small, we just allocate extra memory to use as this temp.

      both = (size_t)vertical->gather_prescatter_contributors_size + (size_t)vertical->gather_prescatter_coefficients_size;

#ifdef STBIR__SEPARATE_ALLOCATIONS
      temp_mem_amt = decode_buffer_size;

      #ifdef STBIR_SIMD8
      if ( effective_channels == 3 )
        --temp_mem_amt; // avx in 3 channel mode needs one float at the start of the buffer
      #endif
#else
      temp_mem_amt = (size_t)( decode_buffer_size + ring_buffer_size + vertical_buffer_size ) * (size_t)splits;
#endif
      if ( temp_mem_amt >= both )
      {
        if ( info )
        {
          vertical->gather_prescatter_contributors = (stbir__contributors*)info->split_info[0].decode_buffer;
          vertical->gather_prescatter_coefficients = (float*) ( ( (char*)info->split_info[0].decode_buffer ) + vertical->gather_prescatter_contributors_size );
        }
      }
      else
      {
        // ring+decode memory is too small, so allocate temp memory
        STBIR__NEXT_PTR( vertical->gather_prescatter_contributors, vertical->gather_prescatter_contributors_size, stbir__contributors );
        STBIR__NEXT_PTR( vertical->gather_prescatter_coefficients, vertical->gather_prescatter_coefficients_size, float );
      }
    }

    STBIR__NEXT_PTR( horizontal->contributors, horizontal->contributors_size, stbir__contributors );
    STBIR__NEXT_PTR( horizontal->coefficients, horizontal->coefficients_size, float );

    // are the two filters identical?? (happens a lot with mipmap generation)
    if ( ( horizontal->filter_kernel == vertical->filter_kernel ) && ( horizontal->filter_support == vertical->filter_support ) && ( horizontal->edge == vertical->edge ) && ( horizontal->scale_info.output_sub_size == vertical->scale_info.output_sub_size ) )
    {
      float diff_scale = horizontal->scale_info.scale - vertical->scale_info.scale;
      float diff_shift = horizontal->scale_info.pixel_shift - vertical->scale_info.pixel_shift;
      if ( diff_scale < 0.0f ) diff_scale = -diff_scale;
      if ( diff_shift < 0.0f ) diff_shift = -diff_shift;
      if ( ( diff_scale <= stbir__small_float ) && ( diff_shift <= stbir__small_float ) )
      {
        if ( horizontal->is_gather == vertical->is_gather )
        {
          copy_horizontal = 1;
          goto no_vert_alloc;
        }
        // everything matches, but vertical is scatter, horizontal is gather, use horizontal coeffs for vertical pivot coeffs
        possibly_use_horizontal_for_pivot = horizontal;
      }
    }

    STBIR__NEXT_PTR( vertical->contributors, vertical->contributors_size, stbir__contributors );
    STBIR__NEXT_PTR( vertical->coefficients, vertical->coefficients_size, float );

   no_vert_alloc:

    if ( info )
    {
      STBIR_PROFILE_BUILD_START( horizontal );

      stbir__calculate_filters( horizontal, 0, user_data STBIR_ONLY_PROFILE_BUILD_SET_INFO );

      // setup the horizontal gather functions
      // start with defaulting to the n_coeffs functions (specialized on channels and remnant leftover)
      info->horizontal_gather_channels = stbir__horizontal_gather_n_coeffs_funcs[ effective_channels ][ horizontal->extent_info.widest & 3 ];
      // but if the number of coeffs <= 12, use another set of special cases. <=12 coeffs is any enlarging resize, or shrinking resize down to about 1/3 size
      if ( horizontal->extent_info.widest <= 12 )
        info->horizontal_gather_channels = stbir__horizontal_gather_channels_funcs[ effective_channels ][ horizontal->extent_info.widest - 1 ];

      info->scanline_extents.conservative.n0 = conservative->n0;
      info->scanline_extents.conservative.n1 = conservative->n1;

      // get exact extents
      stbir__get_extents( horizontal, &info->scanline_extents );

      // pack the horizontal coeffs
      horizontal->coefficient_width = stbir__pack_coefficients(horizontal->num_contributors, horizontal->contributors, horizontal->coefficients, horizontal->coefficient_width, horizontal->extent_info.widest, info->scanline_extents.conservative.n0, info->scanline_extents.conservative.n1 );

      STBIR_MEMCPY( &info->horizontal, horizontal, sizeof( stbir__sampler ) );

      STBIR_PROFILE_BUILD_END( horizontal );

      if ( copy_horizontal )
      {
        STBIR_MEMCPY( &info->vertical, horizontal, sizeof( stbir__sampler ) );
      }
      else
      {
        STBIR_PROFILE_BUILD_START( vertical );

        stbir__calculate_filters( vertical, possibly_use_horizontal_for_pivot, user_data STBIR_ONLY_PROFILE_BUILD_SET_INFO );
        STBIR_MEMCPY( &info->vertical, vertical, sizeof( stbir__sampler ) );

        STBIR_PROFILE_BUILD_END( vertical );
      }

      // setup the vertical split ranges
      stbir__get_split_info( info->split_info, info->splits, info->vertical.scale_info.output_sub_size, info->vertical.filter_pixel_margin, info->vertical.scale_info.input_full_size, info->vertical.is_gather, info->vertical.contributors );

      // now we know precisely how many entries we need
      info->ring_buffer_num_entries = info->vertical.extent_info.widest;

      // we never need more ring buffer entries than the scanlines we're outputting
      if ( ( !info->vertical.is_gather ) && ( info->ring_buffer_num_entries > conservative_split_output_size ) )
        info->ring_buffer_num_entries = conservative_split_output_size;
      STBIR_ASSERT( info->ring_buffer_num_entries <= info->alloc_ring_buffer_num_entries );
    }
    #undef STBIR__NEXT_PTR


    // is this the first time through loop?
    if ( info == 0 )
    {
      alloced_total = ( 15 + (size_t)advance_mem );
      alloced = STBIR_MALLOC( alloced_total, user_data );
      if ( alloced == 0 )
        return 0;
    }
    else
      return info;  // success
  }
}
