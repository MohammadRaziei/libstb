#include "codec/decode/jpeg.hpp"

namespace stb::codec::detail {

inline std::uint32_t rotate_left(std::uint32_t x, int y) { return (x << y) | (x >> (-y & 31)); }


//////////////////////////////////////////////////////////////////////////////
//
//  "baseline" JPEG/JFIF decoder
//
//    simple implementation
//      - doesn't support delayed output of y-dimension
//      - simple interface (only one output format: 8-bit interleaved RGB)
//      - doesn't try to recover corrupt jpegs
//      - doesn't allow partial loading, loading multiple at once
//      - still fast on x86 (copying globals into locals doesn't help x86)
//      - allocates lots of intermediate memory (full size of all components)
//        - non-interleaved case requires this anyway
//        - allows good upsampling (see next)
//    high-quality
//      - upsampled channels are bilinearly interpolated, even across blocks
//      - quality integer IDCT derived from IJG's 'slow'
//    performance
//      - fast huffman; reasonable integer IDCT
//      - some SIMD kernels for common paths on targets with SSE2/NEON
//      - uses a lot of intermediate memory, could cache poorly


// huffman decoding acceleration
constexpr int fast_bits = 9;  // larger handles more cases; smaller stomps less cache

struct huffman_table {
   byte  fast[1 << fast_bits];
   // weirdly, repacking this into AoS is a 10% speed loss, instead of a win
   std::uint16_t code[256];
   byte  values[256];
   byte  size[257];
   unsigned int maxcode[18];
   int    delta[17];   // old 'firstsymbol' - old 'firstcode'

    int build(int *count);
};


class jpeg_decoder {
public:
   stream *stream_;
   huffman_table huff_dc_[4];
   huffman_table huff_ac_[4];
   std::uint16_t dequant_[4][64];
   std::int16_t fast_ac_[4][1 << fast_bits];

// sizes for components, interleaved MCUs
   int img_h_max_, img_v_max_;
   int img_mcu_x_, img_mcu_y_;
   int img_mcu_w_, img_mcu_h_;

// definition of jpeg image component
   struct
   {
      int id;
      int h,v;
      int tq;
      int hd,ha;
      int dc_pred;

      int x,y,w2,h2;
      byte *data;
      void *raw_data, *raw_coeff;
      byte *linebuf;
      short   *coeff;   // progressive only
      int      coeff_w, coeff_h; // number of 8x8 coefficient blocks
   } img_comp_[4];

   std::uint32_t   code_buffer_; // jpeg entropy-coded buffer
   int            code_bits_;   // number of valid bits
   unsigned char  marker_;      // marker seen while filling entropy buffer
   int            nomore_;      // flag if we saw a marker so must stop

   int            progressive_;
   int            spec_start_;
   int            spec_end_;
   int            succ_high_;
   int            succ_low_;
   int            eob_run_;
   int            jfif_;
   int            app14_color_transform_; // Adobe APP14 tag
   int            rgb_;

   int scan_n_, order_[4];
   int restart_interval_, todo_;

// kernels
   void (*idct_block_kernel_)(byte *out, int out_stride, short data[64]);
   void (*YCbCr_to_RGB_kernel_)(byte *out, const byte *y, const byte *pcb, const byte *pcr, int count, int step);
   byte *(*resample_row_hv_2_kernel_)(byte *out, byte *in_near, byte *in_far, int w, int hs);

