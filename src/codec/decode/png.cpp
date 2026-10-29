#include "codec/decode/inflate.hpp"
#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// public domain "baseline" PNG decoder   v0.10  Sean Barrett 2006-11-18
//    simple implementation
//      - only 8-bit samples
//      - no CRC checking
//      - allocates lots of intermediate memory
//        - avoids problem of streaming data between subsystems
//        - avoids explicit window management
//    performance
//      - uses stb_zlib, a PD zlib implementation with fast huffman decoding

struct png_chunk
{
   std::uint32_t length;
   std::uint32_t type;
};

static png_chunk get_chunk_header(stream *s)
{
   png_chunk c;
   c.length = s->get32be();
   c.type   = s->get32be();
   return c;
}

static int check_png_header(stream *s)
{
   static const byte png_sig[8] = { 137,80,78,71,13,10,26,10 };
   int i;
   for (i=0; i < 8; ++i)
      if (s->get8() != png_sig[i]) return fail("bad png sig","Not a PNG");
   return 1;
}

class png_decoder {
public:
   stream *s_;
   byte *idata_, *expanded_, *out_;
   int depth_;

    int create_png_image_raw(byte *raw, std::uint32_t raw_len, int out_n, std::uint32_t x, std::uint32_t y, int depth, int color);
    int create_png_image(byte *image_data, std::uint32_t image_data_len, int out_n, int depth, int color, int interlaced);
    int compute_transparency(byte tc[3], int out_n);
    int compute_transparency16(std::uint16_t tc[3], int out_n);
    int expand_png_palette(byte *palette, int len, int pal_img_n);
    int parse_png_file(int scan, int req_comp);
    void * do_png(int *x, int *y, int *n, int req_comp, result_info *ri);
    int info_raw(int *x, int *y, int *comp);
};



enum {
   F_none=0,
   F_sub=1,
   F_up=2,
   F_avg=3,
   F_paeth=4,
   // synthetic filter used for first scanline to avoid needing a dummy row of 0s
   F_avg_first
};

