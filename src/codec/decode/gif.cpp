#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// *************************************************************************************************
// GIF loader -- public domain by Jean-Marc Lienher -- simplified/shrunk by stb

struct gif_lzw
{
   std::int16_t prefix;
   byte first;
   byte suffix;
};

class gif_decoder {
public:
   int width_,height_;
   byte *out_;                 // output buffer (always 4 components)
   byte *background_;          // The current "background" as far as a gif is concerned
   byte *history_;
   int flags_, bgindex_, ratio_, transparent_, eflags_;
   byte  palette_[256][4];
   byte local_palette_[256][4];
   gif_lzw codes_[8192];
   byte *color_table_;
   int parse_, step_;
   int lflags_;
   int start_x_, start_y_;
   int max_x_, max_y_;
   int cur_x_, cur_y_;
   int line_size_;
   int delay_;

    void out_gif_code(std::uint16_t code);
};


static int gif_test_raw(stream *s)
{
   int sz;
   if (s->get8() != 'G' || s->get8() != 'I' || s->get8() != 'F' || s->get8() != '8') return 0;
   sz = s->get8();
   if (sz != '9' && sz != '7') return 0;
   if (s->get8() != 'a') return 0;
   return 1;
}

int gif::test(stream *s)
{
   int r = gif_test_raw(s);
   s->rewind();
   return r;
}

static void gif_parse_colortable(stream *s, byte pal[256][4], int num_entries, int transp)
{
   int i;
   for (i=0; i < num_entries; ++i) {
      pal[i][2] = s->get8();
      pal[i][1] = s->get8();
      pal[i][0] = s->get8();
      pal[i][3] = transp == i ? 0 : 255;
   }
}

static int gif_header(stream *s, gif_decoder *g, int *comp, int is_info)
{
   byte version;
   if (s->get8() != 'G' || s->get8() != 'I' || s->get8() != 'F' || s->get8() != '8')
      return fail("not GIF", "Corrupt GIF");

   version = s->get8();
   if (version != '7' && version != '9')    return fail("not GIF", "Corrupt GIF");
   if (s->get8() != 'a')                return fail("not GIF", "Corrupt GIF");

   failure_message = "";
   g->width_ = s->get16le();
   g->height_ = s->get16le();
   g->flags_ = s->get8();
   g->bgindex_ = s->get8();
   g->ratio_ = s->get8();
   g->transparent_ = -1;

   if (g->width_ > max_dimensions) return fail("too large","Very large image (corrupt?)");
   if (g->height_ > max_dimensions) return fail("too large","Very large image (corrupt?)");

   if (comp != 0) *comp = 4;  // can't actually tell whether it's 3 or 4 until we parse the comments

   if (is_info) return 1;

   if (g->flags_ & 0x80)
      gif_parse_colortable(s,g->palette_, 2 << (g->flags_ & 7), -1);

   return 1;
}

static int gif_info_raw(stream *s, int *x, int *y, int *comp)
{
   gif_decoder* g = (gif_decoder*) alloc(sizeof(gif_decoder));
   if (!g) return fail("outofmem", "Out of memory");
   if (!gif_header(s, g, comp, 1)) {
      std::free(g);
      s->rewind();
      return 0;
   }
   if (x) *x = g->width_;
   if (y) *y = g->height_;
   std::free(g);
   return 1;
}

void gif_decoder::out_gif_code(std::uint16_t code)
{
   byte *p, *c;
   int idx;

   // recurse to decode the prefixes, since the linked-list is backwards,
   // and working backwards through an interleaved image would be nasty
   if (codes_[code].prefix >= 0)
      out_gif_code(codes_[code].prefix);

   if (cur_y_ >= max_y_) return;

   idx = cur_x_ + cur_y_;
   p = &out_[idx];
   history_[idx / 4] = 1;

   c = &color_table_[codes_[code].suffix * 4];
   if (c[3] > 128) { // don't render transparent pixels;
      p[0] = c[2];
      p[1] = c[1];
      p[2] = c[0];
      p[3] = c[3];
   }
   cur_x_ += 4;

   if (cur_x_ >= max_x_) {
      cur_x_ = start_x_;
      cur_y_ += step_;

      while (cur_y_ >= max_y_ && parse_ > 0) {
         step_ = (1 << parse_) * line_size_;
         cur_y_ = start_y_ + (step_ >> 1);
         --parse_;
      }
   }
}