    void grow_buffer_unsafe();
    int huff_decode(huffman_table *h);
    int extend_receive(int n);
    int get_bits(int n);
    int get_bit();
    int decode_block(short data[64], huffman_table *hdc, huffman_table *hac, std::int16_t *fac, int b, std::uint16_t *dequant);
    int decode_block_prog_dc(short data[64], huffman_table *hdc, int b);
    int decode_block_prog_ac(short data[64], huffman_table *hac, std::int16_t *fac);
    byte get_marker();
    void reset();
    int parse_entropy_coded_data();
    void finish();
    int process_marker(int m);
    int process_scan_header();
    int free_jpeg_components(int ncomp, int why);
    int process_frame_header(int scan);
    int decode_jpeg_header(int scan);
    byte skip_jpeg_junk_at_end();
    int decode_jpeg_image();
    void setup_jpeg();
    void cleanup_jpeg();
    byte * load_jpeg_image(int *out_x, int *out_y, int *comp, int req_comp);
    int info_raw(int *x, int *y, int *comp);
};


int huffman_table::build(int *count)
{
   int i,j,k=0;
   unsigned int code;
   // build size list for each symbol (from JPEG spec)
   for (i=0; i < 16; ++i) {
      for (j=0; j < count[i]; ++j) {
         size[k++] = (byte) (i+1);
         if(k >= 257) return fail("bad size list","Corrupt JPEG");
      }
   }
   size[k] = 0;

   // compute actual symbols (from jpeg spec)
   code = 0;
   k = 0;
   for(j=1; j <= 16; ++j) {
      // compute delta to add to code to compute symbol id
      delta[j] = k - code;
      if (size[k] == j) {
         while (size[k] == j)
            this->code[k++] = (std::uint16_t) (code++);
         if (code-1 >= (1u << j)) return fail("bad code lengths","Corrupt JPEG");
      }
      // compute largest code + 1 for this size, preshifted as needed later
      maxcode[j] = code << (16-j);
      code <<= 1;
   }
   maxcode[j] = 0xffffffff;

   // build non-spec acceleration table; 255 is flag for not-accelerated
   memset(fast, 255, 1 << fast_bits);
   for (i=0; i < k; ++i) {
      int s = size[i];
      if (s <= fast_bits) {
         int c = this->code[i] << (fast_bits-s);
         int m = 1 << (fast_bits-s);
         for (j=0; j < m; ++j) {
            fast[c+j] = (byte) i;
         }
      }
   }
   return 1;
}

// build a table that decodes both magnitude and value of small ACs in
// one go.
static void build_fast_ac(std::int16_t *fast_ac, huffman_table *h)
{
   int i;
   for (i=0; i < (1 << fast_bits); ++i) {
      byte fast = h->fast[i];
      fast_ac[i] = 0;
      if (fast < 255) {
         int rs = h->values[fast];
         int run = (rs >> 4) & 15;
         int magbits = rs & 15;
         int len = h->size[fast];

         if (magbits && len + magbits <= fast_bits) {
            // magnitude code followed by receive_extend code
            int k = ((i << len) & ((1 << fast_bits) - 1)) >> (fast_bits - magbits);
            int m = 1 << (magbits - 1);
            if (k < m) k += (~0U << magbits) + 1;
            // if the result is small enough, we can fit it in fast_ac table
            if (k >= -128 && k <= 127)
               fast_ac[i] = (std::int16_t) ((k * 256) + (run * 16) + (len + magbits));
         }
      }
   }
}

void jpeg_decoder::grow_buffer_unsafe()
{
   do {
      unsigned int b = nomore_ ? 0 : stream_->get8();
      if (b == 0xff) {
         int c = stream_->get8();
         while (c == 0xff) c = stream_->get8(); // consume fill bytes
         if (c != 0) {
            marker_ = (unsigned char) c;
            nomore_ = 1;
            return;
         }
      }
      code_buffer_ |= b << (24 - code_bits_);
      code_bits_ += 8;
   } while (code_bits_ <= 24);
}

// (1 << n) - 1
static const std::uint32_t bmask[17]={0,1,3,7,15,31,63,127,255,511,1023,2047,4095,8191,16383,32767,65535};

// decode a jpeg huffman value from the bitstream
inline int jpeg_decoder::huff_decode(huffman_table *h)
{
   unsigned int temp;
   int c,k;

   if (code_bits_ < 16) grow_buffer_unsafe();

   // look at the top fast_bits and determine what symbol ID it is,
   // if the code is <= fast_bits
   c = (code_buffer_ >> (32 - fast_bits)) & ((1 << fast_bits)-1);
   k = h->fast[c];
   if (k < 255) {
      int s = h->size[k];
      if (s > code_bits_)
         return -1;
      code_buffer_ <<= s;
      code_bits_ -= s;
      return h->values[k];
   }

   // naive test is to shift the code_buffer down so k bits are
   // valid, then test against maxcode. To speed this up, we've
   // preshifted maxcode left so that it has (16-k) 0s at the
   // end; in other words, regardless of the number of bits, it
   // wants to be compared against something shifted to have 16;
   // that way we don't need to shift inside the loop.
   temp = code_buffer_ >> 16;
   for (k=fast_bits+1 ; ; ++k)
      if (temp < h->maxcode[k])
         break;
   if (k == 17) {
      // error! code not found
      code_bits_ -= 16;
      return -1;
   }

   if (k > code_bits_)
      return -1;

   // convert the huffman code to the symbol id
   c = ((code_buffer_ >> (32 - k)) & bmask[k]) + h->delta[k];
   if(c < 0 || c >= 256) // symbol id out of bounds!
       return -1;
   assert((((code_buffer) >> (32 - h->size[c])) & bmask[h->size[c]]) == h->code[c]);

   // convert the id to a symbol
   code_bits_ -= k;
   code_buffer_ <<= k;
   return h->values[c];
}

// bias[n] = (-1<<n) + 1
static const int jbias[16] = {0,-1,-3,-7,-15,-31,-63,-127,-255,-511,-1023,-2047,-4095,-8191,-16383,-32767};

// combined JPEG 'receive' and JPEG 'extend', since baseline
// always extends everything it receives.
inline int jpeg_decoder::extend_receive(int n)
{
   unsigned int k;
   int sgn;
   if (code_bits_ < n) grow_buffer_unsafe();
   if (code_bits_ < n) return 0; // ran out of bits from stream, return 0s intead of continuing

   sgn = code_buffer_ >> 31; // sign bit always in MSB; 0 if MSB clear (positive), 1 if MSB set (negative)
   k = rotate_left(code_buffer_, n);
   code_buffer_ = k & ~bmask[n];
   k &= bmask[n];
   code_bits_ -= n;
   return k + (jbias[n] & (sgn - 1));
}

// get some unsigned bits
inline int jpeg_decoder::get_bits(int n)
{
   unsigned int k;
   if (code_bits_ < n) grow_buffer_unsafe();
   if (code_bits_ < n) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = rotate_left(code_buffer_, n);
   code_buffer_ = k & ~bmask[n];
   k &= bmask[n];
   code_bits_ -= n;
   return k;
}

inline int jpeg_decoder::get_bit()
{
   unsigned int k;
   if (code_bits_ < 1) grow_buffer_unsafe();
   if (code_bits_ < 1) return 0; // ran out of bits from stream, return 0s intead of continuing
   k = code_buffer_;
   code_buffer_ <<= 1;
   --code_bits_;
   return k & 0x80000000;
}

// given a value that's at position X in the zigzag stream,
// where does it appear in the 8x8 matrix coded as row-major?
static const byte jpeg_dezigzag[64+15] =
{
    0,  1,  8, 16,  9,  2,  3, 10,
   17, 24, 32, 25, 18, 11,  4,  5,
   12, 19, 26, 33, 40, 48, 41, 34,
   27, 20, 13,  6,  7, 14, 21, 28,
   35, 42, 49, 56, 57, 50, 43, 36,
   29, 22, 15, 23, 30, 37, 44, 51,
   58, 59, 52, 45, 38, 31, 39, 46,
   53, 60, 61, 54, 47, 55, 62, 63,
   // let corrupt input sample past end
   63, 63, 63, 63, 63, 63, 63, 63,
   63, 63, 63, 63, 63, 63, 63
};

// decode one 64-entry block--
int jpeg_decoder::decode_block(short data[64], huffman_table *hdc, huffman_table *hac, std::int16_t *fac, int b, std::uint16_t *dequant)
{
   int diff,dc,k;
   int t;

   if (code_bits_ < 16) grow_buffer_unsafe();
   t = huff_decode(hdc);
   if (t < 0 || t > 15) return fail("bad huffman code","Corrupt JPEG");

   // 0 all the ac values now so we can do it 32-bits at a time
   memset(data,0,64*sizeof(data[0]));

   diff = t ? extend_receive(t) : 0;
   if (!addints_valid(img_comp_[b].dc_pred, diff)) return fail("bad delta","Corrupt JPEG");
   dc = img_comp_[b].dc_pred + diff;
   img_comp_[b].dc_pred = dc;
   if (!mul2shorts_valid(dc, dequant[0])) return fail("can't merge dc and ac", "Corrupt JPEG");
   data[0] = (short) (dc * dequant[0]);

   // decode AC components, see JPEG spec
   k = 1;
   do {
      unsigned int zig;
      int c,r,s;
      if (code_bits_ < 16) grow_buffer_unsafe();
      c = (code_buffer_ >> (32 - fast_bits)) & ((1 << fast_bits)-1);
      r = fac[c];
      if (r) { // fast-AC path
         k += (r >> 4) & 15; // run
         s = r & 15; // combined length
         if (s > code_bits_) return fail("bad huffman code", "Combined length longer than code bits available");
         code_buffer_ <<= s;
         code_bits_ -= s;
         // decode into unzigzag'd location
         zig = jpeg_dezigzag[k++];
         data[zig] = (short) ((r >> 8) * dequant[zig]);
      } else {
         int rs = huff_decode(hac);
         if (rs < 0) return fail("bad huffman code","Corrupt JPEG");
         s = rs & 15;
         r = rs >> 4;
         if (s == 0) {
            if (rs != 0xf0) break; // end block
            k += 16;
         } else {
            k += r;
            // decode into unzigzag'd location
            zig = jpeg_dezigzag[k++];
            data[zig] = (short) (extend_receive(s) * dequant[zig]);
         }
      }
   } while (k < 64);
   return 1;
}

int jpeg_decoder::decode_block_prog_dc(short data[64], huffman_table *hdc, int b)
{
   int diff,dc;
   int t;
   if (spec_end_ != 0) return fail("can't merge dc and ac", "Corrupt JPEG");

   if (code_bits_ < 16) grow_buffer_unsafe();

   if (succ_high_ == 0) {
      // first scan for DC coefficient, must be first
      memset(data,0,64*sizeof(data[0])); // 0 all the ac values now
      t = huff_decode(hdc);
      if (t < 0 || t > 15) return fail("can't merge dc and ac", "Corrupt JPEG");
      diff = t ? extend_receive(t) : 0;

      if (!addints_valid(img_comp_[b].dc_pred, diff)) return fail("bad delta", "Corrupt JPEG");
      dc = img_comp_[b].dc_pred + diff;
      img_comp_[b].dc_pred = dc;
      if (!mul2shorts_valid(dc, 1 << succ_low_)) return fail("can't merge dc and ac", "Corrupt JPEG");
      data[0] = (short) (dc * (1 << succ_low_));
   } else {
      // refinement scan for DC coefficient
      if (get_bit())
         data[0] += (short) (1 << succ_low_);
   }
   return 1;
}

// @OPTIMIZE: store non-zigzagged during the decode passes,
// and only de-zigzag when dequantizing
int jpeg_decoder::decode_block_prog_ac(short data[64], huffman_table *hac, std::int16_t *fac)
{
   int k;
   if (spec_start_ == 0) return fail("can't merge dc and ac", "Corrupt JPEG");

   if (succ_high_ == 0) {
      int shift = succ_low_;

      if (eob_run_) {
         --eob_run_;
         return 1;
      }

      k = spec_start_;
      do {
         unsigned int zig;
         int c,r,s;
         if (code_bits_ < 16) grow_buffer_unsafe();
         c = (code_buffer_ >> (32 - fast_bits)) & ((1 << fast_bits)-1);
         r = fac[c];
         if (r) { // fast-AC path
            k += (r >> 4) & 15; // run
            s = r & 15; // combined length
            if (s > code_bits_) return fail("bad huffman code", "Combined length longer than code bits available");
            code_buffer_ <<= s;
            code_bits_ -= s;
            zig = jpeg_dezigzag[k++];
            data[zig] = (short) ((r >> 8) * (1 << shift));
         } else {
            int rs = huff_decode(hac);
            if (rs < 0) return fail("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  eob_run_ = (1 << r);
                  if (r)
                     eob_run_ += get_bits(r);
                  --eob_run_;
                  break;
               }
               k += 16;
            } else {
               k += r;
               zig = jpeg_dezigzag[k++];
               data[zig] = (short) (extend_receive(s) * (1 << shift));
            }
         }
      } while (k <= spec_end_);
   } else {
      // refinement scan for these AC coefficients

      short bit = (short) (1 << succ_low_);

      if (eob_run_) {
         --eob_run_;
         for (k = spec_start_; k <= spec_end_; ++k) {
            short *p = &data[jpeg_dezigzag[k]];
            if (*p != 0)
               if (get_bit())
                  if ((*p & bit)==0) {
                     if (*p > 0)
                        *p += bit;
                     else
                        *p -= bit;
                  }
         }
      } else {
         k = spec_start_;
         do {
            int r,s;
            int rs = huff_decode(hac); // @OPTIMIZE see if we can use the fast path here, advance-by-r is so slow, eh
            if (rs < 0) return fail("bad huffman code","Corrupt JPEG");
            s = rs & 15;
            r = rs >> 4;
            if (s == 0) {
               if (r < 15) {
                  eob_run_ = (1 << r) - 1;
                  if (r)
                     eob_run_ += get_bits(r);
                  r = 64; // force end of block
               } else {
                  // r=15 s=0 should write 16 0s, so we just do
                  // a run of 15 0s and then write s (which is 0),
                  // so we don't have to do anything special here
               }
            } else {
               if (s != 1) return fail("bad huffman code", "Corrupt JPEG");
               // sign bit
               if (get_bit())
                  s = bit;
               else
                  s = -bit;
            }

            // advance by r
            while (k <= spec_end_) {
               short *p = &data[jpeg_dezigzag[k++]];
               if (*p != 0) {
                  if (get_bit())
                     if ((*p & bit)==0) {
                        if (*p > 0)
                           *p += bit;
                        else
                           *p -= bit;
                     }
               } else {
                  if (r == 0) {
                     *p = (short) s;
                     break;
                  }
                  --r;
               }
            }
         } while (k <= spec_end_);
      }
   }
   return 1;
}