static byte first_row_filter[5] =
{
   F_none,
   F_sub,
   F_none,
   F_avg_first,
   F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static int paeth(int a, int b, int c)
{
   // This formulation looks very different from the reference in the PNG spec, but is
   // actually equivalent and has favorable data dependencies and admits straightforward
   // generation of branch-free code, which helps performance significantly.
   int thresh = c*3 - (a + b);
   int lo = a < b ? a : b;
   int hi = a < b ? b : a;
   int t0 = (hi <= thresh) ? lo : c;
   int t1 = (thresh <= lo) ? hi : t0;
   return t1;
}

static const byte depth_scale_table[9] = { 0, 0xff, 0x55, 0, 0x11, 0,0,0, 0x01 };

// adds an extra all-255 alpha channel
// dest == src is legal
// img_n must be 1 or 3
static void create_png_alpha_expand8(byte *dest, byte *src, std::uint32_t x, int img_n)
{
   int i;
   // must process data backwards since we allow dest==src
   if (img_n == 1) {
      for (i=x-1; i >= 0; --i) {
         dest[i*2+1] = 255;
         dest[i*2+0] = src[i];
      }
   } else {
      assert(img_n == 3);
      for (i=x-1; i >= 0; --i) {
         dest[i*4+3] = 255;
         dest[i*4+2] = src[i*3+2];
         dest[i*4+1] = src[i*3+1];
         dest[i*4+0] = src[i*3+0];
      }
   }
}

// create the png data from post-deflated data
int png_decoder::create_png_image_raw(byte *raw, std::uint32_t raw_len, int out_n, std::uint32_t x, std::uint32_t y, int depth, int color)
{
   int bytes = (depth == 16 ? 2 : 1);
   std::uint32_t i,j,stride = x*out_n*bytes;
   std::uint32_t img_len, img_width_bytes;
   byte *filter_buf;
   int all_ok = 1;
   int k;
   int img_n = s_->img_n; // copy it into this local for later

   int output_bytes = out_n*bytes;
   int filter_bytes = img_n*bytes;
   int width = x;

   assert(out_n == s->img_n || out_n == s->img_n+1);
   out_ = (byte *) alloc_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
   if (!out_) return fail("outofmem", "Out of memory");

   // note: error exits here don't need to clean up out individually,
   // do_png always does on error.
   if (!mad3sizes_valid(img_n, x, depth, 7)) return fail("too large", "Corrupt PNG");
   img_width_bytes = (((img_n * x * depth) + 7) >> 3);
   if (!mad2sizes_valid(img_width_bytes, y, img_width_bytes)) return fail("too large", "Corrupt PNG");
   img_len = (img_width_bytes + 1) * y;

   // we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
   // but issue #276 reported this PNG in the wild that had extra data at the end (all zeros),
   // so just check for raw_len < img_len always.
   if (raw_len < img_len) return fail("not enough pixels","Corrupt PNG");

   // Allocate two scan lines worth of filter workspace buffer.
   filter_buf = (byte *) alloc_mad2(img_width_bytes, 2, 0);
   if (!filter_buf) return fail("outofmem", "Out of memory");

   // Filtering for low-bit-depth images
   if (depth < 8) {
      filter_bytes = 1;
      width = img_width_bytes;
   }

   for (j=0; j < y; ++j) {
      // cur/prior filter buffers alternate
      byte *cur = filter_buf + (j & 1)*img_width_bytes;
      byte *prior = filter_buf + (~j & 1)*img_width_bytes;
      byte *dest = out_ + stride*j;
      int nk = width * filter_bytes;
      int filter = *raw++;

      // check filter type
      if (filter > 4) {
         all_ok = fail("invalid filter","Corrupt PNG");
         break;
      }

      // if first row, use special filter that doesn't sample previous row
      if (j == 0) filter = first_row_filter[filter];

      // perform actual filtering
      switch (filter) {
      case F_none:
         memcpy(cur, raw, nk);
         break;
      case F_sub:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = truncate_to_byte(raw[k] + cur[k-filter_bytes]);
         break;
      case F_up:
         for (k = 0; k < nk; ++k)
            cur[k] = truncate_to_byte(raw[k] + prior[k]);
         break;
      case F_avg:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = truncate_to_byte(raw[k] + (prior[k]>>1));
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = truncate_to_byte(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
         break;
      case F_paeth:
         for (k = 0; k < filter_bytes; ++k)
            cur[k] = truncate_to_byte(raw[k] + prior[k]); // prior[k] == paeth(0,prior[k],0)
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = truncate_to_byte(raw[k] + paeth(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
         break;
      case F_avg_first:
         memcpy(cur, raw, filter_bytes);
         for (k = filter_bytes; k < nk; ++k)
            cur[k] = truncate_to_byte(raw[k] + (cur[k-filter_bytes] >> 1));
         break;
      }

      raw += nk;

      // expand decoded bits in cur to dest, also adding an extra alpha channel if desired
      if (depth < 8) {
         byte scale = (color == 0) ? depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
         byte *in = cur;
         byte *out = dest;
         byte inb = 0;
         std::uint32_t nsmp = x*img_n;

         // expand bits to bytes first
         if (depth == 4) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 1) == 0) inb = *in++;
               *out++ = scale * (inb >> 4);
               inb <<= 4;
            }
         } else if (depth == 2) {
            for (i=0; i < nsmp; ++i) {
               if ((i & 3) == 0) inb = *in++;
               *out++ = scale * (inb >> 6);
               inb <<= 2;
            }
         } else {
            assert(depth == 1);
            for (i=0; i < nsmp; ++i) {
               if ((i & 7) == 0) inb = *in++;
               *out++ = scale * (inb >> 7);
               inb <<= 1;
            }
         }

         // insert alpha=255 values if desired
         if (img_n != out_n)
            create_png_alpha_expand8(dest, dest, x, img_n);
      } else if (depth == 8) {
         if (img_n == out_n)
            memcpy(dest, cur, x*img_n);
         else
            create_png_alpha_expand8(dest, cur, x, img_n);
      } else if (depth == 16) {
         // convert the image data from big-endian to platform-native
         std::uint16_t *dest16 = (std::uint16_t*)dest;
         std::uint32_t nsmp = x*img_n;

         if (img_n == out_n) {
            for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
               *dest16 = (cur[0] << 8) | cur[1];
         } else {
            assert(img_n+1 == out_n);
            if (img_n == 1) {
               for (i = 0; i < x; ++i, dest16 += 2, cur += 2) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = 0xffff;
               }
            } else {
               assert(img_n == 3);
               for (i = 0; i < x; ++i, dest16 += 4, cur += 6) {
                  dest16[0] = (cur[0] << 8) | cur[1];
                  dest16[1] = (cur[2] << 8) | cur[3];
                  dest16[2] = (cur[4] << 8) | cur[5];
                  dest16[3] = 0xffff;
               }
            }
         }
      }
   }

   std::free(filter_buf);
   if (!all_ok) return 0;

   return 1;
}

