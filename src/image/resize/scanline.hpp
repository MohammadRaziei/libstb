// The encoder, the ring buffer of decoded scanlines and the gather / scatter loops that drive the kernels.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

static void stbir__encode_scanline( stbir__info const * stbir_info, void *output_buffer_data, float * encode_buffer, int row  STBIR_ONLY_PROFILE_GET_SPLIT_INFO )
{
  int num_pixels = stbir_info->horizontal.scale_info.output_sub_size;
  int channels = stbir_info->channels;
  int width_times_channels = num_pixels * channels;
  void * output_buffer;

  // un-alpha weight if we need to
  if ( stbir_info->alpha_unweight )
  {
    STBIR_PROFILE_START( unalpha );
    stbir_info->alpha_unweight( encode_buffer, width_times_channels );
    STBIR_PROFILE_END( unalpha );
  }

  // write directly into output by default
  output_buffer = output_buffer_data;

  // if we have an output callback, we first convert the decode buffer in place (and then hand that to the callback)
  if ( stbir_info->out_pixels_cb )
    output_buffer = encode_buffer;

  STBIR_PROFILE_START( encode );
  // convert into the output buffer
  stbir_info->encode_pixels( output_buffer, width_times_channels, encode_buffer );
  STBIR_PROFILE_END( encode );

  // if we have an output callback, call it to send the data
  if ( stbir_info->out_pixels_cb )
    stbir_info->out_pixels_cb( output_buffer, num_pixels, row, stbir_info->user_data );
}


// Get the ring buffer pointer for an index
static float* stbir__get_ring_buffer_entry(stbir__info const * stbir_info, stbir__per_split_info const * split_info, int index )
{
  STBIR_ASSERT( index < stbir_info->ring_buffer_num_entries );

  #ifdef STBIR__SEPARATE_ALLOCATIONS
    return split_info->ring_buffers[ index ];
  #else
    return (float*) ( ( (char*) split_info->ring_buffer ) + ( index * stbir_info->ring_buffer_length_bytes ) );
  #endif
}

// Get the specified scan line from the ring buffer
static float* stbir__get_ring_buffer_scanline(stbir__info const * stbir_info, stbir__per_split_info const * split_info, int get_scanline)
{
  int ring_buffer_index = (split_info->ring_buffer_begin_index + (get_scanline - split_info->ring_buffer_first_scanline)) % stbir_info->ring_buffer_num_entries;
  return stbir__get_ring_buffer_entry( stbir_info, split_info, ring_buffer_index );
}

static void stbir__resample_horizontal_gather(stbir__info const * stbir_info, float* output_buffer, float const * input_buffer STBIR_ONLY_PROFILE_GET_SPLIT_INFO )
{
  float const * decode_buffer = input_buffer - ( stbir_info->scanline_extents.conservative.n0 * stbir_info->effective_channels );

  STBIR_PROFILE_START( horizontal );
  if ( ( stbir_info->horizontal.filter_enum == STBIR_FILTER_POINT_SAMPLE ) && ( stbir_info->horizontal.scale_info.scale == 1.0f ) )
    STBIR_MEMCPY( output_buffer, input_buffer, stbir_info->horizontal.scale_info.output_sub_size * sizeof( float ) * stbir_info->effective_channels );
  else
    stbir_info->horizontal_gather_channels( output_buffer, stbir_info->horizontal.scale_info.output_sub_size, decode_buffer, stbir_info->horizontal.contributors, stbir_info->horizontal.coefficients, stbir_info->horizontal.coefficient_width );
  STBIR_PROFILE_END( horizontal );
}