constexpr byte marker_none = 0xff;
// if there's a pending marker from the entropy stream, return that
// otherwise, fetch from the stream and get a marker. if there's no
// marker, return 0xff, which is never a valid marker value
byte jpeg_decoder::get_marker()
{
   byte x;
   if (marker_ != marker_none) { x = marker_; marker_ = marker_none; return x; }
   x = stream_->get8();
   if (x != 0xff) return marker_none;
   while (x == 0xff)
      x = stream_->get8(); // consume repeated 0xff fill bytes
   return x;
}

// in each scan, we'll have scan_n components, and the order
// of the components is specified by order[]
constexpr bool is_restart(int x) { return x >= 0xd0 && x <= 0xd7; }

// after a restart interval, jpeg_reset the entropy decoder and
// the dc prediction
void jpeg_decoder::reset()
{
   code_bits_ = 0;
   code_buffer_ = 0;
   nomore_ = 0;
   img_comp_[0].dc_pred = img_comp_[1].dc_pred = img_comp_[2].dc_pred = img_comp_[3].dc_pred = 0;
   marker_ = marker_none;
   todo_ = restart_interval_ ? restart_interval_ : 0x7fffffff;
   eob_run_ = 0;
   // no more than 1<<31 MCUs if no restart_interal? that's plenty safe,
   // since we don't even allow 1<<30 pixels
}