int png_decoder::create_png_image(byte *image_data, std::uint32_t image_data_len, int out_n, int depth, int color, int interlaced)
{
   int bytes = (depth == 16 ? 2 : 1);
   int out_bytes = out_n * bytes;
   byte *final;
   int p;
   if (!interlaced)
      return create_png_image_raw(image_data, image_data_len, out_n, s_->img_x, s_->img_y, depth, color);

   // de-interlacing
   final = (byte *) alloc_mad3(s_->img_x, s_->img_y, out_bytes, 0);
   if (!final) return fail("outofmem", "Out of memory");
   for (p=0; p < 7; ++p) {
      int xorig[] = { 0,4,0,2,0,1,0 };
      int yorig[] = { 0,0,4,0,2,0,1 };
      int xspc[]  = { 8,8,4,4,2,2,1 };
      int yspc[]  = { 8,8,8,4,4,2,2 };
      int i,j,x,y;
      // pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
      x = (s_->img_x - xorig[p] + xspc[p]-1) / xspc[p];
      y = (s_->img_y - yorig[p] + yspc[p]-1) / yspc[p];
      if (x && y) {
         std::uint32_t img_len = ((((s_->img_n * x * depth) + 7) >> 3) + 1) * y;
         if (!create_png_image_raw(image_data, image_data_len, out_n, x, y, depth, color)) {
            std::free(final);
            return 0;
         }
         for (j=0; j < y; ++j) {
            for (i=0; i < x; ++i) {
               int out_y = j*yspc[p]+yorig[p];
               int out_x = i*xspc[p]+xorig[p];
               memcpy(final + out_y*s_->img_x*out_bytes + out_x*out_bytes,
                      out_ + (j*x+i)*out_bytes, out_bytes);
            }
         }
         std::free(out_);
         image_data += img_len;
         image_data_len -= img_len;
      }
   }
   out_ = final;

   return 1;
}