static void stbir__resample_vertical_gather(stbir__info const * stbir_info, stbir__per_split_info* split_info, int n, int contrib_n0, int contrib_n1, float const * vertical_coefficients )
{
  float* encode_buffer = split_info->vertical_buffer;
  float* decode_buffer = split_info->decode_buffer;
  int vertical_first = stbir_info->vertical_first;
  int width = (vertical_first) ? ( stbir_info->scanline_extents.conservative.n1-stbir_info->scanline_extents.conservative.n0+1 ) : stbir_info->horizontal.scale_info.output_sub_size;
  int width_times_channels = stbir_info->effective_channels * width;

  STBIR_ASSERT( stbir_info->vertical.is_gather );

  // loop over the contributing scanlines and scale into the buffer
  STBIR_PROFILE_START( vertical );
  {
    int k = 0, total = contrib_n1 - contrib_n0 + 1;
    STBIR_ASSERT( total > 0 );
    do {
      float const * inputs[8];
      int i, cnt = total; if ( cnt > 8 ) cnt = 8;
      for( i = 0 ; i < cnt ; i++ )
        inputs[ i ] = stbir__get_ring_buffer_scanline(stbir_info, split_info, k+i+contrib_n0 );

      // call the N scanlines at a time function (up to 8 scanlines of blending at once)
      ((k==0)?stbir__vertical_gathers:stbir__vertical_gathers_continues)[cnt-1]( (vertical_first) ? decode_buffer : encode_buffer, vertical_coefficients + k, inputs, inputs[0] + width_times_channels );
      k += cnt;
      total -= cnt;
    } while ( total );
  }
  STBIR_PROFILE_END( vertical );

  if ( vertical_first )
  {
    // Now resample the gathered vertical data in the horizontal axis into the encode buffer
    decode_buffer[ width_times_channels ] = 0.0f; // clear two over for horizontals with a remnant of 3
    decode_buffer[ width_times_channels+1 ] = 0.0f; 
    stbir__resample_horizontal_gather(stbir_info, encode_buffer, decode_buffer  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );
  }

  stbir__encode_scanline( stbir_info, ( (char *) stbir_info->output_data ) + ((size_t)n * (size_t)stbir_info->output_stride_bytes),
                          encode_buffer, n  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );
}

static void stbir__decode_and_resample_for_vertical_gather_loop(stbir__info const * stbir_info, stbir__per_split_info* split_info, int n)
{
  int ring_buffer_index;
  float* ring_buffer;

  // Decode the nth scanline from the source image into the decode buffer.
  stbir__decode_scanline( stbir_info, n, split_info->decode_buffer  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );

  // update new end scanline
  split_info->ring_buffer_last_scanline = n;

  // get ring buffer
  ring_buffer_index = (split_info->ring_buffer_begin_index + (split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline)) % stbir_info->ring_buffer_num_entries;
  ring_buffer = stbir__get_ring_buffer_entry(stbir_info, split_info, ring_buffer_index);

  // Now resample it into the ring buffer.
  stbir__resample_horizontal_gather( stbir_info, ring_buffer, split_info->decode_buffer  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );

  // Now it's sitting in the ring buffer ready to be used as source for the vertical sampling.
}

static void stbir__vertical_gather_loop( stbir__info const * stbir_info, stbir__per_split_info* split_info, int split_count )
{
  int y, start_output_y, end_output_y;
  stbir__contributors* vertical_contributors = stbir_info->vertical.contributors;
  float const * vertical_coefficients = stbir_info->vertical.coefficients;

  STBIR_ASSERT( stbir_info->vertical.is_gather );

  start_output_y = split_info->start_output_y;
  end_output_y = split_info[split_count-1].end_output_y;

  vertical_contributors += start_output_y;
  vertical_coefficients += start_output_y * stbir_info->vertical.coefficient_width;

  // initialize the ring buffer for gathering
  split_info->ring_buffer_begin_index = 0;
  split_info->ring_buffer_first_scanline = vertical_contributors->n0;
  split_info->ring_buffer_last_scanline = split_info->ring_buffer_first_scanline - 1; // means "empty"

  for (y = start_output_y; y < end_output_y; y++)
  {
    int in_first_scanline, in_last_scanline;

    in_first_scanline = vertical_contributors->n0;
    in_last_scanline = vertical_contributors->n1;

    // make sure the indexing hasn't broken
    STBIR_ASSERT( in_first_scanline >= split_info->ring_buffer_first_scanline );

    // Load in new scanlines
    while (in_last_scanline > split_info->ring_buffer_last_scanline)
    {
      STBIR_ASSERT( ( split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline + 1 ) <= stbir_info->ring_buffer_num_entries );

      // make sure there was room in the ring buffer when we add new scanlines
      if ( ( split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline + 1 ) == stbir_info->ring_buffer_num_entries )
      {
        split_info->ring_buffer_first_scanline++;
        split_info->ring_buffer_begin_index++;
      }

      if ( stbir_info->vertical_first )
      {
        float * ring_buffer = stbir__get_ring_buffer_scanline( stbir_info, split_info, ++split_info->ring_buffer_last_scanline );
        // Decode the nth scanline from the source image into the decode buffer.
        stbir__decode_scanline( stbir_info, split_info->ring_buffer_last_scanline, ring_buffer  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );
      }
      else
      {
        stbir__decode_and_resample_for_vertical_gather_loop(stbir_info, split_info, split_info->ring_buffer_last_scanline + 1);
      }
    }

    // Now all buffers should be ready to write a row of vertical sampling, so do it.
    stbir__resample_vertical_gather(stbir_info, split_info, y, in_first_scanline, in_last_scanline, vertical_coefficients );

    ++vertical_contributors;
    vertical_coefficients += stbir_info->vertical.coefficient_width;
  }
}

