// stbir__decode_scanline: input pixels of any layout / type to the float working format.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

static void stbir__decode_scanline(stbir__info const * stbir_info, int n, float * output_buffer STBIR_ONLY_PROFILE_GET_SPLIT_INFO )
{
  int channels = stbir_info->channels;
  int effective_channels = stbir_info->effective_channels;
  int input_sample_in_bytes = stbir__type_size[stbir_info->input_type] * channels;
  stbir_edge edge_horizontal = stbir_info->horizontal.edge;
  stbir_edge edge_vertical = stbir_info->vertical.edge;
  int row = stbir__edge_wrap(edge_vertical, n, stbir_info->vertical.scale_info.input_full_size);
  const void* input_plane_data = ( (char *) stbir_info->input_data ) + (size_t)row * (size_t) stbir_info->input_stride_bytes;
  stbir__span const * spans = stbir_info->scanline_extents.spans;
  float * full_decode_buffer = output_buffer - stbir_info->scanline_extents.conservative.n0 * effective_channels;
  float * last_decoded = 0;

  // if we are on edge_zero, and we get in here with an out of bounds n, then the calculate filters has failed
  STBIR_ASSERT( !(edge_vertical == STBIR_EDGE_ZERO && (n < 0 || n >= stbir_info->vertical.scale_info.input_full_size)) );

  do
  {
    float * decode_buffer;
    void const * input_data;
    float * end_decode;
    int width_times_channels;
    int width;

    if ( spans->n1 < spans->n0 )
      break;

    width = spans->n1 + 1 - spans->n0;
    decode_buffer = full_decode_buffer + spans->n0 * effective_channels;
    end_decode = full_decode_buffer + ( spans->n1 + 1 ) * effective_channels;
    width_times_channels = width * channels;

    // read directly out of input plane by default
    input_data = ( (char*)input_plane_data ) + spans->pixel_offset_for_input * input_sample_in_bytes;

    // if we have an input callback, call it to get the input data
    if ( stbir_info->in_pixels_cb )
    {
      // call the callback with a temp buffer (that they can choose to use or not).  the temp is just right aligned memory in the decode_buffer itself
      input_data = stbir_info->in_pixels_cb( ( (char*) end_decode ) - ( width * input_sample_in_bytes ) + ( ( stbir_info->input_type != STBIR_TYPE_FLOAT ) ? ( sizeof(float)*STBIR_INPUT_CALLBACK_PADDING ) : 0 ), input_plane_data, width, spans->pixel_offset_for_input, row, stbir_info->user_data );
    }

    STBIR_PROFILE_START( decode );
    // convert the pixels info the float decode_buffer, (we index from end_decode, so that when channels<effective_channels, we are right justified in the buffer)
    last_decoded = stbir_info->decode_pixels( (float*)end_decode - width_times_channels, width_times_channels, input_data );
    STBIR_PROFILE_END( decode );

    if (stbir_info->alpha_weight)
    {
      STBIR_PROFILE_START( alpha );
      stbir_info->alpha_weight( decode_buffer, width_times_channels );
      STBIR_PROFILE_END( alpha );
    }

    ++spans;
  } while ( spans <= ( &stbir_info->scanline_extents.spans[1] ) );

  // handle the edge_wrap filter (all other types are handled back out at the calculate_filter stage)
  // basically the idea here is that if we have the whole scanline in memory, we don't redecode the
  //   wrapped edge pixels, and instead just memcpy them from the scanline into the edge positions
  if ( ( edge_horizontal == STBIR_EDGE_WRAP ) && ( stbir_info->scanline_extents.edge_sizes[0] | stbir_info->scanline_extents.edge_sizes[1] ) )
  {
    // this code only runs if we're in edge_wrap, and we're doing the entire scanline
    int e, start_x[2];
    int input_full_size = stbir_info->horizontal.scale_info.input_full_size;

    start_x[0] = -stbir_info->scanline_extents.edge_sizes[0];  // left edge start x
    start_x[1] =  input_full_size;                             // right edge

    for( e = 0; e < 2 ; e++ )
    {
      // do each margin
      int margin = stbir_info->scanline_extents.edge_sizes[e];
      if ( margin )
      {
        int x = start_x[e];
        float * marg = full_decode_buffer + x * effective_channels;
        float const * src = full_decode_buffer + stbir__edge_wrap(edge_horizontal, x, input_full_size) * effective_channels;
        STBIR_MEMCPY( marg, src, margin * effective_channels * sizeof(float) );
        if ( e == 1 ) last_decoded = marg + margin * effective_channels;
      }
    }
  }
  
  // some of the horizontal gathers read one float off the edge (which is masked out), but we force a zero here to make sure no NaNs leak in
  //   (we can't pre-zero it, because the input callback can use that area as padding)
  last_decoded[0] = 0.0f; 

  // we clear this extra float, because the final output pixel filter kernel might have used one less coeff than the max filter width
  //   when this happens, we do read that pixel from the input, so it too could be Nan, so just zero an extra one.
  //   this fits because each scanline is padded by three floats (STBIR_INPUT_CALLBACK_PADDING)
  last_decoded[1] = 0.0f;
}


//=================
