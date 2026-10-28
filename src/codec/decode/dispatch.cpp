#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// Whether the next decodes of this thread flip the image vertically (set by the caller, per call).
static thread_local int flip_on_load = 0;

void set_flip_on_load(int flip) { flip_on_load = flip; }

static void *load_main(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc)
{
   memset(ri, 0, sizeof(*ri)); // make sure it's initialized if we add new fields
   ri->bits_per_channel = 8; // default is 8 so most paths don't have to be changed
   ri->channel_order = order_rgb; // all current input & output are this, but this is here so we can add BGR order
   ri->num_channels = 0;

   // test the formats with a very explicit header first (at least a FOURCC
   // or distinctive magic number first)
   if (png::test(s))  return png::load(s,x,y,comp,req_comp, ri);
   if (bmp::test(s))  return bmp::load(s,x,y,comp,req_comp, ri);
   if (gif::test(s))  return gif::load(s,x,y,comp,req_comp, ri);
   if (psd::test(s))  return psd::load(s,x,y,comp,req_comp, ri, bpc);
   if (pic::test(s))  return pic::load(s,x,y,comp,req_comp, ri);

   // then the formats that can end up attempting to load with just 1 or 2
   // bytes matching expectations; these are prone to false positives, so
   // try them later
   if (jpeg::test(s)) return jpeg::load(s,x,y,comp,req_comp, ri);
   if (pnm::test(s))  return pnm::load(s,x,y,comp,req_comp, ri);

   if (hdr::test(s)) {
      float *hdr = hdr::load(s, x,y,comp,req_comp, ri);
      return hdr_to_ldr(hdr, *x, *y, req_comp ? req_comp : *comp);
   }

   // test tga last because it's a crappy test!
   if (tga::test(s))
      return tga::load(s,x,y,comp,req_comp, ri);

   return fail_bytes("unknown image type", "Image not of any known type, or corrupt");
}

static byte *convert_16_to_8(std::uint16_t *orig, int w, int h, int channels)
{
   int i;
   int img_len = w * h * channels;
   byte *reduced;

   reduced = (byte *) alloc(img_len);
   if (reduced == NULL) return fail_bytes("outofmem", "Out of memory");

   for (i = 0; i < img_len; ++i)
      reduced[i] = (byte)((orig[i] >> 8) & 0xFF); // top half of each byte is sufficient approx of 16->8 bit scaling

   std::free(orig);
   return reduced;
}


static void vertical_flip(void *image, int w, int h, int bytes_per_pixel)
{
   int row;
   size_t bytes_per_row = (size_t)w * bytes_per_pixel;
   byte temp[2048];
   byte *bytes = (byte *)image;

   for (row = 0; row < (h>>1); row++) {
      byte *row0 = bytes + row*bytes_per_row;
      byte *row1 = bytes + (h - row - 1)*bytes_per_row;
      // swap row0 with row1
      size_t bytes_left = bytes_per_row;
      while (bytes_left) {
         size_t bytes_copy = (bytes_left < sizeof(temp)) ? bytes_left : sizeof(temp);
         memcpy(temp, row0, bytes_copy);
         memcpy(row0, row1, bytes_copy);
         memcpy(row1, temp, bytes_copy);
         row0 += bytes_copy;
         row1 += bytes_copy;
         bytes_left -= bytes_copy;
      }
   }
}

static unsigned char *load_and_postprocess_8bit(stream *s, int *x, int *y, int *comp, int req_comp)
{
   result_info ri;
   void *result = load_main(s, x, y, comp, req_comp, &ri, 8);

   if (result == NULL)
      return NULL;

   // it is the responsibility of the loaders to make sure we get either 8 or 16 bit.
   assert(ri.bits_per_channel == 8 || ri.bits_per_channel == 16);

   if (ri.bits_per_channel != 8) {
      result = convert_16_to_8((std::uint16_t *) result, *x, *y, req_comp == 0 ? *comp : req_comp);
      ri.bits_per_channel = 8;
   }

   // @TODO: move convert_format to here

   if (flip_on_load) {
      int channels = req_comp ? req_comp : *comp;
      vertical_flip(result, *x, *y, channels * sizeof(byte));
   }

   return (unsigned char *) result;
}






byte *load_from_memory(byte const *buffer, int len, int *x, int *y, int *comp, int req_comp)
{
   stream s(buffer, len);
   return load_and_postprocess_8bit(&s,x,y,comp,req_comp);
}




// these is-hdr-or-not is defined independent of whether STBI_NO_LINEAR is
// defined, for API simplicity; if STBI_NO_LINEAR is defined, it always
// reports false!




static int info_main(stream *s, int *x, int *y, int *comp)
{
   if (jpeg::info(s, x, y, comp)) return 1;

   if (png::info(s, x, y, comp))  return 1;

   if (gif::info(s, x, y, comp))  return 1;

   if (bmp::info(s, x, y, comp))  return 1;

   if (psd::info(s, x, y, comp))  return 1;

   if (pic::info(s, x, y, comp))  return 1;

   if (pnm::info(s, x, y, comp))  return 1;

   if (hdr::info(s, x, y, comp))  return 1;

   // test tga last because it's a crappy test!
   if (tga::info(s, x, y, comp))
       return 1;
   return fail("unknown image type", "Image not of any known type, or corrupt");
}



int info_from_memory(byte const *buffer, int len, int *x, int *y, int *comp)
{
   stream s(buffer, len);
   return info_main(&s,x,y,comp);
}




}  // namespace stb::codec::detail