#define STBIR__FLOAT_EMPTY_MARKER 3.0e+38F
#define STBIR__FLOAT_BUFFER_IS_EMPTY(ptr) ((ptr)[0]==STBIR__FLOAT_EMPTY_MARKER)

static void stbir__encode_first_scanline_from_scatter(stbir__info const * stbir_info, stbir__per_split_info* split_info)
{
  // evict a scanline out into the output buffer
  float* ring_buffer_entry = stbir__get_ring_buffer_entry(stbir_info, split_info, split_info->ring_buffer_begin_index );

  // dump the scanline out
  stbir__encode_scanline( stbir_info, ( (char *)stbir_info->output_data ) + ( (size_t)split_info->ring_buffer_first_scanline * (size_t)stbir_info->output_stride_bytes ), ring_buffer_entry, split_info->ring_buffer_first_scanline  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );

  // mark it as empty
  ring_buffer_entry[ 0 ] = STBIR__FLOAT_EMPTY_MARKER;

  // advance the first scanline
  split_info->ring_buffer_first_scanline++;
  if ( ++split_info->ring_buffer_begin_index == stbir_info->ring_buffer_num_entries )
    split_info->ring_buffer_begin_index = 0;
}

static void stbir__horizontal_resample_and_encode_first_scanline_from_scatter(stbir__info const * stbir_info, stbir__per_split_info* split_info)
{
  // evict a scanline out into the output buffer

  float* ring_buffer_entry = stbir__get_ring_buffer_entry(stbir_info, split_info, split_info->ring_buffer_begin_index );

  // Now resample it into the buffer.
  stbir__resample_horizontal_gather( stbir_info, split_info->vertical_buffer, ring_buffer_entry  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );

  // dump the scanline out
  stbir__encode_scanline( stbir_info, ( (char *)stbir_info->output_data ) + ( (size_t)split_info->ring_buffer_first_scanline * (size_t)stbir_info->output_stride_bytes ), split_info->vertical_buffer, split_info->ring_buffer_first_scanline  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );

  // mark it as empty
  ring_buffer_entry[ 0 ] = STBIR__FLOAT_EMPTY_MARKER;

  // advance the first scanline
  split_info->ring_buffer_first_scanline++;
  if ( ++split_info->ring_buffer_begin_index == stbir_info->ring_buffer_num_entries )
    split_info->ring_buffer_begin_index = 0;
}

