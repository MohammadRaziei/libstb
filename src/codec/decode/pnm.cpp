#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// *************************************************************************************************
// Portable Gray Map and Portable Pixel Map loader
// by Ken Miller
//
// PGM: http://netpbm.sourceforge.net/doc/pgm.html
// PPM: http://netpbm.sourceforge.net/doc/ppm.html
//
// Known limitations:
//    Does not support comments in the header section
//    Does not support ASCII image data (formats P2 and P3)


int pnm::test(stream *s)
{
   char p, t;
   p = (char) s->get8();
   t = (char) s->get8();
   if (p != 'P' || (t != '5' && t != '6')) {
       s->rewind();
       return 0;
   }
   return 1;
}

void * pnm::load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri)
{
   byte *out;
   (void)(ri);

   ri->bits_per_channel = pnm::info(s, (int *)&s->img_x, (int *)&s->img_y, (int *)&s->img_n);
   if (ri->bits_per_channel == 0)
      return 0;

   if (s->img_y > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");
   if (s->img_x > max_dimensions) return fail_bytes("too large","Very large image (corrupt?)");

   *x = s->img_x;
   *y = s->img_y;
   if (comp) *comp = s->img_n;

   if (!mad4sizes_valid(s->img_n, s->img_x, s->img_y, ri->bits_per_channel / 8, 0))
      return fail_bytes("too large", "PNM too large");

   out = (byte *) alloc_mad4(s->img_n, s->img_x, s->img_y, ri->bits_per_channel / 8, 0);
   if (!out) return fail_bytes("outofmem", "Out of memory");
   if (!s->getn(out, s->img_n * s->img_x * s->img_y * (ri->bits_per_channel / 8))) {
      std::free(out);
      return fail_bytes("bad PNM", "PNM file truncated");
   }

   if (req_comp && req_comp != s->img_n) {
      if (ri->bits_per_channel == 16) {
         out = (byte *) convert_format16((std::uint16_t *) out, s->img_n, req_comp, s->img_x, s->img_y);
      } else {
         out = convert_format(out, s->img_n, req_comp, s->img_x, s->img_y);
      }
      if (out == NULL) return out; // convert_format frees input on failure
   }
   return out;
}

static int      pnm_isspace(char c)
{
   return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static void     pnm_skip_whitespace(stream *s, char *c)
{
   for (;;) {
      while (!s->at_eof() && pnm_isspace(*c))
         *c = (char) s->get8();

      if (s->at_eof() || *c != '#')
         break;

      while (!s->at_eof() && *c != '\n' && *c != '\r' )
         *c = (char) s->get8();
   }
}

static int      pnm_isdigit(char c)
{
   return c >= '0' && c <= '9';
}

static int      pnm_getinteger(stream *s, char *c)
{
   int value = 0;

   while (!s->at_eof() && pnm_isdigit(*c)) {
      value = value*10 + (*c - '0');
      *c = (char) s->get8();
      if((value > 214748364) || (value == 214748364 && *c > '7'))
          return fail("integer parse overflow", "Parsing an integer in the PPM header overflowed a 32-bit int");
   }

   return value;
}

int pnm::info(stream *s, int *x, int *y, int *comp)
{
   int maxv, dummy;
   char c, p, t;

   if (!x) x = &dummy;
   if (!y) y = &dummy;
   if (!comp) comp = &dummy;

   s->rewind();

   // Get identifier
   p = (char) s->get8();
   t = (char) s->get8();
   if (p != 'P' || (t != '5' && t != '6')) {
       s->rewind();
       return 0;
   }

   *comp = (t == '6') ? 3 : 1;  // '5' is 1-component .pgm; '6' is 3-component .ppm

   c = (char) s->get8();
   pnm_skip_whitespace(s, &c);

   *x = pnm_getinteger(s, &c); // read width
   if(*x == 0)
       return fail("invalid width", "PPM image header had zero or overflowing width");
   pnm_skip_whitespace(s, &c);

   *y = pnm_getinteger(s, &c); // read height
   if (*y == 0)
       return fail("invalid width", "PPM image header had zero or overflowing width");
   pnm_skip_whitespace(s, &c);

   maxv = pnm_getinteger(s, &c);  // read max value
   if (maxv > 65535)
      return fail("max value > 65535", "PPM image supports only 8-bit and 16-bit images");
   else if (maxv > 255)
      return 16;
   else
      return 8;
}


}  // namespace stb::codec::detail
