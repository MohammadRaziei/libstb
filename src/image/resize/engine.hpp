// Running a resize (perform_resize), regions, the STBIR_RESIZE setup functions and the quick helpers.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

static int stbir__perform_resize( stbir__info const * info, int split_start, int split_count )
{
  stbir__per_split_info * split_info = info->split_info + split_start;

  STBIR_PROFILE_CLEAR_EXTRAS();

  STBIR_PROFILE_FIRST_START( looping );
  if (info->vertical.is_gather)
    stbir__vertical_gather_loop( info, split_info, split_count );
  else
    stbir__vertical_scatter_loop( info, split_info, split_count );
  STBIR_PROFILE_END( looping );

  return 1;
}

static void stbir__update_info_from_resize( stbir__info * info, STBIR_RESIZE * resize )
{
  static stbir__decode_pixels_func * decode_simple[STBIR_TYPE_HALF_FLOAT-STBIR_TYPE_UINT8_SRGB+1]=
  {
    /* 1ch-4ch */ stbir__decode_uint8_srgb, stbir__decode_uint8_srgb, 0, stbir__decode_float_linear, stbir__decode_half_float_linear,
  };

  static stbir__decode_pixels_func * decode_alphas[STBIRI_AR-STBIRI_RGBA+1][STBIR_TYPE_HALF_FLOAT-STBIR_TYPE_UINT8_SRGB+1]=
  {
    { /* RGBA */ stbir__decode_uint8_srgb4_linearalpha,      stbir__decode_uint8_srgb,      0, stbir__decode_float_linear,      stbir__decode_half_float_linear },
    { /* BGRA */ stbir__decode_uint8_srgb4_linearalpha_BGRA, stbir__decode_uint8_srgb_BGRA, 0, stbir__decode_float_linear_BGRA, stbir__decode_half_float_linear_BGRA },
    { /* ARGB */ stbir__decode_uint8_srgb4_linearalpha_ARGB, stbir__decode_uint8_srgb_ARGB, 0, stbir__decode_float_linear_ARGB, stbir__decode_half_float_linear_ARGB },
    { /* ABGR */ stbir__decode_uint8_srgb4_linearalpha_ABGR, stbir__decode_uint8_srgb_ABGR, 0, stbir__decode_float_linear_ABGR, stbir__decode_half_float_linear_ABGR },
    { /* RA   */ stbir__decode_uint8_srgb2_linearalpha,      stbir__decode_uint8_srgb,      0, stbir__decode_float_linear,      stbir__decode_half_float_linear },
    { /* AR   */ stbir__decode_uint8_srgb2_linearalpha_AR,   stbir__decode_uint8_srgb_AR,   0, stbir__decode_float_linear_AR,   stbir__decode_half_float_linear_AR },
  };

  static stbir__decode_pixels_func * decode_simple_scaled_or_not[2][2]=
  {
    { stbir__decode_uint8_linear_scaled,  stbir__decode_uint8_linear }, { stbir__decode_uint16_linear_scaled, stbir__decode_uint16_linear },
  };

  static stbir__decode_pixels_func * decode_alphas_scaled_or_not[STBIRI_AR-STBIRI_RGBA+1][2][2]=
  {
    { /* RGBA */ { stbir__decode_uint8_linear_scaled,       stbir__decode_uint8_linear },      { stbir__decode_uint16_linear_scaled,      stbir__decode_uint16_linear } },
    { /* BGRA */ { stbir__decode_uint8_linear_scaled_BGRA,  stbir__decode_uint8_linear_BGRA }, { stbir__decode_uint16_linear_scaled_BGRA, stbir__decode_uint16_linear_BGRA } },
    { /* ARGB */ { stbir__decode_uint8_linear_scaled_ARGB,  stbir__decode_uint8_linear_ARGB }, { stbir__decode_uint16_linear_scaled_ARGB, stbir__decode_uint16_linear_ARGB } },
    { /* ABGR */ { stbir__decode_uint8_linear_scaled_ABGR,  stbir__decode_uint8_linear_ABGR }, { stbir__decode_uint16_linear_scaled_ABGR, stbir__decode_uint16_linear_ABGR } },
    { /* RA   */ { stbir__decode_uint8_linear_scaled,       stbir__decode_uint8_linear },      { stbir__decode_uint16_linear_scaled,      stbir__decode_uint16_linear } },
    { /* AR   */ { stbir__decode_uint8_linear_scaled_AR,    stbir__decode_uint8_linear_AR },   { stbir__decode_uint16_linear_scaled_AR,   stbir__decode_uint16_linear_AR } }
  };

  static stbir__encode_pixels_func * encode_simple[STBIR_TYPE_HALF_FLOAT-STBIR_TYPE_UINT8_SRGB+1]=
  {
    /* 1ch-4ch */ stbir__encode_uint8_srgb, stbir__encode_uint8_srgb, 0, stbir__encode_float_linear, stbir__encode_half_float_linear,
  };

  static stbir__encode_pixels_func * encode_alphas[STBIRI_AR-STBIRI_RGBA+1][STBIR_TYPE_HALF_FLOAT-STBIR_TYPE_UINT8_SRGB+1]=
  {
    { /* RGBA */ stbir__encode_uint8_srgb4_linearalpha,      stbir__encode_uint8_srgb,      0, stbir__encode_float_linear,      stbir__encode_half_float_linear },
    { /* BGRA */ stbir__encode_uint8_srgb4_linearalpha_BGRA, stbir__encode_uint8_srgb_BGRA, 0, stbir__encode_float_linear_BGRA, stbir__encode_half_float_linear_BGRA },
    { /* ARGB */ stbir__encode_uint8_srgb4_linearalpha_ARGB, stbir__encode_uint8_srgb_ARGB, 0, stbir__encode_float_linear_ARGB, stbir__encode_half_float_linear_ARGB },
    { /* ABGR */ stbir__encode_uint8_srgb4_linearalpha_ABGR, stbir__encode_uint8_srgb_ABGR, 0, stbir__encode_float_linear_ABGR, stbir__encode_half_float_linear_ABGR },
    { /* RA   */ stbir__encode_uint8_srgb2_linearalpha,      stbir__encode_uint8_srgb,      0, stbir__encode_float_linear,      stbir__encode_half_float_linear },
    { /* AR   */ stbir__encode_uint8_srgb2_linearalpha_AR,   stbir__encode_uint8_srgb_AR,   0, stbir__encode_float_linear_AR,   stbir__encode_half_float_linear_AR }
  };

  static stbir__encode_pixels_func * encode_simple_scaled_or_not[2][2]=
  {
    { stbir__encode_uint8_linear_scaled,  stbir__encode_uint8_linear }, { stbir__encode_uint16_linear_scaled, stbir__encode_uint16_linear },
  };

  static stbir__encode_pixels_func * encode_alphas_scaled_or_not[STBIRI_AR-STBIRI_RGBA+1][2][2]=
  {
    { /* RGBA */ { stbir__encode_uint8_linear_scaled,       stbir__encode_uint8_linear },       { stbir__encode_uint16_linear_scaled,      stbir__encode_uint16_linear } },
    { /* BGRA */ { stbir__encode_uint8_linear_scaled_BGRA,  stbir__encode_uint8_linear_BGRA },  { stbir__encode_uint16_linear_scaled_BGRA, stbir__encode_uint16_linear_BGRA } },
    { /* ARGB */ { stbir__encode_uint8_linear_scaled_ARGB,  stbir__encode_uint8_linear_ARGB },  { stbir__encode_uint16_linear_scaled_ARGB, stbir__encode_uint16_linear_ARGB } },
    { /* ABGR */ { stbir__encode_uint8_linear_scaled_ABGR,  stbir__encode_uint8_linear_ABGR },  { stbir__encode_uint16_linear_scaled_ABGR, stbir__encode_uint16_linear_ABGR } },
    { /* RA   */ { stbir__encode_uint8_linear_scaled,       stbir__encode_uint8_linear },       { stbir__encode_uint16_linear_scaled,      stbir__encode_uint16_linear } },
    { /* AR   */ { stbir__encode_uint8_linear_scaled_AR,    stbir__encode_uint8_linear_AR },    { stbir__encode_uint16_linear_scaled_AR,   stbir__encode_uint16_linear_AR } }
  };

  stbir__decode_pixels_func * decode_pixels = 0;
  stbir__encode_pixels_func * encode_pixels = 0;
  stbir_datatype input_type, output_type;

  input_type = resize->input_data_type;
  output_type = resize->output_data_type;
  info->input_data = resize->input_pixels;
  info->input_stride_bytes = resize->input_stride_in_bytes;
  info->output_stride_bytes = resize->output_stride_in_bytes;

  // if we're completely point sampling, then we can turn off SRGB
  if ( ( info->horizontal.filter_enum == STBIR_FILTER_POINT_SAMPLE ) && ( info->vertical.filter_enum == STBIR_FILTER_POINT_SAMPLE ) )
  {
    if ( ( ( input_type  == STBIR_TYPE_UINT8_SRGB ) || ( input_type  == STBIR_TYPE_UINT8_SRGB_ALPHA ) ) &&
         ( ( output_type == STBIR_TYPE_UINT8_SRGB ) || ( output_type == STBIR_TYPE_UINT8_SRGB_ALPHA ) ) )
    {
      input_type = STBIR_TYPE_UINT8;
      output_type = STBIR_TYPE_UINT8;
    }
  }

  // recalc the output and input strides
  if ( info->input_stride_bytes == 0 )
    info->input_stride_bytes = info->channels * info->horizontal.scale_info.input_full_size * stbir__type_size[input_type];

  if ( info->output_stride_bytes == 0 )
    info->output_stride_bytes = info->channels * info->horizontal.scale_info.output_sub_size * stbir__type_size[output_type];

  // calc offset
  info->output_data = ( (char*) resize->output_pixels ) + ( (size_t) info->offset_y * (size_t) resize->output_stride_in_bytes ) + ( info->offset_x * info->channels * stbir__type_size[output_type] );

  info->in_pixels_cb = resize->input_cb;
  info->user_data = resize->user_data;
  info->out_pixels_cb = resize->output_cb;

  // setup the input format converters
  if ( ( input_type == STBIR_TYPE_UINT8 ) || ( input_type == STBIR_TYPE_UINT16 ) )
  {
    int non_scaled = 0;

    // check if we can run unscaled - 0-255.0/0-65535.0 instead of 0-1.0 (which is a tiny bit faster when doing linear 8->8 or 16->16)
    if ( ( !info->alpha_weight ) && ( !info->alpha_unweight )  ) // don't short circuit when alpha weighting (get everything to 0-1.0 as usual)
      if ( ( ( input_type == STBIR_TYPE_UINT8 ) && ( output_type == STBIR_TYPE_UINT8 ) ) || ( ( input_type == STBIR_TYPE_UINT16 ) && ( output_type == STBIR_TYPE_UINT16 ) ) )
        non_scaled = 1;

    if ( info->input_pixel_layout_internal <= STBIRI_4CHANNEL )
      decode_pixels = decode_simple_scaled_or_not[ input_type == STBIR_TYPE_UINT16 ][ non_scaled ];
    else
      decode_pixels = decode_alphas_scaled_or_not[ ( info->input_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ input_type == STBIR_TYPE_UINT16 ][ non_scaled ];
  }
  else
  {
    if ( info->input_pixel_layout_internal <= STBIRI_4CHANNEL )
      decode_pixels = decode_simple[ input_type - STBIR_TYPE_UINT8_SRGB ];
    else
      decode_pixels = decode_alphas[ ( info->input_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ input_type - STBIR_TYPE_UINT8_SRGB ];
  }

  // setup the output format converters
  if ( ( output_type == STBIR_TYPE_UINT8 ) || ( output_type == STBIR_TYPE_UINT16 ) )
  {
    int non_scaled = 0;

    // check if we can run unscaled - 0-255.0/0-65535.0 instead of 0-1.0 (which is a tiny bit faster when doing linear 8->8 or 16->16)
    if ( ( !info->alpha_weight ) && ( !info->alpha_unweight ) ) // don't short circuit when alpha weighting (get everything to 0-1.0 as usual)
      if ( ( ( input_type == STBIR_TYPE_UINT8 ) && ( output_type == STBIR_TYPE_UINT8 ) ) || ( ( input_type == STBIR_TYPE_UINT16 ) && ( output_type == STBIR_TYPE_UINT16 ) ) )
        non_scaled = 1;

    if ( info->output_pixel_layout_internal <= STBIRI_4CHANNEL )
      encode_pixels = encode_simple_scaled_or_not[ output_type == STBIR_TYPE_UINT16 ][ non_scaled ];
    else
      encode_pixels = encode_alphas_scaled_or_not[ ( info->output_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ output_type == STBIR_TYPE_UINT16 ][ non_scaled ];
  }
  else
  {
    if ( info->output_pixel_layout_internal <= STBIRI_4CHANNEL )
      encode_pixels = encode_simple[ output_type - STBIR_TYPE_UINT8_SRGB ];
    else
      encode_pixels = encode_alphas[ ( info->output_pixel_layout_internal - STBIRI_RGBA ) % ( STBIRI_AR-STBIRI_RGBA+1 ) ][ output_type - STBIR_TYPE_UINT8_SRGB ];
  }

  info->input_type = input_type;
  info->output_type = output_type;
  info->decode_pixels = decode_pixels;
  info->encode_pixels = encode_pixels;
}

static void stbir__clip( int * outx, int * outsubw, int outw, double * u0, double * u1 )
{
  double per, adj;
  int over;

  // do left/top edge
  if ( *outx < 0 )
  {
    per = ( (double)*outx ) / ( (double)*outsubw ); // is negative
    adj = per * ( *u1 - *u0 );
    *u0 -= adj; // increases u0
    *outx = 0;
  }

  // do right/bot edge
  over = outw - ( *outx + *outsubw );
  if ( over < 0 )
  {
    per = ( (double)over ) / ( (double)*outsubw ); // is negative
    adj = per * ( *u1 - *u0 );
    *u1 += adj; // decrease u1
    *outsubw = outw - *outx;
  }
}

// converts a double to a rational that has less than one float bit of error (returns 0 if unable to do so)
static int stbir__double_to_rational(double f, stbir_uint32 limit, stbir_uint32 *numer, stbir_uint32 *denom, int limit_denom ) // limit_denom (1) or limit numer (0)
{
  double err;
  stbir_uint64 top, bot;
  stbir_uint64 numer_last = 0;
  stbir_uint64 denom_last = 1;
  stbir_uint64 numer_estimate = 1;
  stbir_uint64 denom_estimate = 0;

  // scale to past float error range
  top = (stbir_uint64)( f * (double)(1 << 25) );
  bot = 1 << 25;

  // keep refining, but usually stops in a few loops - usually 5 for bad cases
  for(;;)
  {
    stbir_uint64 est, temp;

    // hit limit, break out and do best full range estimate
    if ( ( ( limit_denom ) ? denom_estimate : numer_estimate ) >= limit )
      break;

    // is the current error less than 1 bit of a float? if so, we're done
    if ( denom_estimate )
    {
      err = ( (double)numer_estimate / (double)denom_estimate ) - f;
      if ( err < 0.0 ) err = -err;
      if ( err < ( 1.0 / (double)(1<<24) ) )
      {
        // yup, found it
        *numer = (stbir_uint32) numer_estimate;
        *denom = (stbir_uint32) denom_estimate;
        return 1;
      }
    }

    // no more refinement bits left? break out and do full range estimate
    if ( bot == 0 )
      break;

    // gcd the estimate bits
    est = top / bot;
    temp = top % bot;
    top = bot;
    bot = temp;

    // move remainders
    temp = est * denom_estimate + denom_last;
    denom_last = denom_estimate;
    denom_estimate = temp;

    // move remainders
    temp = est * numer_estimate + numer_last;
    numer_last = numer_estimate;
    numer_estimate = temp;
  }

  // we didn't find anything good enough for float, use a full range estimate
  if ( limit_denom )
  {
    numer_estimate= (stbir_uint64)( f * (double)limit + 0.5 );
    denom_estimate = limit;
  }
  else
  {
    numer_estimate = limit;
    denom_estimate = (stbir_uint64)( ( (double)limit / f ) + 0.5 );
  }

  *numer = (stbir_uint32) numer_estimate;
  *denom = (stbir_uint32) denom_estimate;

  err = ( denom_estimate ) ? ( ( (double)(stbir_uint32)numer_estimate / (double)(stbir_uint32)denom_estimate ) - f ) : 1.0;
  if ( err < 0.0 ) err = -err;
  return ( err < ( 1.0 / (double)(1<<24) ) ) ? 1 : 0;
}

static int stbir__calculate_region_transform( stbir__scale_info * scale_info, int output_full_range, int * output_offset, int output_sub_range, int input_full_range, double input_s0, double input_s1 )
{
  double output_range, input_range, output_s, input_s, ratio, scale;

  input_s = input_s1 - input_s0;

  // null area
  if ( ( output_full_range == 0 ) || ( input_full_range == 0 ) ||
       ( output_sub_range == 0 ) || ( input_s <= stbir__small_float ) )
    return 0;

  // are either of the ranges completely out of bounds?
  if ( ( *output_offset >= output_full_range ) || ( ( *output_offset + output_sub_range ) <= 0 ) || ( input_s0 >= (1.0f-stbir__small_float) ) || ( input_s1 <= stbir__small_float ) )
    return 0;

  output_range = (double)output_full_range;
  input_range = (double)input_full_range;

  output_s = ( (double)output_sub_range) / output_range;

  // figure out the scaling to use
  ratio = output_s / input_s;

  // save scale before clipping
  scale = ( output_range / input_range ) * ratio;
  scale_info->scale = (float)scale;
  scale_info->inv_scale = (float)( 1.0 / scale );

  // clip output area to left/right output edges (and adjust input area)
  stbir__clip( output_offset, &output_sub_range, output_full_range, &input_s0, &input_s1 );

  // recalc input area
  input_s = input_s1 - input_s0;

  // after clipping do we have zero input area?
  if ( input_s <= stbir__small_float )
    return 0;

  // calculate and store the starting source offsets in output pixel space
  scale_info->pixel_shift = (float) ( input_s0 * ratio * output_range );

  scale_info->scale_is_rational = stbir__double_to_rational( scale, ( scale <= 1.0 ) ? output_full_range : input_full_range, &scale_info->scale_numerator, &scale_info->scale_denominator, ( scale >= 1.0 ) );

  scale_info->input_full_size = input_full_range;
  scale_info->output_sub_size = output_sub_range;

  return 1;
}


static void stbir__init_and_set_layout( STBIR_RESIZE * resize, stbir_pixel_layout pixel_layout, stbir_datatype data_type )
{
  resize->input_cb = 0;
  resize->output_cb = 0;
  resize->user_data = resize;
  resize->samplers = 0;
  resize->called_alloc = 0;
  resize->horizontal_filter = STBIR_FILTER_DEFAULT;
  resize->horizontal_filter_kernel = 0; resize->horizontal_filter_support = 0;
  resize->vertical_filter = STBIR_FILTER_DEFAULT;
  resize->vertical_filter_kernel = 0; resize->vertical_filter_support = 0;
  resize->horizontal_edge = STBIR_EDGE_CLAMP;
  resize->vertical_edge = STBIR_EDGE_CLAMP;
  resize->input_s0 = 0; resize->input_t0 = 0; resize->input_s1 = 1; resize->input_t1 = 1;
  resize->output_subx = 0; resize->output_suby = 0; resize->output_subw = resize->output_w; resize->output_subh = resize->output_h;
  resize->input_data_type = data_type;
  resize->output_data_type = data_type;
  resize->input_pixel_layout_public = pixel_layout;
  resize->output_pixel_layout_public = pixel_layout;
  resize->needs_rebuild = 1;
}

STBIRDEF void stbir_resize_init( STBIR_RESIZE * resize,
                                 const void *input_pixels,  int input_w,  int input_h, int input_stride_in_bytes, // stride can be zero
                                       void *output_pixels, int output_w, int output_h, int output_stride_in_bytes, // stride can be zero
                                 stbir_pixel_layout pixel_layout, stbir_datatype data_type )
{
  resize->input_pixels = input_pixels;
  resize->input_w = input_w;
  resize->input_h = input_h;
  resize->input_stride_in_bytes = input_stride_in_bytes;
  resize->output_pixels = output_pixels;
  resize->output_w = output_w;
  resize->output_h = output_h;
  resize->output_stride_in_bytes = output_stride_in_bytes;
  resize->fast_alpha = 0;

  stbir__init_and_set_layout( resize, pixel_layout, data_type );
}






STBIRDEF int stbir_set_edgemodes( STBIR_RESIZE * resize, stbir_edge horizontal_edge, stbir_edge vertical_edge )       // CLAMP by default
{
  resize->horizontal_edge = horizontal_edge;
  resize->vertical_edge = vertical_edge;
  resize->needs_rebuild = 1;
  return 1;
}

STBIRDEF int stbir_set_filters( STBIR_RESIZE * resize, stbir_filter horizontal_filter, stbir_filter vertical_filter ) // STBIR_DEFAULT_FILTER_UPSAMPLE/DOWNSAMPLE by default
{
  resize->horizontal_filter = horizontal_filter;
  resize->vertical_filter = vertical_filter;
  resize->needs_rebuild = 1;
  return 1;
}








static int stbir__perform_build( STBIR_RESIZE * resize, int splits )
{
  stbir__contributors conservative = { 0, 0 };
  stbir__sampler horizontal, vertical;
  int new_output_subx, new_output_suby;
  stbir__info * out_info;
  #ifdef STBIR_PROFILE
  stbir__info profile_infod;  // used to contain building profile info before everything is allocated
  stbir__info * profile_info = &profile_infod;
  #endif

  // have we already built the samplers?
  if ( resize->samplers )
    return 0;

  #define STBIR_RETURN_ERROR_AND_ASSERT( exp )  STBIR_ASSERT( !(exp) ); if (exp) return 0;
  STBIR_RETURN_ERROR_AND_ASSERT( (unsigned)resize->horizontal_filter >= STBIR_FILTER_OTHER)
  STBIR_RETURN_ERROR_AND_ASSERT( (unsigned)resize->vertical_filter >= STBIR_FILTER_OTHER)
  #undef STBIR_RETURN_ERROR_AND_ASSERT

  if ( splits <= 0 )
    return 0;

  STBIR_PROFILE_BUILD_FIRST_START( build );

  new_output_subx = resize->output_subx;
  new_output_suby = resize->output_suby;

  // do horizontal clip and scale calcs
  if ( !stbir__calculate_region_transform( &horizontal.scale_info, resize->output_w, &new_output_subx, resize->output_subw, resize->input_w, resize->input_s0, resize->input_s1 ) )
    return 0;

  // do vertical clip and scale calcs
  if ( !stbir__calculate_region_transform( &vertical.scale_info, resize->output_h, &new_output_suby, resize->output_subh, resize->input_h, resize->input_t0, resize->input_t1 ) )
    return 0;

  // if nothing to do, just return
  if ( ( horizontal.scale_info.output_sub_size == 0 ) || ( vertical.scale_info.output_sub_size == 0 ) )
    return 0;

  stbir__set_sampler(&horizontal, resize->horizontal_filter, resize->horizontal_filter_kernel, resize->horizontal_filter_support, resize->horizontal_edge, &horizontal.scale_info, 1, resize->user_data );
  stbir__get_conservative_extents( &horizontal, &conservative, resize->user_data );
  stbir__set_sampler(&vertical, resize->vertical_filter, resize->vertical_filter_kernel, resize->vertical_filter_support, resize->vertical_edge, &vertical.scale_info, 0, resize->user_data );

  if ( ( vertical.scale_info.output_sub_size / splits ) < STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS ) // each split should be a minimum of 4 scanlines (handwavey choice)
  {
    splits = vertical.scale_info.output_sub_size / STBIR_FORCE_MINIMUM_SCANLINES_FOR_SPLITS;
    if ( splits == 0 ) splits = 1;
  }

  STBIR_PROFILE_BUILD_START( alloc );
  out_info = stbir__alloc_internal_mem_and_build_samplers( &horizontal, &vertical, &conservative, resize->input_pixel_layout_public, resize->output_pixel_layout_public, splits, new_output_subx, new_output_suby, resize->fast_alpha, resize->user_data STBIR_ONLY_PROFILE_BUILD_SET_INFO );
  STBIR_PROFILE_BUILD_END( alloc );
  STBIR_PROFILE_BUILD_END( build );

  if ( out_info )
  {
    resize->splits = splits;
    resize->samplers = out_info;
    resize->needs_rebuild = 0;
    #ifdef STBIR_PROFILE
      STBIR_MEMCPY( &out_info->profile, &profile_infod.profile, sizeof( out_info->profile ) );
    #endif

    // update anything that can be changed without recalcing samplers
    stbir__update_info_from_resize( out_info, resize );

    return splits;
  }

  return 0;
}

STBIRDEF void stbir_free_samplers( STBIR_RESIZE * resize )
{
  if ( resize->samplers )
  {
    stbir__free_internal_mem( resize->samplers );
    resize->samplers = 0;
    resize->called_alloc = 0;
  }
}

STBIRDEF int stbir_build_samplers_with_splits( STBIR_RESIZE * resize, int splits )
{
  if ( ( resize->samplers == 0 ) || ( resize->needs_rebuild ) )
  {
    if ( resize->samplers )
      stbir_free_samplers( resize );

    resize->called_alloc = 1;
    return stbir__perform_build( resize, splits );
  }

  STBIR_PROFILE_BUILD_CLEAR( resize->samplers );

  return 1;
}

STBIRDEF int stbir_build_samplers( STBIR_RESIZE * resize )
{
  return stbir_build_samplers_with_splits( resize, 1 );
}

STBIRDEF int stbir_resize_extended( STBIR_RESIZE * resize )
{
  int result;

  if ( ( resize->samplers == 0 ) || ( resize->needs_rebuild ) )
  {
    int alloc_state = resize->called_alloc;  // remember allocated state

    if ( resize->samplers )
    {
      stbir__free_internal_mem( resize->samplers );
      resize->samplers = 0;
    }

    if ( !stbir_build_samplers( resize ) )
      return 0;

    resize->called_alloc = alloc_state;

    // if build_samplers succeeded (above), but there are no samplers set, then
    //   the area to stretch into was zero pixels, so don't do anything and return
    //   success
    if ( resize->samplers == 0 )
      return 1;
  }
  else
  {
    // didn't build anything - clear it
    STBIR_PROFILE_BUILD_CLEAR( resize->samplers );
  }

  // do resize
  result = stbir__perform_resize( resize->samplers, 0, resize->splits );

  // if we alloced, then free
  if ( !resize->called_alloc )
  {
    stbir_free_samplers( resize );
    resize->samplers = 0;
  }

  return result;
}

#ifdef STBIR_PROFILE

STBIRDEF void stbir_resize_build_profile_info( STBIR_PROFILE_INFO * info, STBIR_RESIZE const * resize )
{
  static char const * bdescriptions[6] = { "Building", "Allocating", "Horizontal sampler", "Vertical sampler", "Coefficient cleanup", "Coefficient pivot" } ;
  stbir__info* samp = resize->samplers;
  int i;

  typedef int testa[ (STBIR__ARRAY_SIZE( bdescriptions ) == (STBIR__ARRAY_SIZE( samp->profile.array )-1) )?1:-1];
  typedef int testb[ (sizeof( samp->profile.array ) == (sizeof(samp->profile.named)) )?1:-1];
  typedef int testc[ (sizeof( info->clocks ) >= (sizeof(samp->profile.named)) )?1:-1];

  for( i = 0 ; i < STBIR__ARRAY_SIZE( bdescriptions ) ; i++)
    info->clocks[i] = samp->profile.array[i+1];

  info->total_clocks = samp->profile.named.total;
  info->descriptions = bdescriptions;
  info->count = STBIR__ARRAY_SIZE( bdescriptions );
}

STBIRDEF void stbir_resize_split_profile_info( STBIR_PROFILE_INFO * info, STBIR_RESIZE const * resize, int split_start, int split_count )
{
  static char const * descriptions[7] = { "Looping", "Vertical sampling", "Horizontal sampling", "Scanline input", "Scanline output", "Alpha weighting", "Alpha unweighting" };
  stbir__per_split_info * split_info;
  int s, i;

  typedef int testa[ (STBIR__ARRAY_SIZE( descriptions ) == (STBIR__ARRAY_SIZE( split_info->profile.array )-1) )?1:-1];
  typedef int testb[ (sizeof( split_info->profile.array ) == (sizeof(split_info->profile.named)) )?1:-1];
  typedef int testc[ (sizeof( info->clocks ) >= (sizeof(split_info->profile.named)) )?1:-1];

  if ( split_start == -1 )
  {
    split_start = 0;
    split_count = resize->samplers->splits;
  }

  if ( ( split_start >= resize->splits ) || ( split_start < 0 ) || ( ( split_start + split_count ) > resize->splits ) || ( split_count <= 0 ) )
  {
    info->total_clocks = 0;
    info->descriptions = 0;
    info->count = 0;
    return;
  }

  split_info = resize->samplers->split_info + split_start;

  // sum up the profile from all the splits
  for( i = 0 ; i < STBIR__ARRAY_SIZE( descriptions ) ; i++ )
  {
    stbir_uint64 sum = 0;
    for( s = 0 ; s < split_count ; s++ )
      sum += split_info[s].profile.array[i+1];
    info->clocks[i] = sum;
  }

  info->total_clocks = split_info->profile.named.total; 
  info->descriptions = descriptions;
  info->count = STBIR__ARRAY_SIZE( descriptions );
}

STBIRDEF void stbir_resize_extended_profile_info( STBIR_PROFILE_INFO * info, STBIR_RESIZE const * resize )
{
  stbir_resize_split_profile_info( info, resize, -1, 0 );
}

#endif // STBIR_PROFILE

#undef STBIR_BGR
#undef STBIR_1CHANNEL
#undef STBIR_2CHANNEL
#undef STBIR_RGB
#undef STBIR_RGBA
#undef STBIR_4CHANNEL
#undef STBIR_BGRA
#undef STBIR_ARGB
#undef STBIR_ABGR
#undef STBIR_RA
#undef STBIR_AR
#undef STBIR_RGBA_PM
#undef STBIR_BGRA_PM
#undef STBIR_ARGB_PM
#undef STBIR_ABGR_PM
#undef STBIR_RA_PM
#undef STBIR_AR_PM