static void stbir__resample_vertical_scatter(stbir__info const * stbir_info, stbir__per_split_info* split_info, int n0, int n1, float const * vertical_coefficients, float const * vertical_buffer, float const * vertical_buffer_end )
{
  STBIR_ASSERT( !stbir_info->vertical.is_gather );

  STBIR_PROFILE_START( vertical );
  {
    int k = 0, total = n1 - n0 + 1;
    STBIR_ASSERT( total > 0 );
    do {
      float * outputs[8];
      int i, n = total; if ( n > 8 ) n = 8;
      for( i = 0 ; i < n ; i++ )
      {
        outputs[ i ] = stbir__get_ring_buffer_scanline(stbir_info, split_info, k+i+n0 );
        if ( ( i ) && ( STBIR__FLOAT_BUFFER_IS_EMPTY( outputs[i] ) != STBIR__FLOAT_BUFFER_IS_EMPTY( outputs[0] ) ) ) // make sure runs are of the same type
        {
          n = i;
          break;
        }
      }
      // call the scatter to N scanlines at a time function (up to 8 scanlines of scattering at once)
      ((STBIR__FLOAT_BUFFER_IS_EMPTY( outputs[0] ))?stbir__vertical_scatter_sets:stbir__vertical_scatter_blends)[n-1]( outputs, vertical_coefficients + k, vertical_buffer, vertical_buffer_end );
      k += n;
      total -= n;
    } while ( total );
  }

  STBIR_PROFILE_END( vertical );
}

typedef void stbir__handle_scanline_for_scatter_func(stbir__info const * stbir_info, stbir__per_split_info* split_info);

