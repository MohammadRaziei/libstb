#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// *************************************************************************************************
// Photoshop PSD loader -- PD by Thatcher Ulrich, integration by Nicolas Schulz, tweaked by STB

int psd::test(stream *s)
{
   int r = (s->get32be() == 0x38425053);
   s->rewind();
   return r;
}

static int psd_decode_rle(stream *s, byte *p, int pixelCount)
{
   int count, nleft, len;

   count = 0;
   while ((nleft = pixelCount - count) > 0) {
      len = s->get8();
      if (len == 128) {
         // No-op.
      } else if (len < 128) {
         // Copy next len+1 bytes literally.
         len++;
         if (len > nleft) return 0; // corrupt data
         count += len;
         while (len) {
            *p = s->get8();
            p += 4;
            len--;
         }
      } else if (len > 128) {
         byte   val;
         // Next -len+1 bytes in the dest are replicated from next source byte.
         // (Interpret len as a negative 8-bit int.)
         len = 257 - len;
         if (len > nleft) return 0; // corrupt data
         val = s->get8();
         count += len;
         while (len) {
            *p = val;
            p += 4;
            len--;
         }
      }
   }

   return 1;
}

void * psd::load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc)
{
   int pixelCount;
   int channelCount, compression;
   int channel, i;
   int bitdepth;
   int w,h;
   byte *out;
   (void)(ri);

   // Check identifier
   if (s->get32be() != 0x38425053)   // "8BPS"
      return fail_bytes("not PSD", "Corrupt PSD image");

   // Check file type version.
   if (s->get16be() != 1)
      return fail_bytes("wrong version", "Unsupported version of PSD image");

   // Skip 6 reserved bytes.
   s->skip(6 );

   // Read the number of channels (R, G, B, A, etc).
   channelCount = s->get16be();
   if (channelCount < 0 || channelCount > 16)
      return fail_bytes("wrong channel count", "Unsupported number of channels in PSD image");

   // Read the rows and columns of the image.
   h = s->get32be();
   w = s->get32be();

   if (h > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");
   if (w > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");

   // Make sure the depth is 8 bits.
   bitdepth = s->get16be();
   if (bitdepth != 8 && bitdepth != 16)
      return fail_bytes("unsupported bit depth", "PSD bit depth is not 8 or 16 bit");

   // Make sure the color mode is RGB.
   // Valid options are:
   //   0: Bitmap
   //   1: Grayscale
   //   2: Indexed color
   //   3: RGB color
   //   4: CMYK color
   //   7: Multichannel
   //   8: Duotone
   //   9: Lab color
   if (s->get16be() != 3)
      return fail_bytes("wrong color format", "PSD is not in RGB color format");

   // Skip the Mode Data.  (It's the palette for indexed color; other info for other modes.)
   s->skip(s->get32be() );

   // Skip the image resources.  (resolution, pen tool paths, etc)
   s->skip(s->get32be() );

   // Skip the reserved data.
   s->skip(s->get32be() );

   // Find out if the data is compressed.
   // Known values:
   //   0: no compression
   //   1: RLE compressed
   compression = s->get16be();
   if (compression > 1)
      return fail_bytes("bad compression", "PSD has an unknown compression format");

   // Check size
   if (!mad3sizes_valid(4, w, h, 0))
      return fail_bytes("too large", "Corrupt PSD");

   // Create the destination image.

   if (!compression && bitdepth == 16 && bpc == 16) {
      out = (byte *) alloc_mad3(8, w, h, 0);
      ri->bits_per_channel = 16;
   } else
      out = (byte *) alloc(4 * w*h);

   if (!out) return fail_bytes("outofmem", "Out of memory");
   pixelCount = w*h;

   // Initialize the data to zero.
   //memset( out, 0, pixelCount * 4 );

   // Finally, the image data.
   if (compression) {
      // RLE as used by .PSD and .TIFF
      // Loop until you get the number of unpacked bytes you are expecting:
      //     Read the next source byte into n.
      //     If n is between 0 and 127 inclusive, copy the next n+1 bytes literally.
      //     Else if n is between -127 and -1 inclusive, copy the next byte -n+1 times.
      //     Else if n is 128, noop.
      // Endloop

      // The RLE-compressed data is preceded by a 2-byte data count for each row in the data,
      // which we're going to just skip.
      s->skip(h * channelCount * 2 );

      // Read the RLE data by channel.
      for (channel = 0; channel < 4; channel++) {
         byte *p;

         p = out+channel;
         if (channel >= channelCount) {
            // Fill this channel with default data.
            for (i = 0; i < pixelCount; i++, p += 4)
               *p = (channel == 3 ? 255 : 0);
         } else {
            // Read the RLE data.
            if (!psd_decode_rle(s, p, pixelCount)) {
               std::free(out);
               return fail_bytes("corrupt", "bad RLE data");
            }
         }
      }

   } else {
      // We're at the raw image data.  It's each channel in order (Red, Green, Blue, Alpha, ...)
      // where each channel consists of an 8-bit (or 16-bit) value for each pixel in the image.

      // Read the data by channel.
      for (channel = 0; channel < 4; channel++) {
         if (channel >= channelCount) {
            // Fill this channel with default data.
            if (bitdepth == 16 && bpc == 16) {
               std::uint16_t *q = ((std::uint16_t *) out) + channel;
               std::uint16_t val = channel == 3 ? 65535 : 0;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = val;
            } else {
               byte *p = out+channel;
               byte val = channel == 3 ? 255 : 0;
               for (i = 0; i < pixelCount; i++, p += 4)
                  *p = val;
            }
         } else {
            if (ri->bits_per_channel == 16) {    // output bpc
               std::uint16_t *q = ((std::uint16_t *) out) + channel;
               for (i = 0; i < pixelCount; i++, q += 4)
                  *q = (std::uint16_t) s->get16be();
            } else {
               byte *p = out+channel;
               if (bitdepth == 16) {  // input bpc
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = (byte) (s->get16be() >> 8);
               } else {
                  for (i = 0; i < pixelCount; i++, p += 4)
                     *p = s->get8();
               }
            }
         }
      }
   }

   // remove weird white matte from PSD
   if (channelCount >= 4) {
      if (ri->bits_per_channel == 16) {
         for (i=0; i < w*h; ++i) {
            std::uint16_t *pixel = (std::uint16_t *) out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 65535) {
               float a = pixel[3] / 65535.0f;
               float ra = 1.0f / a;
               float inv_a = 65535.0f * (1 - ra);
               pixel[0] = (std::uint16_t) (pixel[0]*ra + inv_a);
               pixel[1] = (std::uint16_t) (pixel[1]*ra + inv_a);
               pixel[2] = (std::uint16_t) (pixel[2]*ra + inv_a);
            }
         }
      } else {
         for (i=0; i < w*h; ++i) {
            unsigned char *pixel = out + 4*i;
            if (pixel[3] != 0 && pixel[3] != 255) {
               float a = pixel[3] / 255.0f;
               float ra = 1.0f / a;
               float inv_a = 255.0f * (1 - ra);
               pixel[0] = (unsigned char) (pixel[0]*ra + inv_a);
               pixel[1] = (unsigned char) (pixel[1]*ra + inv_a);
               pixel[2] = (unsigned char) (pixel[2]*ra + inv_a);
            }
         }
      }
   }

   // convert to desired output format
   if (req_comp && req_comp != 4) {
      if (ri->bits_per_channel == 16)
         out = (byte *) convert_format16((std::uint16_t *) out, 4, req_comp, w, h);
      else
         out = convert_format(out, 4, req_comp, w, h);
      if (out == NULL) return out; // convert_format frees input on failure
   }

   if (comp) *comp = 4;
   *y = h;
   *x = w;

   return out;
}

int psd::info(stream *s, int *x, int *y, int *comp)
{
   int channelCount, dummy, depth;
   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;
   if (s->get32be() != 0x38425053) {
       s->rewind();
       return 0;
   }
   if (s->get16be() != 1) {
       s->rewind();
       return 0;
   }
   s->skip(6);
   channelCount = s->get16be();
   if (channelCount < 0 || channelCount > 16) {
       s->rewind();
       return 0;
   }
   *y = s->get32be();
   *x = s->get32be();
   depth = s->get16be();
   if (depth != 8 && depth != 16) {
       s->rewind();
       return 0;
   }
   if (s->get16be() != 3) {
       s->rewind();
       return 0;
   }
   *comp = 4;
   return 1;
}


}  // namespace stb::codec::detail