int png_decoder::compute_transparency(byte tc[3], int out_n)
{
   std::uint32_t i, pixel_count = s_->img_x * s_->img_y;
   byte *p = out_;

   // compute color-based transparency, assuming we've
   // already got 255 as the alpha value in the output
   assert(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i=0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 255);
         p += 2;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

int png_decoder::compute_transparency16(std::uint16_t tc[3], int out_n)
{
   std::uint32_t i, pixel_count = s_->img_x * s_->img_y;
   std::uint16_t *p = (std::uint16_t*) out_;

   // compute color-based transparency, assuming we've
   // already got 65535 as the alpha value in the output
   assert(out_n == 2 || out_n == 4);

   if (out_n == 2) {
      for (i = 0; i < pixel_count; ++i) {
         p[1] = (p[0] == tc[0] ? 0 : 65535);
         p += 2;
      }
   } else {
      for (i = 0; i < pixel_count; ++i) {
         if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
         p += 4;
      }
   }
   return 1;
}

int png_decoder::expand_png_palette(byte *palette, int len, int pal_img_n)
{
   std::uint32_t i, pixel_count = s_->img_x * s_->img_y;
   byte *p, *temp_out, *orig = out_;

   p = (byte *) alloc_mad2(pixel_count, pal_img_n, 0);
   if (p == NULL) return fail("outofmem", "Out of memory");

   // between here and free(out) below, exitting would leak
   temp_out = p;

   if (pal_img_n == 3) {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p += 3;
      }
   } else {
      for (i=0; i < pixel_count; ++i) {
         int n = orig[i]*4;
         p[0] = palette[n  ];
         p[1] = palette[n+1];
         p[2] = palette[n+2];
         p[3] = palette[n+3];
         p += 4;
      }
   }
   std::free(out_);
   out_ = temp_out;

   (void)(len);

   return 1;
}

constexpr unsigned png_type(unsigned a, unsigned b, unsigned c, unsigned d) { return (a << 24) + (b << 16) + (c << 8) + d; }

