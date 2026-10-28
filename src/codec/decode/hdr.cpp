#include "codec/decode/formats.hpp"

namespace stb::codec::detail {



static float h2l_gamma_i=1.0f/2.2f, h2l_scale_i=1.0f;



byte *hdr_to_ldr(float   *data, int x, int y, int comp)
{
   int i,k,n;
   byte *output;
   if (!data) return NULL;
   output = (byte *) alloc_mad3(x, y, comp, 0);
   if (output == NULL) { std::free(data); return fail_bytes("outofmem", "Out of memory"); }
   // compute number of non-alpha components
   if (comp & 1) n = comp; else n = comp-1;
   for (i=0; i < x*y; ++i) {
      for (k=0; k < n; ++k) {
         float z = (float) pow(data[i*comp+k]*h2l_scale_i, h2l_gamma_i) * 255 + 0.5f;
         if (z < 0) z = 0;
         if (z > 255) z = 255;
         output[i*comp + k] = (byte) static_cast<int>(z);
      }
      if (k < comp) {
         float z = data[i*comp+k] * 255 + 0.5f;
         if (z < 0) z = 0;
         if (z > 255) z = 255;
         output[i*comp + k] = (byte) static_cast<int>(z);
      }
   }
   std::free(data);
   return output;
}

// *************************************************************************************************
// Radiance RGBE HDR loader
// originally by Nicolas Schulz
static int hdr_test_core(stream *s, const char *signature)
{
   int i;
   for (i=0; signature[i]; ++i)
      if (s->get8() != signature[i])
          return 0;
   s->rewind();
   return 1;
}

int hdr::test(stream* s)
{
   int r = hdr_test_core(s, "#?RADIANCE\n");
   s->rewind();
   if(!r) {
       r = hdr_test_core(s, "#?RGBE\n");
       s->rewind();
   }
   return r;
}

constexpr int hdr_buflen = 1024;
static char *hdr_gettoken(stream *z, char *buffer)
{
   int len=0;
   char c = '\0';

   c = (char) z->get8();

   while (!z->at_eof() && c != '\n') {
      buffer[len++] = c;
      if (len == hdr_buflen-1) {
         // flush to end of line
         while (!z->at_eof() && z->get8() != '\n')
            ;
         break;
      }
      c = (char) z->get8();
   }

   buffer[len] = 0;
   return buffer;
}

static void hdr_convert(float *output, byte *input, int req_comp)
{
   if ( input[3] != 0 ) {
      float f1;
      // Exponent
      f1 = (float) ldexp(1.0f, input[3] - (int)(128 + 8));
      if (req_comp <= 2)
         output[0] = (input[0] + input[1] + input[2]) * f1 / 3;
      else {
         output[0] = input[0] * f1;
         output[1] = input[1] * f1;
         output[2] = input[2] * f1;
      }
      if (req_comp == 2) output[1] = 1;
      if (req_comp == 4) output[3] = 1;
   } else {
      switch (req_comp) {
         case 4: output[3] = 1; /* fallthrough */
         case 3: output[0] = output[1] = output[2] = 0;
                 break;
         case 2: output[1] = 1; /* fallthrough */
         case 1: output[0] = 0;
                 break;
      }
   }
}

float * hdr::load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri)
{
   char buffer[hdr_buflen];
   char *token;
   int valid = 0;
   int width, height;
   byte *scanline;
   float *hdr_data;
   int len;
   unsigned char count, value;
   int i, j, k, c1,c2, z;
   const char *headerToken;
   (void)(ri);

   // Check identifier
   headerToken = hdr_gettoken(s,buffer);
   if (strcmp(headerToken, "#?RADIANCE") != 0 && strcmp(headerToken, "#?RGBE") != 0)
      return fail_float("not HDR", "Corrupt HDR image");

   // Parse header
   for(;;) {
      token = hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid)    return fail_float("unsupported format", "Unsupported HDR format");

   // Parse width and height
   // can't use sscanf() if we're not using stdio!
   token = hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3))  return fail_float("unsupported data layout", "Unsupported HDR format");
   token += 3;
   height = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3))  return fail_float("unsupported data layout", "Unsupported HDR format");
   token += 3;
   width = (int) strtol(token, NULL, 10);

   if (height > max_dimensions) return fail_float("too large","Very large image (corrupt?)");
   if (width > max_dimensions) return fail_float("too large","Very large image (corrupt?)");

   *x = width;
   *y = height;

   if (comp) *comp = 3;
   if (req_comp == 0) req_comp = 3;

   if (!mad4sizes_valid(width, height, req_comp, sizeof(float), 0))
      return fail_float("too large", "HDR image is too large");

   // Read data
   hdr_data = (float *) alloc_mad4(width, height, req_comp, sizeof(float), 0);
   if (!hdr_data)
      return fail_float("outofmem", "Out of memory");

   // Load image data
   // image data is stored as some number of sca
   if ( width < 8 || width >= 32768) {
      // Read flat data
      for (j=0; j < height; ++j) {
         for (i=0; i < width; ++i) {
            byte rgbe[4];
           main_decode_loop:
            s->getn(rgbe, 4);
            hdr_convert(hdr_data + j * width * req_comp + i * req_comp, rgbe, req_comp);
         }
      }
   } else {
      // Read RLE-encoded data
      scanline = NULL;

      for (j = 0; j < height; ++j) {
         c1 = s->get8();
         c2 = s->get8();
         len = s->get8();
         if (c1 != 2 || c2 != 2 || (len & 0x80)) {
            // not run-length encoded, so we have to actually use THIS data as a decoded
            // pixel (note this can't be a valid pixel--one of RGB must be >= 128)
            byte rgbe[4];
            rgbe[0] = (byte) c1;
            rgbe[1] = (byte) c2;
            rgbe[2] = (byte) len;
            rgbe[3] = (byte) s->get8();
            hdr_convert(hdr_data, rgbe, req_comp);
            i = 1;
            j = 0;
            std::free(scanline);
            goto main_decode_loop; // yes, this makes no sense
         }
         len <<= 8;
         len |= s->get8();
         if (len != width) { std::free(hdr_data); std::free(scanline); return fail_float("invalid decoded scanline length", "corrupt HDR"); }
         if (scanline == NULL) {
            scanline = (byte *) alloc_mad2(width, 4, 0);
            if (!scanline) {
               std::free(hdr_data);
               return fail_float("outofmem", "Out of memory");
            }
         }

         for (k = 0; k < 4; ++k) {
            int nleft;
            i = 0;
            while ((nleft = width - i) > 0) {
               count = s->get8();
               if (count > 128) {
                  // Run
                  value = s->get8();
                  count -= 128;
                  if ((count == 0) || (count > nleft)) { std::free(hdr_data); std::free(scanline); return fail_float("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = value;
               } else {
                  // Dump
                  if ((count == 0) || (count > nleft)) { std::free(hdr_data); std::free(scanline); return fail_float("corrupt", "bad RLE data in HDR"); }
                  for (z = 0; z < count; ++z)
                     scanline[i++ * 4 + k] = s->get8();
               }
            }
         }
         for (i=0; i < width; ++i)
            hdr_convert(hdr_data+(j*width + i)*req_comp, scanline + i*4, req_comp);
      }
      if (scanline)
         std::free(scanline);
   }

   return hdr_data;
}

int hdr::info(stream *s, int *x, int *y, int *comp)
{
   char buffer[hdr_buflen];
   char *token;
   int valid = 0;
   int dummy;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   if (hdr::test(s) == 0) {
       s->rewind();
       return 0;
   }

   for(;;) {
      token = hdr_gettoken(s,buffer);
      if (token[0] == 0) break;
      if (strcmp(token, "FORMAT=32-bit_rle_rgbe") == 0) valid = 1;
   }

   if (!valid) {
       s->rewind();
       return 0;
   }
   token = hdr_gettoken(s,buffer);
   if (strncmp(token, "-Y ", 3)) {
       s->rewind();
       return 0;
   }
   token += 3;
   *y = (int) strtol(token, &token, 10);
   while (*token == ' ') ++token;
   if (strncmp(token, "+X ", 3)) {
       s->rewind();
       return 0;
   }
   token += 3;
   *x = (int) strtol(token, NULL, 10);
   *comp = 3;
   return 1;
}

}  // namespace stb::codec::detail
