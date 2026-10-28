#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// Targa Truevision - TGA
// by Jonathan Dummer
// returns ch_rgb or whatever, 0 on error
static int tga_get_comp(int bits_per_pixel, int is_grey, int* is_rgb16)
{
   // only RGB or RGBA (incl. 16bit) or grey allowed
   if (is_rgb16) *is_rgb16 = 0;
   switch(bits_per_pixel) {
      case 8:  return ch_grey;
      case 16: if(is_grey) return ch_grey_alpha;
               // fallthrough
      case 15: if(is_rgb16) *is_rgb16 = 1;
               return ch_rgb;
      case 24: // fallthrough
      case 32: return bits_per_pixel/8;
      default: return 0;
   }
}

int tga::info(stream *s, int *x, int *y, int *comp)
{
    int tga_w, tga_h, tga_comp, tga_image_type, tga_bits_per_pixel, tga_colormap_bpp;
    int sz, tga_colormap_type;
    s->get8();                   // discard Offset
    tga_colormap_type = s->get8(); // colormap type
    if( tga_colormap_type > 1 ) {
        s->rewind();
        return 0;      // only RGB or indexed allowed
    }
    tga_image_type = s->get8(); // image type
    if ( tga_colormap_type == 1 ) { // colormapped (paletted) image
        if (tga_image_type != 1 && tga_image_type != 9) {
            s->rewind();
            return 0;
        }
        s->skip(4);       // skip index of first colormap entry and number of entries
        sz = s->get8();    //   check bits per palette color entry
        if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) {
            s->rewind();
            return 0;
        }
        s->skip(4);       // skip image x and y origin
        tga_colormap_bpp = sz;
    } else { // "normal" image w/o colormap - only RGB or grey allowed, +/- RLE
        if ( (tga_image_type != 2) && (tga_image_type != 3) && (tga_image_type != 10) && (tga_image_type != 11) ) {
            s->rewind();
            return 0; // only RGB or grey allowed, +/- RLE
        }
        s->skip(9); // skip colormap specification and image x/y origin
        tga_colormap_bpp = 0;
    }
    tga_w = s->get16le();
    if( tga_w < 1 ) {
        s->rewind();
        return 0;   // test width
    }
    tga_h = s->get16le();
    if( tga_h < 1 ) {
        s->rewind();
        return 0;   // test height
    }
    tga_bits_per_pixel = s->get8(); // bits per pixel
    s->get8(); // ignore alpha bits
    if (tga_colormap_bpp != 0) {
        if((tga_bits_per_pixel != 8) && (tga_bits_per_pixel != 16)) {
            // when using a colormap, tga_bits_per_pixel is the size of the indexes
            // I don't think anything but 8 or 16bit indexes makes sense
            s->rewind();
            return 0;
        }
        tga_comp = tga_get_comp(tga_colormap_bpp, 0, NULL);
    } else {
        tga_comp = tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3) || (tga_image_type == 11), NULL);
    }
    if(!tga_comp) {
      s->rewind();
      return 0;
    }
    if (x) *x = tga_w;
    if (y) *y = tga_h;
    if (comp) *comp = tga_comp;
    return 1;                   // seems to have passed everything
}

int tga::test(stream *s)
{
   int res = 0;
   int sz, tga_color_type;
   s->get8();      //   discard Offset
   tga_color_type = s->get8();   //   color type
   if ( tga_color_type > 1 ) goto errorEnd;   //   only RGB or indexed allowed
   sz = s->get8();   //   image type
   if ( tga_color_type == 1 ) { // colormapped (paletted) image
      if (sz != 1 && sz != 9) goto errorEnd; // colortype 1 demands image type 1 or 9
      s->skip(4);       // skip index of first colormap entry and number of entries
      sz = s->get8();    //   check bits per palette color entry
      if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;
      s->skip(4);       // skip image x and y origin
   } else { // "normal" image w/o colormap
      if ( (sz != 2) && (sz != 3) && (sz != 10) && (sz != 11) ) goto errorEnd; // only RGB or grey allowed, +/- RLE
      s->skip(9); // skip colormap specification and image x/y origin
   }
   if ( s->get16le() < 1 ) goto errorEnd;      //   test width
   if ( s->get16le() < 1 ) goto errorEnd;      //   test height
   sz = s->get8();   //   bits per pixel
   if ( (tga_color_type == 1) && (sz != 8) && (sz != 16) ) goto errorEnd; // for colormapped images, bpp is size of an index
   if ( (sz != 8) && (sz != 15) && (sz != 16) && (sz != 24) && (sz != 32) ) goto errorEnd;

   res = 1; // if we got this far, everything's good and we can return 1 instead of 0

errorEnd:
   s->rewind();
   return res;
}