static byte *process_gif_raster(stream *s, gif_decoder *g)
{
   byte lzw_cs;
   std::int32_t len, init_code;
   std::uint32_t first;
   std::int32_t codesize, codemask, avail, oldcode, bits, valid_bits, clear;
   gif_lzw *p;

   lzw_cs = s->get8();
   if (lzw_cs > 12) return NULL;
   clear = 1 << lzw_cs;
   first = 1;
   codesize = lzw_cs + 1;
   codemask = (1 << codesize) - 1;
   bits = 0;
   valid_bits = 0;
   for (init_code = 0; init_code < clear; init_code++) {
      g->codes_[init_code].prefix = -1;
      g->codes_[init_code].first = (byte) init_code;
      g->codes_[init_code].suffix = (byte) init_code;
   }

   // support no starting clear code
   avail = clear+2;
   oldcode = -1;

   len = 0;
   for(;;) {
      if (valid_bits < codesize) {
         if (len == 0) {
            len = s->get8(); // start new block
            if (len == 0)
               return g->out_;
         }
         --len;
         bits |= (std::int32_t) s->get8() << valid_bits;
         valid_bits += 8;
      } else {
         std::int32_t code = bits & codemask;
         bits >>= codesize;
         valid_bits -= codesize;
         // @OPTIMIZE: is there some way we can accelerate the non-clear path?
         if (code == clear) {  // clear code
            codesize = lzw_cs + 1;
            codemask = (1 << codesize) - 1;
            avail = clear + 2;
            oldcode = -1;
            first = 0;
         } else if (code == clear + 1) { // end of stream code
            s->skip(len);
            while ((len = s->get8()) > 0)
               s->skip(len);
            return g->out_;
         } else if (code <= avail) {
            if (first) {
               return fail_bytes("no clear code", "Corrupt GIF");
            }

            if (oldcode >= 0) {
               p = &g->codes_[avail++];
               if (avail > 8192) {
                  return fail_bytes("too many codes", "Corrupt GIF");
               }

               p->prefix = (std::int16_t) oldcode;
               p->first = g->codes_[oldcode].first;
               p->suffix = (code == avail) ? p->first : g->codes_[code].first;
            } else if (code == avail)
               return fail_bytes("illegal code in raster", "Corrupt GIF");

            g->out_gif_code((std::uint16_t) code);

            if ((avail & codemask) == 0 && avail <= 0x0FFF) {
               codesize++;
               codemask = (1 << codesize) - 1;
            }

            oldcode = code;
         } else {
            return fail_bytes("illegal code in raster", "Corrupt GIF");
         }
      }
   }
}

