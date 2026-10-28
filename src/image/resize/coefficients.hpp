// Pixel extents and the filter coefficients: gather / scatter, normalisation, packing.
// A part of the resizer translation unit: included by resize.cpp, in order. Derived from
// stb_image_resize2.h v2.18 (public domain / MIT, Sean Barrett and contributors).

static void stbir__get_extents( stbir__sampler * samp, stbir__extents * scanline_extents )
{
  int j, stop;
  int left_margin, right_margin;
  int min_n = 0x7fffffff, max_n = -0x7fffffff;
  int min_left = 0x7fffffff, max_left = -0x7fffffff;
  int min_right = 0x7fffffff, max_right = -0x7fffffff;
  stbir_edge edge = samp->edge;
  stbir__contributors* contributors = samp->contributors;
  int output_sub_size = samp->scale_info.output_sub_size;
  int input_full_size = samp->scale_info.input_full_size;
  int filter_pixel_margin = samp->filter_pixel_margin;

  STBIR_ASSERT( samp->is_gather );

  stop = output_sub_size;
  for (j = 0; j < stop; j++ )
  {
    STBIR_ASSERT( contributors[j].n1 >= contributors[j].n0 );
    if ( contributors[j].n0 < min_n )
    {
      min_n = contributors[j].n0;
      stop = j + filter_pixel_margin;  // if we find a new min, only scan another filter width
      if ( stop > output_sub_size ) stop = output_sub_size;
    }
  }

  stop = 0;
  for (j = output_sub_size - 1; j >= stop; j-- )
  {
    STBIR_ASSERT( contributors[j].n1 >= contributors[j].n0 );
    if ( contributors[j].n1 > max_n )
    {
      max_n = contributors[j].n1;
      stop = j - filter_pixel_margin;  // if we find a new max, only scan another filter width
      if (stop<0) stop = 0;
    }
  }

  STBIR_ASSERT( scanline_extents->conservative.n0 <= min_n );
  STBIR_ASSERT( scanline_extents->conservative.n1 >= max_n );

  // now calculate how much into the margins we really read
  left_margin = 0;
  if ( min_n < 0 )
  {
    left_margin = -min_n;
    min_n = 0;
  }

  right_margin = 0;
  if ( max_n >= input_full_size )
  {
    right_margin = max_n - input_full_size + 1;
    max_n = input_full_size - 1;
  }

  // index 1 is margin pixel extents (how many pixels we hang over the edge)
  scanline_extents->edge_sizes[0] = left_margin;
  scanline_extents->edge_sizes[1] = right_margin;

  // index 2 is pixels read from the input
  scanline_extents->spans[0].n0 = min_n;
  scanline_extents->spans[0].n1 = max_n;
  scanline_extents->spans[0].pixel_offset_for_input = min_n;

  // default to no other input range
  scanline_extents->spans[1].n0 = 0;
  scanline_extents->spans[1].n1 = -1;
  scanline_extents->spans[1].pixel_offset_for_input = 0;

  // don't have to do edge calc for zero clamp
  if ( edge == STBIR_EDGE_ZERO )
    return;

  // convert margin pixels to the pixels within the input (min and max)
  for( j = -left_margin ; j < 0 ; j++ )
  {
      int p = stbir__edge_wrap( edge, j, input_full_size );
      if ( p < min_left )
        min_left = p;
      if ( p > max_left )
        max_left = p;
  }

  for( j = input_full_size ; j < (input_full_size + right_margin) ; j++ )
  {
      int p = stbir__edge_wrap( edge, j, input_full_size );
      if ( p < min_right )
        min_right = p;
      if ( p > max_right )
        max_right = p;
  }

  // merge the left margin pixel region if it connects within 4 pixels of main pixel region
  if ( min_left != 0x7fffffff )
  {
    if ( ( ( min_left <= min_n ) && ( ( max_left  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= min_n ) ) ||
         ( ( min_n <= min_left ) && ( ( max_n  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= max_left ) ) )
    {
      scanline_extents->spans[0].n0 = min_n = stbir__min( min_n, min_left );
      scanline_extents->spans[0].n1 = max_n = stbir__max( max_n, max_left );
      scanline_extents->spans[0].pixel_offset_for_input = min_n;
      left_margin = 0;
    }
  }

  // merge the right margin pixel region if it connects within 4 pixels of main pixel region
  if ( min_right != 0x7fffffff )
  {
    if ( ( ( min_right <= min_n ) && ( ( max_right  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= min_n ) ) ||
         ( ( min_n <= min_right ) && ( ( max_n  + STBIR__MERGE_RUNS_PIXEL_THRESHOLD ) >= max_right ) ) )
    {
      scanline_extents->spans[0].n0 = min_n = stbir__min( min_n, min_right );
      scanline_extents->spans[0].n1 = max_n = stbir__max( max_n, max_right );
      scanline_extents->spans[0].pixel_offset_for_input = min_n;
      right_margin = 0;
    }
  }

  STBIR_ASSERT( scanline_extents->conservative.n0 <= min_n );
  STBIR_ASSERT( scanline_extents->conservative.n1 >= max_n );

  // you get two ranges when you have the WRAP edge mode and you are doing just the a piece of the resize
  //   so you need to get a second run of pixels from the opposite side of the scanline (which you
  //   wouldn't need except for WRAP)


  // if we can't merge the min_left range, add it as a second range
  if ( ( left_margin ) && ( min_left != 0x7fffffff ) )
  {
    stbir__span * newspan = scanline_extents->spans + 1;
    STBIR_ASSERT( right_margin == 0 );
    if ( min_left < scanline_extents->spans[0].n0 )
    {
      scanline_extents->spans[1].pixel_offset_for_input = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n0 = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n1 = scanline_extents->spans[0].n1;
      --newspan;
    }
    newspan->pixel_offset_for_input = min_left;
    newspan->n0 = -left_margin;
    newspan->n1 = ( max_left - min_left ) - left_margin;
    scanline_extents->edge_sizes[0] = 0;  // don't need to copy the left margin, since we are directly decoding into the margin
  }
  // if we can't merge the min_right range, add it as a second range
  else  
  if ( ( right_margin ) && ( min_right != 0x7fffffff ) )
  {
    stbir__span * newspan = scanline_extents->spans + 1;
    if ( min_right < scanline_extents->spans[0].n0 )
    {
      scanline_extents->spans[1].pixel_offset_for_input = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n0 = scanline_extents->spans[0].n0;
      scanline_extents->spans[1].n1 = scanline_extents->spans[0].n1;
      --newspan;
    }
    newspan->pixel_offset_for_input = min_right;
    newspan->n0 = scanline_extents->spans[1].n1 + 1;
    newspan->n1 = scanline_extents->spans[1].n1 + 1 + ( max_right - min_right );
    scanline_extents->edge_sizes[1] = 0;  // don't need to copy the right margin, since we are directly decoding into the margin
  }

  // sort the spans into write output order
  if ( ( scanline_extents->spans[1].n1 > scanline_extents->spans[1].n0 ) && ( scanline_extents->spans[0].n0 > scanline_extents->spans[1].n0 ) )
  {
    stbir__span tspan = scanline_extents->spans[0];
    scanline_extents->spans[0] = scanline_extents->spans[1];
    scanline_extents->spans[1] = tspan;
  }
}

static void stbir__calculate_in_pixel_range( int * first_pixel, int * last_pixel, float out_pixel_center, float out_filter_radius, float inv_scale, float out_shift, int input_size, stbir_edge edge )
{
  int first, last;
  float out_pixel_influence_lowerbound = out_pixel_center - out_filter_radius;
  float out_pixel_influence_upperbound = out_pixel_center + out_filter_radius;

  float in_pixel_influence_lowerbound = (out_pixel_influence_lowerbound + out_shift) * inv_scale;
  float in_pixel_influence_upperbound = (out_pixel_influence_upperbound + out_shift) * inv_scale;

  first = (int)(STBIR_FLOORF(in_pixel_influence_lowerbound + 0.5f));
  last = (int)(STBIR_FLOORF(in_pixel_influence_upperbound - 0.5f));
  if ( last < first ) last = first; // point sample mode can span a value *right* at 0.5, and cause these to cross

  if ( edge == STBIR_EDGE_WRAP )
  {
    if ( first < -input_size )
      first = -input_size;
    if ( last >= (input_size*2))
      last = (input_size*2) - 1;
  }

  *first_pixel = first;
  *last_pixel = last;
}

static void stbir__calculate_coefficients_for_gather_upsample( float out_filter_radius, stbir__kernel_callback * kernel, stbir__scale_info * scale_info, int num_contributors, stbir__contributors* contributors, float* coefficient_group, int coefficient_width, stbir_edge edge, void * user_data )
{
  int n, end;
  float inv_scale = scale_info->inv_scale;
  float out_shift = scale_info->pixel_shift;
  int input_size  = scale_info->input_full_size;
  int numerator = scale_info->scale_numerator;
  int polyphase = ( ( scale_info->scale_is_rational ) && ( numerator < num_contributors ) );

  // Looping through out pixels
  end = num_contributors; if ( polyphase ) end = numerator;
  for (n = 0; n < end; n++)
  {
    int i;
    int last_non_zero;
    float out_pixel_center = (float)n + 0.5f;
    float in_center_of_out = (out_pixel_center + out_shift) * inv_scale;

    int in_first_pixel, in_last_pixel;

    stbir__calculate_in_pixel_range( &in_first_pixel, &in_last_pixel, out_pixel_center, out_filter_radius, inv_scale, out_shift, input_size, edge );

    // make sure we never generate a range larger than our precalculated coeff width
    //   this only happens in point sample mode, but it's a good safe thing to do anyway
    if ( ( in_last_pixel - in_first_pixel + 1 ) > coefficient_width )
      in_last_pixel = in_first_pixel + coefficient_width - 1;

    last_non_zero = -1;
    for (i = 0; i <= in_last_pixel - in_first_pixel; i++)
    {
      float in_pixel_center = (float)(i + in_first_pixel) + 0.5f;
      float coeff = kernel(in_center_of_out - in_pixel_center, inv_scale, user_data);

      // kill denormals
      if ( ( ( coeff < stbir__small_float ) && ( coeff > -stbir__small_float ) ) )
      {
        if ( i == 0 )  // if we're at the front, just eat zero contributors
        {
          STBIR_ASSERT ( ( in_last_pixel - in_first_pixel ) != 0 ); // there should be at least one contrib
          ++in_first_pixel;
          i--;
          continue;
        }
        coeff = 0;  // make sure is fully zero (should keep denormals away)
      }
      else
        last_non_zero = i;

      coefficient_group[i] = coeff;
    }

    in_last_pixel = last_non_zero+in_first_pixel; // kills trailing zeros
    contributors->n0 = in_first_pixel;
    contributors->n1 = in_last_pixel;

    STBIR_ASSERT(contributors->n1 >= contributors->n0);

    ++contributors;
    coefficient_group += coefficient_width;
  }
}

static void stbir__insert_coeff( stbir__contributors * contribs, float * coeffs, int new_pixel, float new_coeff, int max_width )
{
  if ( contribs->n1 < contribs->n0 ) // this first clause should never happen, but handle in case
  {
    contribs->n0 = contribs->n1 = new_pixel;
    coeffs[0] = new_coeff;
  }
  else if ( new_pixel <= contribs->n1 )  // before the end
  {
    if ( new_pixel < contribs->n0 ) // before the front?
    {
      if ( ( contribs->n1 - new_pixel + 1 ) <= max_width )
      { 
        int j, o = contribs->n0 - new_pixel;
        for ( j = contribs->n1 - contribs->n0 ; j >= 0 ; j-- )
          coeffs[ j + o ] = coeffs[ j ];
        for ( j = 1 ; j < o ; j++ )
          coeffs[ j ] = 0;
        coeffs[ 0 ] = new_coeff;
        contribs->n0 = new_pixel;
      }
    }
    else
    {
      // add new weight to existing coeff if already there
      coeffs[ new_pixel - contribs->n0 ] += new_coeff;
    }
  }
  else
  {
    if ( ( new_pixel - contribs->n0 + 1 ) <= max_width )
    {
      int j, e = new_pixel - contribs->n0;
      for( j = ( contribs->n1 - contribs->n0 ) + 1 ; j < e ; j++ ) // clear in-betweens coeffs if there are any
        coeffs[j] = 0;

      coeffs[ e ] = new_coeff;
      contribs->n1 = new_pixel;
    }
  }
}

static void stbir__calculate_out_pixel_range( int * first_pixel, int * last_pixel, float in_pixel_center, float in_pixels_radius, float scale, float out_shift, int out_size )
{
  float in_pixel_influence_lowerbound = in_pixel_center - in_pixels_radius;
  float in_pixel_influence_upperbound = in_pixel_center + in_pixels_radius;
  float out_pixel_influence_lowerbound = in_pixel_influence_lowerbound * scale - out_shift;
  float out_pixel_influence_upperbound = in_pixel_influence_upperbound * scale - out_shift;
  int out_first_pixel = (int)(STBIR_FLOORF(out_pixel_influence_lowerbound + 0.5f));
  int out_last_pixel = (int)(STBIR_FLOORF(out_pixel_influence_upperbound - 0.5f));

  if ( out_first_pixel < 0 )
    out_first_pixel = 0;
  if ( out_last_pixel >= out_size )
    out_last_pixel = out_size - 1;
  *first_pixel = out_first_pixel;
  *last_pixel = out_last_pixel;
}

static void stbir__calculate_coefficients_for_gather_downsample( int start, int end, float in_pixels_radius, stbir__kernel_callback * kernel, stbir__scale_info * scale_info, int coefficient_width, int num_contributors, stbir__contributors * contributors, float * coefficient_group, void * user_data )
{
  int in_pixel;
  int i;
  int first_out_inited = -1;
  float scale = scale_info->scale;
  float out_shift = scale_info->pixel_shift;
  int out_size = scale_info->output_sub_size;
  int numerator = scale_info->scale_numerator;
  int polyphase = ( ( scale_info->scale_is_rational ) && ( numerator < out_size ) );

  STBIR__UNUSED(num_contributors);

  // Loop through the input pixels
  for (in_pixel = start; in_pixel < end; in_pixel++)
  {
    float in_pixel_center = (float)in_pixel + 0.5f;
    float out_center_of_in = in_pixel_center * scale - out_shift;
    int out_first_pixel, out_last_pixel;

    stbir__calculate_out_pixel_range( &out_first_pixel, &out_last_pixel, in_pixel_center, in_pixels_radius, scale, out_shift, out_size );

    if ( out_first_pixel > out_last_pixel )
      continue;

    // clamp or exit if we are using polyphase filtering, and the limit is up
    if ( polyphase )
    {
      // when polyphase, you only have to do coeffs up to the numerator count
      if ( out_first_pixel == numerator )
        break;

      // don't do any extra work, clamp last pixel at numerator too
      if ( out_last_pixel >= numerator )
        out_last_pixel = numerator - 1;
    }

    for (i = 0; i <= out_last_pixel - out_first_pixel; i++)
    {
      float out_pixel_center = (float)(i + out_first_pixel) + 0.5f;
      float x = out_pixel_center - out_center_of_in;
      float coeff = kernel(x, scale, user_data) * scale;

      // kill the coeff if it's too small (avoid denormals)
      if ( ( ( coeff < stbir__small_float ) && ( coeff > -stbir__small_float ) ) )
        coeff = 0.0f;

      {
        int out = i + out_first_pixel;
        float * coeffs = coefficient_group + out * coefficient_width;
        stbir__contributors * contribs = contributors + out;

        // is this the first time this output pixel has been seen?  Init it.
        if ( out > first_out_inited )
        {
          STBIR_ASSERT( out == ( first_out_inited + 1 ) ); // ensure we have only advanced one at time
          first_out_inited = out;
          contribs->n0 = in_pixel;
          contribs->n1 = in_pixel;
          coeffs[0]  = coeff;
        }
        else
        {
          // insert on end (always in order)
          if ( coeffs[0] == 0.0f )  // if the first coefficent is zero, then zap it for this coeffs
          {
            STBIR_ASSERT( ( in_pixel - contribs->n0 ) == 1 ); // ensure that when we zap, we're at the 2nd pos
            contribs->n0 = in_pixel;
          }
          contribs->n1 = in_pixel;
          STBIR_ASSERT( ( in_pixel - contribs->n0 ) < coefficient_width );
          coeffs[in_pixel - contribs->n0]  = coeff;
        }
      }
    }
  }
}

#ifdef STBIR_RENORMALIZE_IN_FLOAT
#define STBIR_RENORM_TYPE float
#else
#define STBIR_RENORM_TYPE double
#endif

static void stbir__cleanup_gathered_coefficients( stbir_edge edge, stbir__filter_extent_info* filter_info, stbir__scale_info * scale_info, int num_contributors, stbir__contributors* contributors, float * coefficient_group, int coefficient_width )
{
  int input_size = scale_info->input_full_size;
  int input_last_n1 = input_size - 1;
  int n, end;
  int lowest = 0x7fffffff;
  int highest = -0x7fffffff;
  int widest = -1;
  int numerator = scale_info->scale_numerator;
  int denominator = scale_info->scale_denominator;
  int polyphase = ( ( scale_info->scale_is_rational ) && ( numerator < num_contributors ) );
  float * coeffs;
  stbir__contributors * contribs;

  // weight all the coeffs for each sample
  coeffs = coefficient_group;
  contribs = contributors;
  end = num_contributors; if ( polyphase ) end = numerator;
  for (n = 0; n < end; n++)
  {
    int i;
    STBIR_RENORM_TYPE filter_scale, total_filter = 0;
    int e;

    // add all contribs
    e = contribs->n1 - contribs->n0;
    for( i = 0 ; i <= e ; i++ )
    {
      total_filter += (STBIR_RENORM_TYPE) coeffs[i];
      STBIR_ASSERT( ( coeffs[i] >= -2.0f ) && ( coeffs[i] <= 2.0f )  ); // check for wonky weights
    }

    // rescale
    if ( ( total_filter < stbir__small_float ) && ( total_filter > -stbir__small_float ) )
    {
      // all coeffs are extremely small, just zero it
      contribs->n1 = contribs->n0;
      coeffs[0] = 0.0f;
    }
    else
    {
      // if the total isn't 1.0, rescale everything
      if ( ( total_filter < (1.0f-stbir__small_float) ) || ( total_filter > (1.0f+stbir__small_float) ) )
      {
        filter_scale = ((STBIR_RENORM_TYPE)1.0) / total_filter;

        // scale them all
        for (i = 0; i <= e; i++)
          coeffs[i] = (float) ( coeffs[i] * filter_scale );
      }
    }
    ++contribs;
    coeffs += coefficient_width;
  }

  // if we have a rational for the scale, we can exploit the polyphaseness to not calculate
  //   most of the coefficients, so we copy them here
  if ( polyphase )
  {
    stbir__contributors * prev_contribs = contributors;
    stbir__contributors * cur_contribs = contributors + numerator;

    for( n = numerator ; n < num_contributors ; n++ )
    {
      cur_contribs->n0 = prev_contribs->n0 + denominator;
      cur_contribs->n1 = prev_contribs->n1 + denominator;
      ++cur_contribs;
      ++prev_contribs;
    }
    stbir_overlapping_memcpy( coefficient_group + numerator * coefficient_width, coefficient_group, ( num_contributors - numerator ) * coefficient_width * sizeof( coeffs[ 0 ] ) );
  }

  coeffs = coefficient_group;
  contribs = contributors;

  for (n = 0; n < num_contributors; n++)
  {
    int i;

    // in zero edge mode, just remove out of bounds contribs completely (since their weights are accounted for now)
    if ( edge == STBIR_EDGE_ZERO )
    {
      // shrink the right side if necessary
      if ( contribs->n1 > input_last_n1 )
        contribs->n1 = input_last_n1;

      // shrink the left side
      if ( contribs->n0 < 0 )
      {
        int j, left, skips = 0;

        skips = -contribs->n0;
        contribs->n0 = 0;

        // now move down the weights
        left = contribs->n1 - contribs->n0 + 1;
        if ( left > 0 )
        {
          for( j = 0 ; j < left ; j++ )
            coeffs[ j ] = coeffs[ j + skips ];
        }
      }
    }
    else if ( ( edge == STBIR_EDGE_CLAMP ) || ( edge == STBIR_EDGE_REFLECT ) )
    {
      // for clamp and reflect, calculate the true inbounds position (based on edge type) and just add that to the existing weight

      // right hand side first
      if ( contribs->n1 > input_last_n1 )
      {
        int start = contribs->n0;
        int endi = contribs->n1;
        contribs->n1 = input_last_n1;
        for( i = input_size; i <= endi; i++ )
          stbir__insert_coeff( contribs, coeffs, stbir__edge_wrap_slow[edge]( i, input_size ), coeffs[i-start], coefficient_width );
      }

      // now check left hand edge
      if ( contribs->n0 < 0 )
      {
        int save_n0;
        float save_n0_coeff;
        float * c = coeffs - ( contribs->n0 + 1 );

        // reinsert the coeffs with it reflected or clamped (insert accumulates, if the coeffs exist)
        for( i = -1 ; i > contribs->n0 ; i-- )
          stbir__insert_coeff( contribs, coeffs, stbir__edge_wrap_slow[edge]( i, input_size ), *c--, coefficient_width );
        save_n0 = contribs->n0;
        save_n0_coeff = c[0]; // save it, since we didn't do the final one (i==n0), because there might be too many coeffs to hold (before we resize)!

        // now slide all the coeffs down (since we have accumulated them in the positive contribs) and reset the first contrib
        contribs->n0 = 0;
        for(i = 0 ; i <= contribs->n1 ; i++ )
          coeffs[i] = coeffs[i-save_n0];

        // now that we have shrunk down the contribs, we insert the first one safely
        stbir__insert_coeff( contribs, coeffs, stbir__edge_wrap_slow[edge]( save_n0, input_size ), save_n0_coeff, coefficient_width );
      }
    }

    if ( contribs->n0 <= contribs->n1 )
    {
      int diff = contribs->n1 - contribs->n0 + 1;
      while ( diff && ( coeffs[ diff-1 ] == 0.0f ) )
        --diff;

      contribs->n1 = contribs->n0 + diff - 1;

      if ( contribs->n0 <= contribs->n1 )
      {
        if ( contribs->n0 < lowest )
          lowest = contribs->n0;
        if ( contribs->n1 > highest )
          highest = contribs->n1;
        if ( diff > widest )
          widest = diff;
      }

      // re-zero out unused coefficients (if any)
      for( i = diff ; i < coefficient_width ; i++ )
        coeffs[i] = 0.0f;
    }

    ++contribs;
    coeffs += coefficient_width;
  }
  filter_info->lowest = lowest;
  filter_info->highest = highest;
  filter_info->widest = widest;
}

#undef STBIR_RENORM_TYPE 

static int stbir__pack_coefficients( int num_contributors, stbir__contributors* contributors, float * coefficents, int coefficient_width, int widest, int row0, int row1 ) 
{
  #define STBIR_MOVE_1( dest, src ) { STBIR_NO_UNROLL(dest); ((stbir_uint32*)(dest))[0] = ((stbir_uint32*)(src))[0]; }
  #define STBIR_MOVE_2( dest, src ) { STBIR_NO_UNROLL(dest); ((stbir_uint64*)(dest))[0] = ((stbir_uint64*)(src))[0]; }
  #ifdef STBIR_SIMD
  #define STBIR_MOVE_4( dest, src ) { stbir__simdf t; STBIR_NO_UNROLL(dest); stbir__simdf_load( t, src ); stbir__simdf_store( dest, t ); }
  #else
  #define STBIR_MOVE_4( dest, src ) { STBIR_NO_UNROLL(dest); ((stbir_uint64*)(dest))[0] = ((stbir_uint64*)(src))[0]; ((stbir_uint64*)(dest))[1] = ((stbir_uint64*)(src))[1]; }
  #endif

  int row_end = row1 + 1;
  STBIR__UNUSED( row0 ); // only used in an assert

  if ( coefficient_width != widest )
  {
    float * pc = coefficents;
    float * coeffs = coefficents;
    float * pc_end = coefficents + num_contributors * widest;
    switch( widest )
    {
      case 1:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_1( pc, coeffs );
          ++pc;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 2:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_2( pc, coeffs );
          pc += 2;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 3:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_2( pc, coeffs );
          STBIR_MOVE_1( pc+2, coeffs+2 );
          pc += 3;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 4:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          pc += 4;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 5:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_1( pc+4, coeffs+4 );
          pc += 5;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 6:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_2( pc+4, coeffs+4 );
          pc += 6;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 7:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_2( pc+4, coeffs+4 );
          STBIR_MOVE_1( pc+6, coeffs+6 );
          pc += 7;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 8:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          pc += 8;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 9:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_1( pc+8, coeffs+8 );
          pc += 9;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 10:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_2( pc+8, coeffs+8 );
          pc += 10;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 11:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_2( pc+8, coeffs+8 );
          STBIR_MOVE_1( pc+10, coeffs+10 );
          pc += 11;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      case 12:
        STBIR_NO_UNROLL_LOOP_START
        do {
          STBIR_MOVE_4( pc, coeffs );
          STBIR_MOVE_4( pc+4, coeffs+4 );
          STBIR_MOVE_4( pc+8, coeffs+8 );
          pc += 12;
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
      default:
        STBIR_NO_UNROLL_LOOP_START
        do {
          float * copy_end = pc + widest - 4;
          float * c = coeffs;
          do {
            STBIR_NO_UNROLL( pc );
            STBIR_MOVE_4( pc, c );
            pc += 4;
            c += 4;
          } while ( pc <= copy_end );
          copy_end += 4;
          STBIR_NO_UNROLL_LOOP_START
          while ( pc < copy_end )
          {
            STBIR_MOVE_1( pc, c );
            ++pc; ++c;
          }
          coeffs += coefficient_width;
        } while ( pc < pc_end );
        break;
    }
  }

  // some horizontal routines read one float off the end (which is then masked off), so put in a sentinel so we don't read an snan or denormal
  coefficents[ widest * num_contributors ] = 8888.0f;

  // the minimum we might read for unrolled filters widths is 12. So, we need to
  //   make sure we never read outside the decode buffer, by possibly moving
  //   the sample area back into the scanline, and putting zeros weights first.
  // we start on the right edge and check until we're well past the possible
  //   clip area (2*widest).
  {
    stbir__contributors * contribs = contributors + num_contributors - 1;
    float * coeffs = coefficents + widest * ( num_contributors - 1 );

    // go until no chance of clipping (this is usually less than 8 lops)
    while ( ( contribs >= contributors ) && ( ( contribs->n0 + widest*2 ) >= row_end ) )
    {
      // might we clip??
      if ( ( contribs->n0 + widest ) > row_end )
      {
        int stop_range = widest;

        // if range is larger than 12, it will be handled by generic loops that can terminate on the exact length
        //   of this contrib n1, instead of a fixed widest amount - so calculate this
        if ( widest > 12 )
        {
          int mod;

          // how far will be read in the n_coeff loop (which depends on the widest count mod4);
          mod = widest & 3;
          stop_range = ( ( ( contribs->n1 - contribs->n0 + 1 ) - mod + 3 ) & ~3 ) + mod;

          // the n_coeff loops do a minimum amount of coeffs, so factor that in!
          if ( stop_range < ( 8 + mod ) ) stop_range = 8 + mod;
        }

        // now see if we still clip with the refined range
        if ( ( contribs->n0 + stop_range ) > row_end )
        {
          int new_n0 = row_end - stop_range;
          int num = contribs->n1 - contribs->n0 + 1;
          int backup = contribs->n0 - new_n0;
          float * from_co = coeffs + num - 1;
          float * to_co = from_co + backup;

          STBIR_ASSERT( ( new_n0 >= row0 ) && ( new_n0 < contribs->n0 ) );

          // move the coeffs over
          while( num )
          {
            *to_co-- = *from_co--;
            --num;
          }
          // zero new positions
          while ( to_co >= coeffs )
            *to_co-- = 0;
          // set new start point
          contribs->n0 = new_n0;
          if ( widest > 12 )
          {
            int mod;

            // how far will be read in the n_coeff loop (which depends on the widest count mod4);
            mod = widest & 3;
            stop_range = ( ( ( contribs->n1 - contribs->n0 + 1 ) - mod + 3 ) & ~3 ) + mod;

            // the n_coeff loops do a minimum amount of coeffs, so factor that in!
            if ( stop_range < ( 8 + mod ) ) stop_range = 8 + mod;
          }
        }
      }
      --contribs;
      coeffs -= widest;
    }
  }

  return widest;
  #undef STBIR_MOVE_1
  #undef STBIR_MOVE_2
  #undef STBIR_MOVE_4
}

static void stbir__calculate_filters( stbir__sampler * samp, stbir__sampler * other_axis_for_pivot, void * user_data STBIR_ONLY_PROFILE_BUILD_GET_INFO )
{
  int n;
  float scale = samp->scale_info.scale;
  stbir__kernel_callback * kernel = samp->filter_kernel;
  stbir__support_callback * support = samp->filter_support;
  float inv_scale = samp->scale_info.inv_scale;
  int input_full_size = samp->scale_info.input_full_size;
  int gather_num_contributors = samp->num_contributors;
  stbir__contributors* gather_contributors = samp->contributors;
  float * gather_coeffs = samp->coefficients;
  int gather_coefficient_width = samp->coefficient_width;

  switch ( samp->is_gather )
  {
    case 1: // gather upsample
    {
      float out_pixels_radius = support(inv_scale,user_data) * scale;

      stbir__calculate_coefficients_for_gather_upsample( out_pixels_radius, kernel, &samp->scale_info, gather_num_contributors, gather_contributors, gather_coeffs, gather_coefficient_width, samp->edge, user_data );

      STBIR_PROFILE_BUILD_START( cleanup );
      stbir__cleanup_gathered_coefficients( samp->edge, &samp->extent_info, &samp->scale_info, gather_num_contributors, gather_contributors, gather_coeffs, gather_coefficient_width );
      STBIR_PROFILE_BUILD_END( cleanup );
    }
    break;

    case 0: // scatter downsample (only on vertical)
    case 2: // gather downsample
    {
      float in_pixels_radius = support(scale,user_data) * inv_scale;
      int filter_pixel_margin = samp->filter_pixel_margin;
      int input_end = input_full_size + filter_pixel_margin;

      // if this is a scatter, we do a downsample gather to get the coeffs, and then pivot after
      if ( !samp->is_gather )
      {
        // check if we are using the same gather downsample on the horizontal as this vertical,
        //   if so, then we don't have to generate them, we can just pivot from the horizontal.
        if ( other_axis_for_pivot )
        {
          gather_contributors = other_axis_for_pivot->contributors;
          gather_coeffs = other_axis_for_pivot->coefficients;
          gather_coefficient_width = other_axis_for_pivot->coefficient_width;
          gather_num_contributors = other_axis_for_pivot->num_contributors;
          samp->extent_info.lowest = other_axis_for_pivot->extent_info.lowest;
          samp->extent_info.highest = other_axis_for_pivot->extent_info.highest;
          samp->extent_info.widest = other_axis_for_pivot->extent_info.widest;
          goto jump_right_to_pivot;
        }

        gather_contributors = samp->gather_prescatter_contributors;
        gather_coeffs = samp->gather_prescatter_coefficients;
        gather_coefficient_width = samp->gather_prescatter_coefficient_width;
        gather_num_contributors = samp->gather_prescatter_num_contributors;
      }

      stbir__calculate_coefficients_for_gather_downsample( -filter_pixel_margin, input_end, in_pixels_radius, kernel, &samp->scale_info, gather_coefficient_width, gather_num_contributors, gather_contributors, gather_coeffs, user_data );

      STBIR_PROFILE_BUILD_START( cleanup );
      stbir__cleanup_gathered_coefficients( samp->edge, &samp->extent_info, &samp->scale_info, gather_num_contributors, gather_contributors, gather_coeffs, gather_coefficient_width );
      STBIR_PROFILE_BUILD_END( cleanup );

      if ( !samp->is_gather )
      {
        // if this is a scatter (vertical only), then we need to pivot the coeffs
        stbir__contributors * scatter_contributors;
        int highest_set;

        jump_right_to_pivot:

        STBIR_PROFILE_BUILD_START( pivot );

        highest_set = (-filter_pixel_margin) - 1;
        for (n = 0; n < gather_num_contributors; n++)
        {
          int k;
          int gn0 = gather_contributors->n0, gn1 = gather_contributors->n1;
          int scatter_coefficient_width = samp->coefficient_width;
          float * scatter_coeffs = samp->coefficients + ( gn0 + filter_pixel_margin ) * scatter_coefficient_width;
          float * g_coeffs = gather_coeffs;
          scatter_contributors = samp->contributors + ( gn0 + filter_pixel_margin );

          for (k = gn0 ; k <= gn1 ; k++ )
          {
            float gc = *g_coeffs++;
            
            // skip zero and denormals - must skip zeros to avoid adding coeffs beyond scatter_coefficient_width
            //   (which happens when pivoting from horizontal, which might have dummy zeros)
            if ( ( ( gc >= stbir__small_float ) || ( gc <= -stbir__small_float ) ) )
            {
              if ( ( k > highest_set ) || ( scatter_contributors->n0 > scatter_contributors->n1 ) )
              {
                {
                  // if we are skipping over several contributors, we need to clear the skipped ones
                  stbir__contributors * clear_contributors = samp->contributors + ( highest_set + filter_pixel_margin + 1);
                  while ( clear_contributors < scatter_contributors )
                  {
                    clear_contributors->n0 = 0;
                    clear_contributors->n1 = -1;
                    ++clear_contributors;
                  }
                }
                scatter_contributors->n0 = n;
                scatter_contributors->n1 = n;
                scatter_coeffs[0]  = gc;
                highest_set = k;
              }
              else
              {
                stbir__insert_coeff( scatter_contributors, scatter_coeffs, n, gc, scatter_coefficient_width );
              }
              STBIR_ASSERT( ( scatter_contributors->n1 - scatter_contributors->n0 + 1 ) <= scatter_coefficient_width );
            }
            ++scatter_contributors;
            scatter_coeffs += scatter_coefficient_width;
          }

          ++gather_contributors;
          gather_coeffs += gather_coefficient_width;
        }

        // now clear any unset contribs
        {
          stbir__contributors * clear_contributors = samp->contributors + ( highest_set + filter_pixel_margin + 1);
          stbir__contributors * end_contributors = samp->contributors + samp->num_contributors;
          while ( clear_contributors < end_contributors )
          {
            clear_contributors->n0 = 0;
            clear_contributors->n1 = -1;
            ++clear_contributors;
          }
        }

        STBIR_PROFILE_BUILD_END( pivot );
      }
    }
    break;
  }
}