static void stbir__vertical_scatter_loop( stbir__info const * stbir_info, stbir__per_split_info* split_info, int split_count )
{
  int y, start_output_y, end_output_y, start_input_y, end_input_y;
  stbir__contributors* vertical_contributors = stbir_info->vertical.contributors;
  float const * vertical_coefficients = stbir_info->vertical.coefficients;
  stbir__handle_scanline_for_scatter_func * handle_scanline_for_scatter;
  void * scanline_scatter_buffer;
  void * scanline_scatter_buffer_end;
  int on_first_input_y, last_input_y;
  int width = (stbir_info->vertical_first) ? ( stbir_info->scanline_extents.conservative.n1-stbir_info->scanline_extents.conservative.n0+1 ) : stbir_info->horizontal.scale_info.output_sub_size;
  int width_times_channels = stbir_info->effective_channels * width;

  STBIR_ASSERT( !stbir_info->vertical.is_gather );

  start_output_y = split_info->start_output_y;
  end_output_y = split_info[split_count-1].end_output_y;  // may do multiple split counts

  start_input_y = split_info->start_input_y;
  end_input_y = split_info[split_count-1].end_input_y;

  // adjust for starting offset start_input_y
  y = start_input_y + stbir_info->vertical.filter_pixel_margin;
  vertical_contributors += y ;
  vertical_coefficients += stbir_info->vertical.coefficient_width * y;

  if ( stbir_info->vertical_first )
  {
    handle_scanline_for_scatter = stbir__horizontal_resample_and_encode_first_scanline_from_scatter;
    scanline_scatter_buffer = split_info->decode_buffer;
    scanline_scatter_buffer_end = ( (char*) scanline_scatter_buffer ) + sizeof( float ) * stbir_info->effective_channels * (stbir_info->scanline_extents.conservative.n1-stbir_info->scanline_extents.conservative.n0+1);
  }
  else
  {
    handle_scanline_for_scatter = stbir__encode_first_scanline_from_scatter;
    scanline_scatter_buffer = split_info->vertical_buffer;
    scanline_scatter_buffer_end = ( (char*) scanline_scatter_buffer ) + sizeof( float ) * stbir_info->effective_channels * stbir_info->horizontal.scale_info.output_sub_size;
  }

  // initialize the ring buffer for scattering
  split_info->ring_buffer_first_scanline = start_output_y;
  split_info->ring_buffer_last_scanline = -1;
  split_info->ring_buffer_begin_index = -1;

  // mark all the buffers as empty to start
  for( y = 0 ; y < stbir_info->ring_buffer_num_entries ; y++ )
  {
    float * decode_buffer = stbir__get_ring_buffer_entry( stbir_info, split_info, y );
    decode_buffer[ width_times_channels ] = 0.0f; // clear two over for horizontals with a remnant of 3
    decode_buffer[ width_times_channels+1 ] = 0.0f; 
    decode_buffer[0] = STBIR__FLOAT_EMPTY_MARKER; // only used on scatter
  }

  // do the loop in input space
  on_first_input_y = 1; last_input_y = start_input_y;
  for (y = start_input_y ; y < end_input_y; y++)
  {
    int out_first_scanline, out_last_scanline;

    out_first_scanline = vertical_contributors->n0;
    out_last_scanline = vertical_contributors->n1;

    STBIR_ASSERT(out_last_scanline - out_first_scanline + 1 <= stbir_info->ring_buffer_num_entries);

    if ( ( out_last_scanline >= out_first_scanline ) && ( ( ( out_first_scanline >= start_output_y ) && ( out_first_scanline < end_output_y ) ) || ( ( out_last_scanline >= start_output_y ) && ( out_last_scanline < end_output_y ) ) ) )
    {
      float const * vc = vertical_coefficients;

      // keep track of the range actually seen for the next resize
      last_input_y = y;
      if ( ( on_first_input_y ) && ( y > start_input_y ) )
        split_info->start_input_y = y;
      on_first_input_y = 0;

      // clip the region
      if ( out_first_scanline < start_output_y )
      {
        vc += start_output_y - out_first_scanline;
        out_first_scanline = start_output_y;
      }

      if ( out_last_scanline >= end_output_y )
        out_last_scanline = end_output_y - 1;

      // if very first scanline, init the index
      if (split_info->ring_buffer_begin_index < 0)
        split_info->ring_buffer_begin_index = out_first_scanline - start_output_y;

      STBIR_ASSERT( split_info->ring_buffer_begin_index <= out_first_scanline );

      // Decode the nth scanline from the source image into the decode buffer.
      stbir__decode_scanline( stbir_info, y, split_info->decode_buffer  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );

      // When horizontal first, we resample horizontally into the vertical buffer before we scatter it out
      if ( !stbir_info->vertical_first )
        stbir__resample_horizontal_gather( stbir_info, split_info->vertical_buffer, split_info->decode_buffer  STBIR_ONLY_PROFILE_SET_SPLIT_INFO );

      // Now it's sitting in the buffer ready to be distributed into the ring buffers.

      // evict from the ringbuffer, if we need are full
      if ( ( ( split_info->ring_buffer_last_scanline - split_info->ring_buffer_first_scanline + 1 ) == stbir_info->ring_buffer_num_entries ) &&
           ( out_last_scanline > split_info->ring_buffer_last_scanline ) )
        handle_scanline_for_scatter( stbir_info, split_info );

      // Now the horizontal buffer is ready to write to all ring buffer rows, so do it.
      stbir__resample_vertical_scatter(stbir_info, split_info, out_first_scanline, out_last_scanline, vc, (float*)scanline_scatter_buffer, (float*)scanline_scatter_buffer_end );

      // update the end of the buffer
      if ( out_last_scanline > split_info->ring_buffer_last_scanline )
        split_info->ring_buffer_last_scanline = out_last_scanline;
    }
    ++vertical_contributors;
    vertical_coefficients += stbir_info->vertical.coefficient_width;
  }

  // now evict the scanlines that are left over in the ring buffer
  while ( split_info->ring_buffer_first_scanline < end_output_y )
    handle_scanline_for_scatter(stbir_info, split_info);

  // update the end_input_y if we do multiple resizes with the same data
  ++last_input_y;
  for( y = 0 ; y < split_count; y++ )
    if ( split_info[y].end_input_y > last_input_y )
      split_info[y].end_input_y = last_input_y;
}


static stbir__kernel_callback * stbir__builtin_kernels[] =   { 0, stbir__filter_trapezoid,  stbir__filter_triangle, stbir__filter_cubic, stbir__filter_catmullrom, stbir__filter_mitchell, stbir__filter_point };
static stbir__support_callback * stbir__builtin_supports[] = { 0, stbir__support_trapezoid, stbir__support_one,     stbir__support_two,  stbir__support_two,       stbir__support_two,     stbir__support_zeropoint5 };