// this function is designed to support animated gifs, although stb_image doesn't support it
// two back is the image from two frames ago, used for a very specific disposal format
static byte *gif_load_next(stream *s, gif_decoder *g, int *comp, int req_comp, byte *two_back)
{
   int dispose;
   int first_frame;
   int pi;
   int pcount;
   (void)(req_comp);

   // on first frame, any non-written pixels get the background colour (non-transparent)
   first_frame = 0;
   if (g->out_ == 0) {
      if (!gif_header(s, g, comp,0)) return 0; // failure_message set by gif_header
      if (!mad3sizes_valid(4, g->width_, g->height_, 0))
         return fail_bytes("too large", "GIF image is too large");
      pcount = g->width_ * g->height_;
      g->out_ = (byte *) alloc(4 * pcount);
      g->background_ = (byte *) alloc(4 * pcount);
      g->history_ = (byte *) alloc(pcount);
      if (!g->out_ || !g->background_ || !g->history_)
         return fail_bytes("outofmem", "Out of memory");

      // image is treated as "transparent" at the start - ie, nothing overwrites the current background;
      // background colour is only used for pixels that are not rendered first frame, after that "background"
      // color refers to the color that was there the previous frame.
      memset(g->out_, 0x00, 4 * pcount);
      memset(g->background_, 0x00, 4 * pcount); // state of the background (starts transparent)
      memset(g->history_, 0x00, pcount);        // pixels that were affected previous frame
      first_frame = 1;
   } else {
      // second frame - how do we dispose of the previous one?
      dispose = (g->eflags_ & 0x1C) >> 2;
      pcount = g->width_ * g->height_;

      if ((dispose == 3) && (two_back == 0)) {
         dispose = 2; // if I don't have an image to revert back to, default to the old background
      }

      if (dispose == 3) { // use previous graphic
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history_[pi]) {
               memcpy( &g->out_[pi * 4], &two_back[pi * 4], 4 );
            }
         }
      } else if (dispose == 2) {
         // restore what was changed last frame to background before that frame;
         for (pi = 0; pi < pcount; ++pi) {
            if (g->history_[pi]) {
               memcpy( &g->out_[pi * 4], &g->background_[pi * 4], 4 );
            }
         }
      } else {
         // This is a non-disposal case eithe way, so just
         // leave the pixels as is, and they will become the new background
         // 1: do not dispose
         // 0:  not specified.
      }

      // background is what out is after the undoing of the previou frame;
      memcpy( g->background_, g->out_, 4 * g->width_ * g->height_ );
   }

   // clear my history;
   memset( g->history_, 0x00, g->width_ * g->height_ );        // pixels that were affected previous frame

   for (;;) {
      int tag = s->get8();
      switch (tag) {
         case 0x2C: /* Image Descriptor */
         {
            std::int32_t x, y, w, h;
            byte *o;

            x = s->get16le();
            y = s->get16le();
            w = s->get16le();
            h = s->get16le();
            if (((x + w) > (g->width_)) || ((y + h) > (g->height_)))
               return fail_bytes("bad Image Descriptor", "Corrupt GIF");

            g->line_size_ = g->width_ * 4;
            g->start_x_ = x * 4;
            g->start_y_ = y * g->line_size_;
            g->max_x_   = g->start_x_ + w * 4;
            g->max_y_   = g->start_y_ + h * g->line_size_;
            g->cur_x_   = g->start_x_;
            g->cur_y_   = g->start_y_;

            // if the width of the specified rectangle is 0, that means
            // we may not see *any* pixels or the image is malformed;
            // to make sure this is caught, move the current y down to
            // max_y (which is what out_gif_code checks).
            if (w == 0)
               g->cur_y_ = g->max_y_;

            g->lflags_ = s->get8();

            if (g->lflags_ & 0x40) {
               g->step_ = 8 * g->line_size_; // first interlaced spacing
               g->parse_ = 3;
            } else {
               g->step_ = g->line_size_;
               g->parse_ = 0;
            }

            if (g->lflags_ & 0x80) {
               gif_parse_colortable(s,g->local_palette_, 2 << (g->lflags_ & 7), g->eflags_ & 0x01 ? g->transparent_ : -1);
               g->color_table_ = (byte *) g->local_palette_;
            } else if (g->flags_ & 0x80) {
               g->color_table_ = (byte *) g->palette_;
            } else
               return fail_bytes("missing color table", "Corrupt GIF");

            o = process_gif_raster(s, g);
            if (!o) return NULL;

            // if this was the first frame,
            pcount = g->width_ * g->height_;
            if (first_frame && (g->bgindex_ > 0)) {
               // if first frame, any pixel not drawn to gets the background color
               for (pi = 0; pi < pcount; ++pi) {
                  if (g->history_[pi] == 0) {
                     g->palette_[g->bgindex_][3] = 255; // just in case it was made transparent, undo that; It will be reset next frame if need be;
                     memcpy( &g->out_[pi * 4], &g->palette_[g->bgindex_], 4 );
                  }
               }
            }

            return o;
         }

         case 0x21: // Comment Extension.
         {
            int len;
            int ext = s->get8();
            if (ext == 0xF9) { // Graphic Control Extension.
               len = s->get8();
               if (len == 4) {
                  g->eflags_ = s->get8();
                  g->delay_ = 10 * s->get16le(); // delay - 1/100th of a second, saving as 1/1000ths.

                  // unset old transparent
                  if (g->transparent_ >= 0) {
                     g->palette_[g->transparent_][3] = 255;
                  }
                  if (g->eflags_ & 0x01) {
                     g->transparent_ = s->get8();
                     if (g->transparent_ >= 0) {
                        g->palette_[g->transparent_][3] = 0;
                     }
                  } else {
                     // don't need transparent
                     s->skip(1);
                     g->transparent_ = -1;
                  }
               } else {
                  s->skip(len);
                  break;
               }
            }
            while ((len = s->get8()) != 0) {
               s->skip(len);
            }
            break;
         }

         case 0x3B: // gif stream termination code
            return (byte *) s; // using '1' causes warning on some compilers

         default:
            return fail_bytes("unknown code", "Corrupt GIF");
      }
   }
}



void * gif::load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri)
{
   byte *u = 0;
   gif_decoder g;
   memset(&g, 0, sizeof(g));
   (void)(ri);

   u = gif_load_next(s, &g, comp, req_comp, 0);
   if (u == (byte *) s) u = 0;  // end of animated gif marker
   if (u) {
      *x = g.width_;
      *y = g.height_;

      // moved conversion to after successful load so that the same
      // can be done for multiple frames.
      if (req_comp && req_comp != 4)
         u = convert_format(u, 4, req_comp, g.width_, g.height_);
   } else if (g.out_) {
      // if there was an error and we allocated an image buffer, free it!
      std::free(g.out_);
   }

   // free buffers needed for multiple frame loading;
   std::free(g.history_);
   std::free(g.background_);

   return u;
}

int gif::info(stream *s, int *x, int *y, int *comp)
{
   return gif_info_raw(s,x,y,comp);
}

}  // namespace stb::codec::detail