int png_decoder::parse_png_file(int scan, int req_comp)
{
   byte palette[1024], pal_img_n=0;
   byte has_trans=0, tc[3]={0};
   std::uint16_t tc16[3];
   std::uint32_t ioff=0, idata_limit=0, i, pal_len=0;
   int first=1,k,interlace=0, color=0, is_iphone=0;

   expanded_ = NULL;
   idata_ = NULL;
   out_ = NULL;

   if (!check_png_header(s_)) return 0;

   if (scan == scan_type) return 1;

   for (;;) {
      png_chunk c = get_chunk_header(s_);
      switch (c.type) {
         case png_type('C','g','B','I'):
            is_iphone = 1;
            s_->skip(c.length);
            break;
         case png_type('I','H','D','R'): {
            int comp,filter;
            if (!first) return fail("multiple IHDR","Corrupt PNG");
            first = 0;
            if (c.length != 13) return fail("bad IHDR len","Corrupt PNG");
            s_->img_x = s_->get32be();
            s_->img_y = s_->get32be();
            if (s_->img_y > max_dimensions) return fail("too large","Very large image (corrupt?)");
            if (s_->img_x > max_dimensions) return fail("too large","Very large image (corrupt?)");
            depth_ = s_->get8();  if (depth_ != 1 && depth_ != 2 && depth_ != 4 && depth_ != 8 && depth_ != 16)  return fail("1/2/4/8/16-bit only","PNG not supported: 1/2/4/8/16-bit only");
            color = s_->get8();  if (color > 6)         return fail("bad ctype","Corrupt PNG");
            if (color == 3 && depth_ == 16)                  return fail("bad ctype","Corrupt PNG");
            if (color == 3) pal_img_n = 3; else if (color & 1) return fail("bad ctype","Corrupt PNG");
            comp  = s_->get8();  if (comp) return fail("bad comp method","Corrupt PNG");
            filter= s_->get8();  if (filter) return fail("bad filter method","Corrupt PNG");
            interlace = s_->get8(); if (interlace>1) return fail("bad interlace method","Corrupt PNG");
            if (!s_->img_x || !s_->img_y) return fail("0-pixel image","Corrupt PNG");
            if (!pal_img_n) {
               s_->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
               if ((1 << 30) / s_->img_x / s_->img_n < s_->img_y) return fail("too large", "Image too large to decode");
            } else {
               // if paletted, then pal_n is our final components, and
               // img_n is # components to decompress/filter.
               s_->img_n = 1;
               if ((1 << 30) / s_->img_x / 4 < s_->img_y) return fail("too large","Corrupt PNG");
            }
            // even with SCAN_header, have to scan to see if we have a tRNS
            break;
         }

         case png_type('P','L','T','E'):  {
            if (first) return fail("first not IHDR", "Corrupt PNG");
            if (c.length > 256*3) return fail("invalid PLTE","Corrupt PNG");
            pal_len = c.length / 3;
            if (pal_len * 3 != c.length) return fail("invalid PLTE","Corrupt PNG");
            for (i=0; i < pal_len; ++i) {
               palette[i*4+0] = s_->get8();
               palette[i*4+1] = s_->get8();
               palette[i*4+2] = s_->get8();
               palette[i*4+3] = 255;
            }
            break;
         }

         case png_type('t','R','N','S'): {
            if (first) return fail("first not IHDR", "Corrupt PNG");
            if (idata_) return fail("tRNS after IDAT","Corrupt PNG");
            if (pal_img_n) {
               if (scan == scan_header) { s_->img_n = 4; return 1; }
               if (pal_len == 0) return fail("tRNS before PLTE","Corrupt PNG");
               if (c.length > pal_len) return fail("bad tRNS len","Corrupt PNG");
               pal_img_n = 4;
               for (i=0; i < c.length; ++i)
                  palette[i*4+3] = s_->get8();
            } else {
               if (!(s_->img_n & 1)) return fail("tRNS with alpha","Corrupt PNG");
               if (c.length != (std::uint32_t) s_->img_n*2) return fail("bad tRNS len","Corrupt PNG");
               has_trans = 1;
               // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
               if (scan == scan_header) { ++s_->img_n; return 1; }
               if (depth_ == 16) {
                  for (k = 0; k < s_->img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
                     tc16[k] = (std::uint16_t)s_->get16be(); // copy the values as-is
               } else {
                  for (k = 0; k < s_->img_n && k < 3; ++k)
                     tc[k] = (byte)(s_->get16be() & 255) * depth_scale_table[depth_]; // non 8-bit images will be larger
               }
            }
            break;
         }

         case png_type('I','D','A','T'): {
            if (first) return fail("first not IHDR", "Corrupt PNG");
            if (pal_img_n && !pal_len) return fail("no PLTE","Corrupt PNG");
            if (scan == scan_header) {
               // header scan definitely stops at first IDAT
               if (pal_img_n)
                  s_->img_n = pal_img_n;
               return 1;
            }
            if (c.length > (1u << 30)) return fail("IDAT size limit", "IDAT section larger than 2^30 bytes");
            if ((int)(ioff + c.length) < (int)ioff) return 0;
            if (ioff + c.length > idata_limit) {
               std::uint32_t idata_limit_old = idata_limit;
               byte *p;
               if (idata_limit == 0) idata_limit = c.length > 4096 ? c.length : 4096;
               while (ioff + c.length > idata_limit)
                  idata_limit *= 2;
               (void)(idata_limit_old);
               p = (byte *) realloc_sized(idata_, idata_limit_old, idata_limit); if (p == NULL) return fail("outofmem", "Out of memory");
               idata_ = p;
            }
            if (!s_->getn(idata_+ioff,c.length)) return fail("outofdata","Corrupt PNG");
            ioff += c.length;
            break;
         }

         case png_type('I','E','N','D'): {
            std::uint32_t raw_len, bpl;
            if (first) return fail("first not IHDR", "Corrupt PNG");
            if (scan != scan_load) return 1;
            if (idata_ == NULL) return fail("no IDAT","Corrupt PNG");
            // initial guess for decoded data size to avoid unnecessary reallocs
            bpl = (s_->img_x * depth_ + 7) / 8; // bytes per line, per component
            raw_len = bpl * s_->img_y * s_->img_n /* pixels */ + s_->img_y /* filter mode per row */;
            expanded_ = (byte *) zlib_decode_malloc((char *) idata_, ioff, raw_len, (int *) &raw_len, !is_iphone);
            if (expanded_ == NULL) return 0; // zlib should set error
            std::free(idata_); idata_ = NULL;
            if ((req_comp == s_->img_n+1 && req_comp != 3 && !pal_img_n) || has_trans)
               s_->img_out_n = s_->img_n+1;
            else
               s_->img_out_n = s_->img_n;
            if (!create_png_image(expanded_, raw_len, s_->img_out_n, depth_, color, interlace)) return 0;
            if (has_trans) {
               if (depth_ == 16) {
                  if (!compute_transparency16(tc16, s_->img_out_n)) return 0;
               } else {
                  if (!compute_transparency(tc, s_->img_out_n)) return 0;
               }
            }
            if (pal_img_n) {
               // pal_img_n == 3 or 4
               s_->img_n = pal_img_n; // record the actual colors we had
               s_->img_out_n = pal_img_n;
               if (req_comp >= 3) s_->img_out_n = req_comp;
               if (!expand_png_palette(palette, pal_len, s_->img_out_n))
                  return 0;
            } else if (has_trans) {
               // non-paletted image with tRNS -> source image has (constant) alpha
               ++s_->img_n;
            }
            std::free(expanded_); expanded_ = NULL;
            // end of PNG chunk, read and skip CRC
            s_->get32be();
            return 1;
         }

         default:
            // if critical, fail
            if (first) return fail("first not IHDR", "Corrupt PNG");
            if ((c.type & (1 << 29)) == 0) {
               // not threadsafe
               static char invalid_chunk[] = "XXXX PNG chunk not known";
               invalid_chunk[0] = truncate_to_byte(c.type >> 24);
               invalid_chunk[1] = truncate_to_byte(c.type >> 16);
               invalid_chunk[2] = truncate_to_byte(c.type >>  8);
               invalid_chunk[3] = truncate_to_byte(c.type >>  0);
               return fail(invalid_chunk, "PNG not supported: unknown PNG chunk type");
            }
            s_->skip(c.length);
            break;
      }
      // end of PNG chunk, read and skip CRC
      s_->get32be();
   }
}

void * png_decoder::do_png(int *x, int *y, int *n, int req_comp, result_info *ri)
{
   void *result=NULL;
   if (req_comp < 0 || req_comp > 4) return fail_bytes("bad req_comp", "Internal error");
   if (parse_png_file(scan_load, req_comp)) {
      if (depth_ <= 8)
         ri->bits_per_channel = 8;
      else if (depth_ == 16)
         ri->bits_per_channel = 16;
      else
         return fail_bytes("bad bits_per_channel", "PNG not supported: unsupported color depth");
      result = out_;
      out_ = NULL;
      if (req_comp && req_comp != s_->img_out_n) {
         if (ri->bits_per_channel == 8)
            result = convert_format((unsigned char *) result, s_->img_out_n, req_comp, s_->img_x, s_->img_y);
         else
            result = convert_format16((std::uint16_t *) result, s_->img_out_n, req_comp, s_->img_x, s_->img_y);
         s_->img_out_n = req_comp;
         if (result == NULL) return result;
      }
      *x = s_->img_x;
      *y = s_->img_y;
      if (n) *n = s_->img_n;
   }
   std::free(out_);      out_      = NULL;
   std::free(expanded_); expanded_ = NULL;
   std::free(idata_);    idata_    = NULL;

   return result;
}

void * png::load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri)
{
   png_decoder p;
   p.s_ = s;
   return p.do_png(x,y,comp,req_comp, ri);
}

int png::test(stream *s)
{
   int r;
   r = check_png_header(s);
   s->rewind();
   return r;
}

int png_decoder::info_raw(int *x, int *y, int *comp)
{
   if (!parse_png_file(scan_header, 0)) {
      s_->rewind();
      return 0;
   }
   if (x) *x = s_->img_x;
   if (y) *y = s_->img_y;
   if (comp) *comp = s_->img_n;
   return 1;
}

int png::info(stream *s, int *x, int *y, int *comp)
{
   png_decoder p;
   p.s_ = s;
   return p.info_raw(x, y, comp);
}


}  // namespace stb::codec::detail