// read 16bit value and convert to 24bit RGB
static void tga_read_rgb16(stream *s, byte* out)
{
   std::uint16_t px = (std::uint16_t)s->get16le();
   std::uint16_t fiveBitMask = 31;
   // we have 3 channels with 5bits each
   int r = (px >> 10) & fiveBitMask;
   int g = (px >> 5) & fiveBitMask;
   int b = px & fiveBitMask;
   // Note that this saves the data in RGB(A) order, so it doesn't need to be swapped later
   out[0] = (byte)((r * 255)/31);
   out[1] = (byte)((g * 255)/31);
   out[2] = (byte)((b * 255)/31);

   // some people claim that the most significant bit might be used for alpha
   // (possibly if an alpha-bit is set in the "image descriptor byte")
   // but that only made 16bit test images completely translucent..
   // so let's treat all 15 and 16bit TGAs as RGB with no alpha.
}

void * tga::load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri)
{
   //   read in the TGA header stuff
   int tga_offset = s->get8();
   int tga_indexed = s->get8();
   int tga_image_type = s->get8();
   int tga_is_RLE = 0;
   int tga_palette_start = s->get16le();
   int tga_palette_len = s->get16le();
   int tga_palette_bits = s->get8();
   int tga_x_origin = s->get16le();
   int tga_y_origin = s->get16le();
   int tga_width = s->get16le();
   int tga_height = s->get16le();
   int tga_bits_per_pixel = s->get8();
   int tga_comp, tga_rgb16=0;
   int tga_inverted = s->get8();
   // int tga_alpha_bits = tga_inverted & 15; // the 4 lowest bits - unused (useless?)
   //   image data
   unsigned char *tga_data;
   unsigned char *tga_palette = NULL;
   int i, j;
   unsigned char raw_data[4] = {0};
   int RLE_count = 0;
   int RLE_repeating = 0;
   int read_next_pixel = 1;
   (void)(ri);
   (void)(tga_x_origin); // @TODO
   (void)(tga_y_origin); // @TODO

   if (tga_height > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");
   if (tga_width > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");

   //   do a tiny bit of precessing
   if ( tga_image_type >= 8 )
   {
      tga_image_type -= 8;
      tga_is_RLE = 1;
   }
   tga_inverted = 1 - ((tga_inverted >> 5) & 1);

   //   If I'm paletted, then I'll use the number of bits from the palette
   if ( tga_indexed ) tga_comp = tga_get_comp(tga_palette_bits, 0, &tga_rgb16);
   else tga_comp = tga_get_comp(tga_bits_per_pixel, (tga_image_type == 3), &tga_rgb16);

   if(!tga_comp) // shouldn't really happen, tga_test() should have ensured basic consistency
      return fail_bytes("bad format", "Can't find out TGA pixelformat");

   //   tga info
   *x = tga_width;
   *y = tga_height;
   if (comp) *comp = tga_comp;

   if (!mad3sizes_valid(tga_width, tga_height, tga_comp, 0))
      return fail_bytes("too large", "Corrupt TGA");

   tga_data = (unsigned char*)alloc_mad3(tga_width, tga_height, tga_comp, 0);
   if (!tga_data) return fail_bytes("outofmem", "Out of memory");

   // skip to the data's starting position (offset usually = 0)
   s->skip(tga_offset );

   if ( !tga_indexed && !tga_is_RLE && !tga_rgb16 ) {
      for (i=0; i < tga_height; ++i) {
         int row = tga_inverted ? tga_height -i - 1 : i;
         byte *tga_row = tga_data + row*tga_width*tga_comp;
         s->getn(tga_row, tga_width * tga_comp);
      }
   } else  {
      //   do I need to load a palette?
      if ( tga_indexed)
      {
         if (tga_palette_len == 0) {  /* you have to have at least one entry! */
            std::free(tga_data);
            return fail_bytes("bad palette", "Corrupt TGA");
         }

         //   any data to skip? (offset usually = 0)
         s->skip(tga_palette_start );
         //   load the palette
         tga_palette = (unsigned char*)alloc_mad2(tga_palette_len, tga_comp, 0);
         if (!tga_palette) {
            std::free(tga_data);
            return fail_bytes("outofmem", "Out of memory");
         }
         if (tga_rgb16) {
            byte *pal_entry = tga_palette;
            assert(tga_comp == ch_rgb);
            for (i=0; i < tga_palette_len; ++i) {
               tga_read_rgb16(s, pal_entry);
               pal_entry += tga_comp;
            }
         } else if (!s->getn(tga_palette, tga_palette_len * tga_comp)) {
               std::free(tga_data);
               std::free(tga_palette);
               return fail_bytes("bad palette", "Corrupt TGA");
         }
      }
      //   load the data
      for (i=0; i < tga_width * tga_height; ++i)
      {
         //   if I'm in RLE mode, do I need to get a RLE png_chunk?
         if ( tga_is_RLE )
         {
            if ( RLE_count == 0 )
            {
               //   yep, get the next byte as a RLE command
               int RLE_cmd = s->get8();
               RLE_count = 1 + (RLE_cmd & 127);
               RLE_repeating = RLE_cmd >> 7;
               read_next_pixel = 1;
            } else if ( !RLE_repeating )
            {
               read_next_pixel = 1;
            }
         } else
         {
            read_next_pixel = 1;
         }
         //   OK, if I need to read a pixel, do it now
         if ( read_next_pixel )
         {
            //   load however much data we did have
            if ( tga_indexed )
            {
               // read in index, then perform the lookup
               int pal_idx = (tga_bits_per_pixel == 8) ? s->get8() : s->get16le();
               if ( pal_idx >= tga_palette_len ) {
                  // invalid index
                  pal_idx = 0;
               }
               pal_idx *= tga_comp;
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = tga_palette[pal_idx+j];
               }
            } else if(tga_rgb16) {
               assert(tga_comp == ch_rgb);
               tga_read_rgb16(s, raw_data);
            } else {
               //   read in the data raw
               for (j = 0; j < tga_comp; ++j) {
                  raw_data[j] = s->get8();
               }
            }
            //   clear the reading flag for the next pixel
            read_next_pixel = 0;
         } // end of reading a pixel

         // copy data
         for (j = 0; j < tga_comp; ++j)
           tga_data[i*tga_comp+j] = raw_data[j];

         //   in case we're in RLE mode, keep counting down
         --RLE_count;
      }
      //   do I need to invert the image?
      if ( tga_inverted )
      {
         for (j = 0; j*2 < tga_height; ++j)
         {
            int index1 = j * tga_width * tga_comp;
            int index2 = (tga_height - 1 - j) * tga_width * tga_comp;
            for (i = tga_width * tga_comp; i > 0; --i)
            {
               unsigned char temp = tga_data[index1];
               tga_data[index1] = tga_data[index2];
               tga_data[index2] = temp;
               ++index1;
               ++index2;
            }
         }
      }
      //   clear my palette, if I had one
      if ( tga_palette != NULL )
      {
         std::free( tga_palette );
      }
   }

   // swap RGB - if the source data was RGB16, it already is in the right order
   if (tga_comp >= 3 && !tga_rgb16)
   {
      unsigned char* tga_pixel = tga_data;
      for (i=0; i < tga_width * tga_height; ++i)
      {
         unsigned char temp = tga_pixel[0];
         tga_pixel[0] = tga_pixel[2];
         tga_pixel[2] = temp;
         tga_pixel += tga_comp;
      }
   }

   // convert to target component count
   if (req_comp && req_comp != tga_comp)
      tga_data = convert_format(tga_data, tga_comp, req_comp, tga_width, tga_height);

   //   the things I do to get rid of an error message, and yet keep
   //   Microsoft's C compilers happy... [8^(
   tga_palette_start = tga_palette_len = tga_palette_bits =
         tga_x_origin = tga_y_origin = 0;
   (void)(tga_palette_start);
   //   OK, done
   return tga_data;
}

}  // namespace stb::codec::detail