int jpeg_decoder::parse_entropy_coded_data()
{
   reset();
   if (!progressive_) {
      if (scan_n_ == 1) {
         int i,j;
         STBI_SIMD_ALIGN(short, data[64]);
         int n = order_[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (img_comp_[n].x+7) >> 3;
         int h = (img_comp_[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               int ha = img_comp_[n].ha;
               if (!decode_block(data, huff_dc_+img_comp_[n].hd, huff_ac_+ha, fast_ac_[ha], n, dequant_[img_comp_[n].tq])) return 0;
               idct_block_kernel_(img_comp_[n].data+img_comp_[n].w2*j*8+i*8, img_comp_[n].w2, data);
               // every data block is an MCU, so countdown the restart interval
               if (--todo_ <= 0) {
                  if (code_bits_ < 24) grow_buffer_unsafe();
                  // if it's NOT a restart, then just bail, so we get corrupt data
                  // rather than no data
                  if (!is_restart(marker_)) return 1;
                  reset();
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         STBI_SIMD_ALIGN(short, data[64]);
         for (j=0; j < img_mcu_y_; ++j) {
            for (i=0; i < img_mcu_x_; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < scan_n_; ++k) {
                  int n = order_[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < img_comp_[n].v; ++y) {
                     for (x=0; x < img_comp_[n].h; ++x) {
                        int x2 = (i*img_comp_[n].h + x)*8;
                        int y2 = (j*img_comp_[n].v + y)*8;
                        int ha = img_comp_[n].ha;
                        if (!decode_block(data, huff_dc_+img_comp_[n].hd, huff_ac_+ha, fast_ac_[ha], n, dequant_[img_comp_[n].tq])) return 0;
                        idct_block_kernel_(img_comp_[n].data+img_comp_[n].w2*y2+x2, img_comp_[n].w2, data);
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--todo_ <= 0) {
                  if (code_bits_ < 24) grow_buffer_unsafe();
                  if (!is_restart(marker_)) return 1;
                  reset();
               }
            }
         }
         return 1;
      }
   } else {
      if (scan_n_ == 1) {
         int i,j;
         int n = order_[0];
         // non-interleaved data, we just need to process one block at a time,
         // in trivial scanline order
         // number of blocks to do just depends on how many actual "pixels" this
         // component has, independent of interleaved MCU blocking and such
         int w = (img_comp_[n].x+7) >> 3;
         int h = (img_comp_[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = img_comp_[n].coeff + 64 * (i + j * img_comp_[n].coeff_w);
               if (spec_start_ == 0) {
                  if (!decode_block_prog_dc(data, &huff_dc_[img_comp_[n].hd], n))
                     return 0;
               } else {
                  int ha = img_comp_[n].ha;
                  if (!decode_block_prog_ac(data, &huff_ac_[ha], fast_ac_[ha]))
                     return 0;
               }
               // every data block is an MCU, so countdown the restart interval
               if (--todo_ <= 0) {
                  if (code_bits_ < 24) grow_buffer_unsafe();
                  if (!is_restart(marker_)) return 1;
                  reset();
               }
            }
         }
         return 1;
      } else { // interleaved
         int i,j,k,x,y;
         for (j=0; j < img_mcu_y_; ++j) {
            for (i=0; i < img_mcu_x_; ++i) {
               // scan an interleaved mcu... process scan_n components in order
               for (k=0; k < scan_n_; ++k) {
                  int n = order_[k];
                  // scan out an mcu's worth of this component; that's just determined
                  // by the basic H and V specified for the component
                  for (y=0; y < img_comp_[n].v; ++y) {
                     for (x=0; x < img_comp_[n].h; ++x) {
                        int x2 = (i*img_comp_[n].h + x);
                        int y2 = (j*img_comp_[n].v + y);
                        short *data = img_comp_[n].coeff + 64 * (x2 + y2 * img_comp_[n].coeff_w);
                        if (!decode_block_prog_dc(data, &huff_dc_[img_comp_[n].hd], n))
                           return 0;
                     }
                  }
               }
               // after all interleaved components, that's an interleaved MCU,
               // so now count down the restart interval
               if (--todo_ <= 0) {
                  if (code_bits_ < 24) grow_buffer_unsafe();
                  if (!is_restart(marker_)) return 1;
                  reset();
               }
            }
         }
         return 1;
      }
   }
}

static void jpeg_dequantize(short *data, std::uint16_t *dequant)
{
   int i;
   for (i=0; i < 64; ++i)
      data[i] *= dequant[i];
}

void jpeg_decoder::finish()
{
   if (progressive_) {
      // dequantize and idct the data
      int i,j,n;
      for (n=0; n < stream_->img_n; ++n) {
         int w = (img_comp_[n].x+7) >> 3;
         int h = (img_comp_[n].y+7) >> 3;
         for (j=0; j < h; ++j) {
            for (i=0; i < w; ++i) {
               short *data = img_comp_[n].coeff + 64 * (i + j * img_comp_[n].coeff_w);
               jpeg_dequantize(data, dequant_[img_comp_[n].tq]);
               idct_block_kernel_(img_comp_[n].data+img_comp_[n].w2*j*8+i*8, img_comp_[n].w2, data);
            }
         }
      }
   }
}

int jpeg_decoder::process_marker(int m)
{
   int L;
   switch (m) {
      case marker_none: // no marker found
         return fail("expected marker","Corrupt JPEG");

      case 0xDD: // DRI - specify restart interval
         if (stream_->get16be() != 4) return fail("bad DRI len","Corrupt JPEG");
         restart_interval_ = stream_->get16be();
         return 1;

      case 0xDB: // DQT - define quantization table
         L = stream_->get16be()-2;
         while (L > 0) {
            int q = stream_->get8();
            int p = q >> 4, sixteen = (p != 0);
            int t = q & 15,i;
            if (p != 0 && p != 1) return fail("bad DQT type","Corrupt JPEG");
            if (t > 3) return fail("bad DQT table","Corrupt JPEG");

            for (i=0; i < 64; ++i)
               dequant_[t][jpeg_dezigzag[i]] = (std::uint16_t)(sixteen ? stream_->get16be() : stream_->get8());
            L -= (sixteen ? 129 : 65);
         }
         return L==0;

      case 0xC4: // DHT - define huffman table
         L = stream_->get16be()-2;
         while (L > 0) {
            byte *v;
            int sizes[16],i,n=0;
            int q = stream_->get8();
            int tc = q >> 4;
            int th = q & 15;
            if (tc > 1 || th > 3) return fail("bad DHT header","Corrupt JPEG");
            for (i=0; i < 16; ++i) {
               sizes[i] = stream_->get8();
               n += sizes[i];
            }
            if(n > 256) return fail("bad DHT header","Corrupt JPEG"); // Loop over i < n would write past end of values!
            L -= 17;
            if (tc == 0) {
               if (!(huff_dc_+th)->build(sizes)) return 0;
               v = huff_dc_[th].values;
            } else {
               if (!(huff_ac_+th)->build(sizes)) return 0;
               v = huff_ac_[th].values;
            }
            for (i=0; i < n; ++i)
               v[i] = stream_->get8();
            if (tc != 0)
               build_fast_ac(fast_ac_[th], huff_ac_ + th);
            L -= n;
         }
         return L==0;
   }

   // check for comment block or APP blocks
   if ((m >= 0xE0 && m <= 0xEF) || m == 0xFE) {
      L = stream_->get16be();
      if (L < 2) {
         if (m == 0xFE)
            return fail("bad COM len","Corrupt JPEG");
         else
            return fail("bad APP len","Corrupt JPEG");
      }
      L -= 2;

      if (m == 0xE0 && L >= 5) { // JFIF APP0 segment
         static const unsigned char tag[5] = {'J','F','I','F','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 5; ++i)
            if (stream_->get8() != tag[i])
               ok = 0;
         L -= 5;
         if (ok)
            jfif_ = 1;
      } else if (m == 0xEE && L >= 12) { // Adobe APP14 segment
         static const unsigned char tag[6] = {'A','d','o','b','e','\0'};
         int ok = 1;
         int i;
         for (i=0; i < 6; ++i)
            if (stream_->get8() != tag[i])
               ok = 0;
         L -= 6;
         if (ok) {
            stream_->get8(); // version
            stream_->get16be(); // flags0
            stream_->get16be(); // flags1
            app14_color_transform_ = stream_->get8(); // color transform
            L -= 6;
         }
      }

      stream_->skip(L);
      return 1;
   }

   return fail("unknown marker","Corrupt JPEG");
}

// after we see SOS
int jpeg_decoder::process_scan_header()
{
   int i;
   int Ls = stream_->get16be();
   scan_n_ = stream_->get8();
   if (scan_n_ < 1 || scan_n_ > 4 || scan_n_ > (int) stream_->img_n) return fail("bad SOS component count","Corrupt JPEG");
   if (Ls != 6+2*scan_n_) return fail("bad SOS len","Corrupt JPEG");
   for (i=0; i < scan_n_; ++i) {
      int id = stream_->get8(), which;
      int q = stream_->get8();
      for (which = 0; which < stream_->img_n; ++which)
         if (img_comp_[which].id == id)
            break;
      if (which == stream_->img_n) return 0; // no match
      img_comp_[which].hd = q >> 4;   if (img_comp_[which].hd > 3) return fail("bad DC huff","Corrupt JPEG");
      img_comp_[which].ha = q & 15;   if (img_comp_[which].ha > 3) return fail("bad AC huff","Corrupt JPEG");
      order_[i] = which;
   }

   {
      int aa;
      spec_start_ = stream_->get8();
      spec_end_   = stream_->get8(); // should be 63, but might be 0
      aa = stream_->get8();
      succ_high_ = (aa >> 4);
      succ_low_  = (aa & 15);
      if (progressive_) {
         if (spec_start_ > 63 || spec_end_ > 63  || spec_start_ > spec_end_ || succ_high_ > 13 || succ_low_ > 13)
            return fail("bad SOS", "Corrupt JPEG");
      } else {
         if (spec_start_ != 0) return fail("bad SOS","Corrupt JPEG");
         if (succ_high_ != 0 || succ_low_ != 0) return fail("bad SOS","Corrupt JPEG");
         spec_end_ = 63;
      }
   }

   return 1;
}

int jpeg_decoder::free_jpeg_components(int ncomp, int why)
{
   int i;
   for (i=0; i < ncomp; ++i) {
      if (img_comp_[i].raw_data) {
         std::free(img_comp_[i].raw_data);
         img_comp_[i].raw_data = NULL;
         img_comp_[i].data = NULL;
      }
      if (img_comp_[i].raw_coeff) {
         std::free(img_comp_[i].raw_coeff);
         img_comp_[i].raw_coeff = 0;
         img_comp_[i].coeff = 0;
      }
      if (img_comp_[i].linebuf) {
         std::free(img_comp_[i].linebuf);
         img_comp_[i].linebuf = NULL;
      }
   }
   return why;
}

int jpeg_decoder::process_frame_header(int scan)
{
   int Lf,p,i,q, h_max=1,v_max=1,c;
   Lf = stream_->get16be();         if (Lf < 11) return fail("bad SOF len","Corrupt JPEG"); // JPEG
   p  = stream_->get8();            if (p != 8) return fail("only 8-bit","JPEG format not supported: 8-bit only"); // JPEG baseline
   stream_->img_y = stream_->get16be();   if (stream_->img_y == 0) return fail("no header height", "JPEG format not supported: delayed height"); // Legal, but we don't handle it--but neither does IJG
   stream_->img_x = stream_->get16be();   if (stream_->img_x == 0) return fail("0 width","Corrupt JPEG"); // JPEG requires
   if (stream_->img_y > max_dimensions) return fail("too large","Very large image (corrupt?)");
   if (stream_->img_x > max_dimensions) return fail("too large","Very large image (corrupt?)");
   c = stream_->get8();
   if (c != 3 && c != 1 && c != 4) return fail("bad component count","Corrupt JPEG");
   stream_->img_n = c;
   for (i=0; i < c; ++i) {
      img_comp_[i].data = NULL;
      img_comp_[i].linebuf = NULL;
   }

   if (Lf != 8+3*stream_->img_n) return fail("bad SOF len","Corrupt JPEG");

   rgb_ = 0;
   for (i=0; i < stream_->img_n; ++i) {
      static const unsigned char rgb[3] = { 'R', 'G', 'B' };
      img_comp_[i].id = stream_->get8();
      if (stream_->img_n == 3 && img_comp_[i].id == rgb[i])
         ++rgb_;
      q = stream_->get8();
      img_comp_[i].h = (q >> 4);  if (!img_comp_[i].h || img_comp_[i].h > 4) return fail("bad H","Corrupt JPEG");
      img_comp_[i].v = q & 15;    if (!img_comp_[i].v || img_comp_[i].v > 4) return fail("bad V","Corrupt JPEG");
      img_comp_[i].tq = stream_->get8();  if (img_comp_[i].tq > 3) return fail("bad TQ","Corrupt JPEG");
   }

   if (scan != scan_load) return 1;

   if (!mad3sizes_valid(stream_->img_x, stream_->img_y, stream_->img_n, 0)) return fail("too large", "Image too large to decode");

   for (i=0; i < stream_->img_n; ++i) {
      if (img_comp_[i].h > h_max) h_max = img_comp_[i].h;
      if (img_comp_[i].v > v_max) v_max = img_comp_[i].v;
   }

   // check that plane subsampling factors are integer ratios; our resamplers can't deal with fractional ratios
   // and I've never seen a non-corrupted JPEG file actually use them
   for (i=0; i < stream_->img_n; ++i) {
      if (h_max % img_comp_[i].h != 0) return fail("bad H","Corrupt JPEG");
      if (v_max % img_comp_[i].v != 0) return fail("bad V","Corrupt JPEG");
   }

   // compute interleaved mcu info
   img_h_max_ = h_max;
   img_v_max_ = v_max;
   img_mcu_w_ = h_max * 8;
   img_mcu_h_ = v_max * 8;
   // these sizes can't be more than 17 bits
   img_mcu_x_ = (stream_->img_x + img_mcu_w_-1) / img_mcu_w_;
   img_mcu_y_ = (stream_->img_y + img_mcu_h_-1) / img_mcu_h_;

   for (i=0; i < stream_->img_n; ++i) {
      // number of effective pixels (e.g. for non-interleaved MCU)
      img_comp_[i].x = (stream_->img_x * img_comp_[i].h + h_max-1) / h_max;
      img_comp_[i].y = (stream_->img_y * img_comp_[i].v + v_max-1) / v_max;
      // to simplify generation, we'll allocate enough memory to decode
      // the bogus oversized data from using interleaved MCUs and their
      // big blocks (e.g. a 16x16 iMCU on an image of width 33); we won't
      // discard the extra data until colorspace conversion
      //
      // img_mcu_x, img_mcu_y: <=17 bits; comp[i].h and .v are <=4 (checked earlier)
      // so these muls can't overflow with 32-bit ints (which we require)
      img_comp_[i].w2 = img_mcu_x_ * img_comp_[i].h * 8;
      img_comp_[i].h2 = img_mcu_y_ * img_comp_[i].v * 8;
      img_comp_[i].coeff = 0;
      img_comp_[i].raw_coeff = 0;
      img_comp_[i].linebuf = NULL;
      img_comp_[i].raw_data = alloc_mad2(img_comp_[i].w2, img_comp_[i].h2, 15);
      if (img_comp_[i].raw_data == NULL)
         return free_jpeg_components(i+1, fail("outofmem", "Out of memory"));
      // align blocks for idct using mmx/sse
      img_comp_[i].data = (byte*) (((size_t) img_comp_[i].raw_data + 15) & ~15);
      if (progressive_) {
         // w2, h2 are multiples of 8 (see above)
         img_comp_[i].coeff_w = img_comp_[i].w2 / 8;
         img_comp_[i].coeff_h = img_comp_[i].h2 / 8;
         img_comp_[i].raw_coeff = alloc_mad3(img_comp_[i].w2, img_comp_[i].h2, sizeof(short), 15);
         if (img_comp_[i].raw_coeff == NULL)
            return free_jpeg_components(i+1, fail("outofmem", "Out of memory"));
         img_comp_[i].coeff = (short*) (((size_t) img_comp_[i].raw_coeff + 15) & ~15);
      }
   }

   return 1;
}

// use comparisons since in some cases we handle more than one case (e.g. SOF)
constexpr bool is_dnl(int x) { return x == 0xdc; }
constexpr bool is_soi(int x) { return x == 0xd8; }
constexpr bool is_eoi(int x) { return x == 0xd9; }
constexpr bool is_sof(int x) { return x == 0xc0 || x == 0xc1 || x == 0xc2; }
constexpr bool is_sos(int x) { return x == 0xda; }

constexpr bool is_sof_progressive(int x) { return x == 0xc2; }

int jpeg_decoder::decode_jpeg_header(int scan)
{
   int m;
   jfif_ = 0;
   app14_color_transform_ = -1; // valid values are 0,1,2
   marker_ = marker_none; // initialize cached marker to empty
   m = get_marker();
   if (!is_soi(m)) return fail("no SOI","Corrupt JPEG");
   if (scan == scan_type) return 1;
   m = get_marker();
   while (!is_sof(m)) {
      if (!process_marker(m)) return 0;
      m = get_marker();
      while (m == marker_none) {
         // some files have extra padding after their blocks, so ok, we'll scan
         if (stream_->at_eof()) return fail("no SOF", "Corrupt JPEG");
         m = get_marker();
      }
   }
   progressive_ = is_sof_progressive(m);
   if (!process_frame_header(scan)) return 0;
   return 1;
}

byte jpeg_decoder::skip_jpeg_junk_at_end()
{
   // some JPEGs have junk at end, skip over it but if we find what looks
   // like a valid marker, resume there
   while (!stream_->at_eof()) {
      byte x = stream_->get8();
      while (x == 0xff) { // might be a marker
         if (stream_->at_eof()) return marker_none;
         x = stream_->get8();
         if (x != 0x00 && x != 0xff) {
            // not a stuffed zero or lead-in to another marker, looks
            // like an actual marker, return it
            return x;
         }
         // stuffed zero has x=0 now which ends the loop, meaning we go
         // back to regular scan loop.
         // repeated 0xff keeps trying to read the next byte of the marker.
      }
   }
   return marker_none;
}

// decode image to YCbCr format
int jpeg_decoder::decode_jpeg_image()
{
   int m;
   for (m = 0; m < 4; m++) {
      img_comp_[m].raw_data = NULL;
      img_comp_[m].raw_coeff = NULL;
   }
   restart_interval_ = 0;
   if (!decode_jpeg_header(scan_load)) return 0;
   m = get_marker();
   while (!is_eoi(m)) {
      if (is_sos(m)) {
         if (!process_scan_header()) return 0;
         if (!parse_entropy_coded_data()) return 0;
         if (marker_ == marker_none) {
            marker_ = skip_jpeg_junk_at_end();
            // if we reach eof without hitting a marker, get_marker() below will fail and we'll eventually return 0
         }
         m = get_marker();
         if (is_restart(m))
            m = get_marker();
      } else if (is_dnl(m)) {
         int Ld = stream_->get16be();
         std::uint32_t NL = stream_->get16be();
         if (Ld != 4) return fail("bad DNL len", "Corrupt JPEG");
         if (NL != stream_->img_y) return fail("bad DNL height", "Corrupt JPEG");
         m = get_marker();
      } else {
         if (!process_marker(m)) return 1;
         m = get_marker();
      }
   }
   if (progressive_)
      finish();
   return 1;
}

// set up the kernels
void jpeg_decoder::setup_jpeg()
{
   idct_block_kernel_ = idct_block;
   YCbCr_to_RGB_kernel_ = YCbCr_to_RGB_row;
   resample_row_hv_2_kernel_ = resample_row_hv_2;

#ifdef STBI_SSE2
   if (sse2_available()) {
      idct_block_kernel_ = idct_simd;
      YCbCr_to_RGB_kernel_ = YCbCr_to_RGB_simd;
      resample_row_hv_2_kernel_ = resample_row_hv_2_simd;
   }
#endif

}

// clean up the temporary component buffers
void jpeg_decoder::cleanup_jpeg()
{
   free_jpeg_components(stream_->img_n, 0);
}

struct resample_filter
{
   resample_row_func resample;
   byte *line0,*line1;
   int hs,vs;   // expansion factor in each axis
   int w_lores; // horizontal pixels pre-expansion
   int ystep;   // how far through vertical expansion we are
   int ypos;    // which pre-expansion row we're on
};
byte * jpeg_decoder::load_jpeg_image(int *out_x, int *out_y, int *comp, int req_comp)
{
   int n, decode_n, is_rgb;
   stream_->img_n = 0; // make cleanup_jpeg safe

   // validate req_comp
   if (req_comp < 0 || req_comp > 4) return fail_bytes("bad req_comp", "Internal error");

   // load a jpeg image from whichever source, but leave in YCbCr format
   if (!decode_jpeg_image()) { cleanup_jpeg(); return NULL; }

   // determine actual number of components to generate
   n = req_comp ? req_comp : stream_->img_n >= 3 ? 3 : 1;

   is_rgb = stream_->img_n == 3 && (rgb_ == 3 || (app14_color_transform_ == 0 && !jfif_));

   if (stream_->img_n == 3 && n < 3 && !is_rgb)
      decode_n = 1;
   else
      decode_n = stream_->img_n;

   // nothing to do if no components requested; check this now to avoid
   // accessing uninitialized coutput[0] later
   if (decode_n <= 0) { cleanup_jpeg(); return NULL; }

   // resample and color-convert
   {
      int k;
      unsigned int i,j;
      byte *output;
      byte *coutput[4] = { NULL, NULL, NULL, NULL };

      resample_filter res_comp[4];

      for (k=0; k < decode_n; ++k) {
         resample_filter *r = &res_comp[k];

         // allocate line buffer big enough for upsampling off the edges
         // with upsample factor of 4
         img_comp_[k].linebuf = (byte *) alloc(stream_->img_x + 3);
         if (!img_comp_[k].linebuf) { cleanup_jpeg(); return fail_bytes("outofmem", "Out of memory"); }

         r->hs      = img_h_max_ / img_comp_[k].h;
         r->vs      = img_v_max_ / img_comp_[k].v;
         r->ystep   = r->vs >> 1;
         r->w_lores = (stream_->img_x + r->hs-1) / r->hs;
         r->ypos    = 0;
         r->line0   = r->line1 = img_comp_[k].data;

         if      (r->hs == 1 && r->vs == 1) r->resample = resample_row_1;
         else if (r->hs == 1 && r->vs == 2) r->resample = resample_row_v_2;
         else if (r->hs == 2 && r->vs == 1) r->resample = resample_row_h_2;
         else if (r->hs == 2 && r->vs == 2) r->resample = resample_row_hv_2_kernel_;
         else                               r->resample = resample_row_generic;
      }

      // can't error after this so, this is safe
      output = (byte *) alloc_mad3(n, stream_->img_x, stream_->img_y, 1);
      if (!output) { cleanup_jpeg(); return fail_bytes("outofmem", "Out of memory"); }

      // now go ahead and resample
      for (j=0; j < stream_->img_y; ++j) {
         byte *out = output + n * stream_->img_x * j;
         for (k=0; k < decode_n; ++k) {
            resample_filter *r = &res_comp[k];
            int y_bot = r->ystep >= (r->vs >> 1);
            coutput[k] = r->resample(img_comp_[k].linebuf,
                                     y_bot ? r->line1 : r->line0,
                                     y_bot ? r->line0 : r->line1,
                                     r->w_lores, r->hs);
            if (++r->ystep >= r->vs) {
               r->ystep = 0;
               r->line0 = r->line1;
               if (++r->ypos < img_comp_[k].y)
                  r->line1 += img_comp_[k].w2;
            }
         }
         if (n >= 3) {
            byte *y = coutput[0];
            if (stream_->img_n == 3) {
               if (is_rgb) {
                  for (i=0; i < stream_->img_x; ++i) {
                     out[0] = y[i];
                     out[1] = coutput[1][i];
                     out[2] = coutput[2][i];
                     out[3] = 255;
                     out += n;
                  }
               } else {
                  YCbCr_to_RGB_kernel_(out, y, coutput[1], coutput[2], stream_->img_x, n);
               }
            } else if (stream_->img_n == 4) {
               if (app14_color_transform_ == 0) { // CMYK
                  for (i=0; i < stream_->img_x; ++i) {
                     byte m = coutput[3][i];
                     out[0] = blinn_8x8(coutput[0][i], m);
                     out[1] = blinn_8x8(coutput[1][i], m);
                     out[2] = blinn_8x8(coutput[2][i], m);
                     out[3] = 255;
                     out += n;
                  }
               } else if (app14_color_transform_ == 2) { // YCCK
                  YCbCr_to_RGB_kernel_(out, y, coutput[1], coutput[2], stream_->img_x, n);
                  for (i=0; i < stream_->img_x; ++i) {
                     byte m = coutput[3][i];
                     out[0] = blinn_8x8(255 - out[0], m);
                     out[1] = blinn_8x8(255 - out[1], m);
                     out[2] = blinn_8x8(255 - out[2], m);
                     out += n;
                  }
               } else { // YCbCr + alpha?  Ignore the fourth channel for now
                  YCbCr_to_RGB_kernel_(out, y, coutput[1], coutput[2], stream_->img_x, n);
               }
            } else
               for (i=0; i < stream_->img_x; ++i) {
                  out[0] = out[1] = out[2] = y[i];
                  out[3] = 255; // not used if n==3
                  out += n;
               }
         } else {
            if (is_rgb) {
               if (n == 1)
                  for (i=0; i < stream_->img_x; ++i)
                     *out++ = compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
               else {
                  for (i=0; i < stream_->img_x; ++i, out += 2) {
                     out[0] = compute_y(coutput[0][i], coutput[1][i], coutput[2][i]);
                     out[1] = 255;
                  }
               }
            } else if (stream_->img_n == 4 && app14_color_transform_ == 0) {
               for (i=0; i < stream_->img_x; ++i) {
                  byte m = coutput[3][i];
                  byte r = blinn_8x8(coutput[0][i], m);
                  byte g = blinn_8x8(coutput[1][i], m);
                  byte b = blinn_8x8(coutput[2][i], m);
                  out[0] = compute_y(r, g, b);
                  out[1] = 255;
                  out += n;
               }
            } else if (stream_->img_n == 4 && app14_color_transform_ == 2) {
               for (i=0; i < stream_->img_x; ++i) {
                  out[0] = blinn_8x8(255 - coutput[0][i], coutput[3][i]);
                  out[1] = 255;
                  out += n;
               }
            } else {
               byte *y = coutput[0];
               if (n == 1)
                  for (i=0; i < stream_->img_x; ++i) out[i] = y[i];
               else
                  for (i=0; i < stream_->img_x; ++i) { *out++ = y[i]; *out++ = 255; }
            }
         }
      }
      cleanup_jpeg();
      *out_x = stream_->img_x;
      *out_y = stream_->img_y;
      if (comp) *comp = stream_->img_n >= 3 ? 3 : 1; // report original components, not output
      return output;
   }
}

void * jpeg::load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri)
{
   unsigned char* result;
   jpeg_decoder* j = (jpeg_decoder*) alloc(sizeof(jpeg_decoder));
   if (!j) return fail_bytes("outofmem", "Out of memory");
   memset(j, 0, sizeof(jpeg_decoder));
   (void)(ri);
   j->stream_ = s;
   j->setup_jpeg();
   result = j->load_jpeg_image(x,y,comp,req_comp);
   std::free(j);
   return result;
}

int jpeg::test(stream *s)
{
   int r;
   jpeg_decoder* j = (jpeg_decoder*)alloc(sizeof(jpeg_decoder));
   if (!j) return fail("outofmem", "Out of memory");
   memset(j, 0, sizeof(jpeg_decoder));
   j->stream_ = s;
   j->setup_jpeg();
   r = j->decode_jpeg_header(scan_type);
   s->rewind();
   std::free(j);
   return r;
}

int jpeg_decoder::info_raw(int *x, int *y, int *comp)
{
   if (!decode_jpeg_header(scan_header)) {
      stream_->rewind();
      return 0;
   }
   if (x) *x = stream_->img_x;
   if (y) *y = stream_->img_y;
   if (comp) *comp = stream_->img_n >= 3 ? 3 : 1;
   return 1;
}

int jpeg::info(stream *s, int *x, int *y, int *comp)
{
   int result;
   jpeg_decoder* j = (jpeg_decoder*) (alloc(sizeof(jpeg_decoder)));
   if (!j) return fail("outofmem", "Out of memory");
   memset(j, 0, sizeof(jpeg_decoder));
   j->stream_ = s;
   result = j->info_raw(x, y, comp);
   std::free(j);
   return result;
}

}  // namespace stb::codec::detail
