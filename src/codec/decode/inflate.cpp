#include "codec/decode/inflate.hpp"
#include "codec/decode/formats.hpp"

namespace stb::codec::detail {


// public domain zlib decode    v0.2  Sean Barrett 2006-11-18
//    simple implementation
//      - all input must be provided in an upfront buffer
//      - all output is written to a single output buffer (can malloc/realloc)
//    performance
//      - fast huffman


// fast-way is faster to check than jpeg huffman, but slow way is slower

// zlib-style huffman encoding
// (jpegs packs from left, zlib from right, so can't share code)

inline static int bitreverse16(int n)
{
  n = ((n & 0xAAAA) >>  1) | ((n & 0x5555) << 1);
  n = ((n & 0xCCCC) >>  2) | ((n & 0x3333) << 2);
  n = ((n & 0xF0F0) >>  4) | ((n & 0x0F0F) << 4);
  n = ((n & 0xFF00) >>  8) | ((n & 0x00FF) << 8);
  return n;
}

inline static int bit_reverse(int v, int bits)
{
   assert(bits <= 16);
   // to bit reverse n bits, reverse 16 and shift
   // e.g. 11 bits, bit reverse and shift away 5
   return bitreverse16(v) >> (16-bits);
}

int zhuffman::build(const byte *sizelist, int num)
{
   int i,k=0;
   int code, next_code[16], sizes[17];

   // DEFLATE spec for generating codes
   memset(sizes, 0, sizeof(sizes));
   memset(fast, 0, sizeof(fast));
   for (i=0; i < num; ++i)
      ++sizes[sizelist[i]];
   sizes[0] = 0;
   for (i=1; i < 16; ++i)
      if (sizes[i] > (1 << i))
         return fail("bad sizes", "Corrupt PNG");
   code = 0;
   for (i=1; i < 16; ++i) {
      next_code[i] = code;
      firstcode[i] = (std::uint16_t) code;
      firstsymbol[i] = (std::uint16_t) k;
      code = (code + sizes[i]);
      if (sizes[i])
         if (code-1 >= (1 << i)) return fail("bad codelengths","Corrupt PNG");
      maxcode[i] = code << (16-i); // preshift for inner loop
      code <<= 1;
      k += sizes[i];
   }
   maxcode[16] = 0x10000; // sentinel
   for (i=0; i < num; ++i) {
      int s = sizelist[i];
      if (s) {
         int c = next_code[s] - firstcode[s] + firstsymbol[s];
         std::uint16_t fastv = (std::uint16_t) ((s << 9) | i);
         size [c] = (byte     ) s;
         value[c] = (std::uint16_t) i;
         if (s <= zfast_bits) {
            int j = bit_reverse(next_code[s],s);
            while (j < (1 << zfast_bits)) {
               fast[j] = fastv;
               j += (1 << s);
            }
         }
         ++next_code[s];
      }
   }
   return 1;
}

// zlib-from-memory implementation for PNG reading
//    because PNG allows splitting the zlib stream arbitrarily,
//    and it's annoying structurally to have PNG call ZLIB call PNG,
//    we require PNG read all the IDATs and combine them into a single
//    memory buffer


inline int inflater::eof()
{
   return (zbuffer_ >= zbuffer_end_);
}

inline byte inflater::get8()
{
   return eof() ? 0 : *zbuffer_++;
}

void inflater::fill_bits()
{
   do {
      if (code_buffer_ >= (1U << num_bits_)) {
        zbuffer_ = zbuffer_end_;  /* treat this as EOF so we fail. */
        return;
      }
      code_buffer_ |= (unsigned int) get8() << num_bits_;
      num_bits_ += 8;
   } while (num_bits_ <= 24);
}

inline unsigned int inflater::receive(int n)
{
   unsigned int k;
   if (num_bits_ < n) fill_bits();
   k = code_buffer_ & ((1 << n) - 1);
   code_buffer_ >>= n;
   num_bits_ -= n;
   return k;
}

int inflater::huffman_decode_slowpath(zhuffman *z)
{
   int b,s,k;
   // not resolved by fast table, so compute it the slow way
   // use jpeg approach, which requires MSbits at top
   k = bit_reverse(code_buffer_, 16);
   for (s=zfast_bits+1; ; ++s)
      if (k < z->maxcode[s])
         break;
   if (s >= 16) return -1; // invalid code!
   // code size is s, so:
   b = (k >> (16-s)) - z->firstcode[s] + z->firstsymbol[s];
   if (b >= znsyms) return -1; // some data was corrupt somewhere!
   if (z->size[b] != s) return -1;  // was originally an assert, but report failure instead.
   code_buffer_ >>= s;
   num_bits_ -= s;
   return z->value[b];
}

inline int inflater::huffman_decode(zhuffman *z)
{
   int b,s;
   if (num_bits_ < 16) {
      if (eof()) {
         if (!hit_zeof_once_) {
            // This is the first time we hit eof, insert 16 extra padding btis
            // to allow us to keep going; if we actually consume any of them
            // though, that is invalid data. This is caught later.
            hit_zeof_once_ = 1;
            num_bits_ += 16; // add 16 implicit zero bits
         } else {
            // We already inserted our extra 16 padding bits and are again
            // out, this stream is actually prematurely terminated.
            return -1;
         }
      } else {
         fill_bits();
      }
   }
   b = z->fast[code_buffer_ & zfast_mask];
   if (b) {
      s = b >> 9;
      code_buffer_ >>= s;
      num_bits_ -= s;
      return b & 511;
   }
   return huffman_decode_slowpath(z);
}

int inflater::expand(char *zout, int n)
{
   char *q;
   unsigned int cur, limit, old_limit;
   zout_ = zout;
   if (!z_expandable_) return fail("output buffer limit","Corrupt PNG");
   cur   = (unsigned int) (zout_ - zout_start_);
   limit = old_limit = (unsigned) (zout_end_ - zout_start_);
   if (UINT_MAX - cur < (unsigned) n) return fail("outofmem", "Out of memory");
   while (cur + n > limit) {
      if(limit > UINT_MAX / 2) return fail("outofmem", "Out of memory");
      limit *= 2;
   }
   q = (char *) realloc_sized(zout_start_, old_limit, limit);
   (void)(old_limit);
   if (q == NULL) return fail("outofmem", "Out of memory");
   zout_start_ = q;
   zout_       = q + cur;
   zout_end_   = q + limit;
   return 1;
}

static const int zlength_base[31] = {
   3,4,5,6,7,8,9,10,11,13,
   15,17,19,23,27,31,35,43,51,59,
   67,83,99,115,131,163,195,227,258,0,0 };

static const int zlength_extra[31]=
{ 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 };

static const int zdist_base[32] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0};

static const int zdist_extra[32] =
{ 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

int inflater::parse_huffman_block()
{
   char *zout = zout_;
   for(;;) {
      int z = huffman_decode(&z_length_);
      if (z < 256) {
         if (z < 0) return fail("bad huffman code","Corrupt PNG"); // error in huffman codes
         if (zout >= zout_end_) {
            if (!expand(zout, 1)) return 0;
            zout = zout_;
         }
         *zout++ = (char) z;
      } else {
         byte *p;
         int len,dist;
         if (z == 256) {
            zout_ = zout;
            if (hit_zeof_once_ && num_bits_ < 16) {
               // The first time we hit zeof, we inserted 16 extra zero bits into our bit
               // buffer so the decoder can just do its speculative decoding. But if we
               // actually consumed any of those bits (which is the case when num_bits < 16),
               // the stream actually read past the end so it is malformed.
               return fail("unexpected end","Corrupt PNG");
            }
            return 1;
         }
         if (z >= 286) return fail("bad huffman code","Corrupt PNG"); // per DEFLATE, length codes 286 and 287 must not appear in compressed data
         z -= 257;
         len = zlength_base[z];
         if (zlength_extra[z]) len += receive(zlength_extra[z]);
         z = huffman_decode(&z_distance_);
         if (z < 0 || z >= 30) return fail("bad huffman code","Corrupt PNG"); // per DEFLATE, distance codes 30 and 31 must not appear in compressed data
         dist = zdist_base[z];
         if (zdist_extra[z]) dist += receive(zdist_extra[z]);
         if (zout - zout_start_ < dist) return fail("bad dist","Corrupt PNG");
         if (len > zout_end_ - zout) {
            if (!expand(zout, len)) return 0;
            zout = zout_;
         }
         p = (byte *) (zout - dist);
         if (dist == 1) { // run of one byte; common in images.
            byte v = *p;
            if (len) { do *zout++ = v; while (--len); }
         } else {
            if (len) { do *zout++ = *p++; while (--len); }
         }
      }
   }
}

int inflater::compute_huffman_codes()
{
   static const byte length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
   zhuffman z_codelength;
   byte lencodes[286+32+137];//padding for maximum single op
   byte codelength_sizes[19];
   int i,n;

   int hlit  = receive(5) + 257;
   int hdist = receive(5) + 1;
   int hclen = receive(4) + 4;
   int ntot  = hlit + hdist;

   memset(codelength_sizes, 0, sizeof(codelength_sizes));
   for (i=0; i < hclen; ++i) {
      int s = receive(3);
      codelength_sizes[length_dezigzag[i]] = (byte) s;
   }
   if (!z_codelength.build(codelength_sizes, 19)) return 0;

   n = 0;
   while (n < ntot) {
      int c = huffman_decode(&z_codelength);
      if (c < 0 || c >= 19) return fail("bad codelengths", "Corrupt PNG");
      if (c < 16)
         lencodes[n++] = (byte) c;
      else {
         byte fill = 0;
         if (c == 16) {
            c = receive(2)+3;
            if (n == 0) return fail("bad codelengths", "Corrupt PNG");
            fill = lencodes[n-1];
         } else if (c == 17) {
            c = receive(3)+3;
         } else if (c == 18) {
            c = receive(7)+11;
         } else {
            return fail("bad codelengths", "Corrupt PNG");
         }
         if (ntot - n < c) return fail("bad codelengths", "Corrupt PNG");
         memset(lencodes+n, fill, c);
         n += c;
      }
   }
   if (n != ntot) return fail("bad codelengths","Corrupt PNG");
   if (!z_length_.build(lencodes, hlit)) return 0;
   if (!z_distance_.build(lencodes+hlit, hdist)) return 0;
   return 1;
}

int inflater::parse_uncompressed_block()
{
   byte header[4];
   int len,nlen,k;
   if (num_bits_ & 7)
      receive(num_bits_ & 7); // discard
   // drain the bit-packed data into header
   k = 0;
   while (num_bits_ > 0) {
      header[k++] = (byte) (code_buffer_ & 255); // suppress MSVC run-time check
      code_buffer_ >>= 8;
      num_bits_ -= 8;
   }
   if (num_bits_ < 0) return fail("zlib corrupt","Corrupt PNG");
   // now fill header the normal way
   while (k < 4)
      header[k++] = get8();
   len  = header[1] * 256 + header[0];
   nlen = header[3] * 256 + header[2];
   if (nlen != (len ^ 0xffff)) return fail("zlib corrupt","Corrupt PNG");
   if (zbuffer_ + len > zbuffer_end_) return fail("read past buffer","Corrupt PNG");
   if (zout_ + len > zout_end_)
      if (!expand(zout_, len)) return 0;
   memcpy(zout_, zbuffer_, len);
   zbuffer_ += len;
   zout_ += len;
   return 1;
}

int inflater::read_header()
{
   int cmf   = get8();
   int cm    = cmf & 15;
   /* int cinfo = cmf >> 4; */
   int flg   = get8();
   if (eof()) return fail("bad zlib header","Corrupt PNG"); // zlib spec
   if ((cmf*256+flg) % 31 != 0) return fail("bad zlib header","Corrupt PNG"); // zlib spec
   if (flg & 32) return fail("no preset dict","Corrupt PNG"); // preset dictionary not allowed in png
   if (cm != 8) return fail("bad compression","Corrupt PNG"); // DEFLATE required for png
   // window = 1 << (8 + cinfo)... but who cares, we fully buffer output
   return 1;
}

static const byte zdefault_length[znsyms] =
{
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
};
static const byte zdefault_distance[32] =
{
   5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};
/*
Init algorithm:
{
   int i;   // use <= to match clearly with spec
   for (i=0; i <= 143; ++i)     zdefault_length[i]   = 8;
   for (   ; i <= 255; ++i)     zdefault_length[i]   = 9;
   for (   ; i <= 279; ++i)     zdefault_length[i]   = 7;
   for (   ; i <= 287; ++i)     zdefault_length[i]   = 8;

   for (i=0; i <=  31; ++i)     zdefault_distance[i] = 5;
}
*/

int inflater::parse(int parse_header)
{
   int final, type;
   if (parse_header)
      if (!read_header()) return 0;
   num_bits_ = 0;
   code_buffer_ = 0;
   hit_zeof_once_ = 0;
   do {
      final = receive(1);
      type = receive(2);
      if (type == 0) {
         if (!parse_uncompressed_block()) return 0;
      } else if (type == 3) {
         return 0;
      } else {
         if (type == 1) {
            // use fixed code lengths
            if (!z_length_.build(zdefault_length  , znsyms)) return 0;
            if (!z_distance_.build(zdefault_distance,  32)) return 0;
         } else {
            if (!compute_huffman_codes()) return 0;
         }
         if (!parse_huffman_block()) return 0;
      }
   } while (!final);
   return 1;
}

int inflater::run(char *obuf, int olen, int exp, int parse_header)
{
   zout_start_ = obuf;
   zout_       = obuf;
   zout_end_   = obuf + olen;
   z_expandable_ = exp;

   return parse(parse_header);
}

char *stbi_zlib_decode_malloc_guesssize(const char *buffer, int len, int initial_size, int *outlen)
{
   inflater a;
   char *p = (char *) alloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer_ = (byte *) buffer;
   a.zbuffer_end_ = (byte *) buffer + len;
   if (a.run(p, initial_size, 1, 1)) {
      if (outlen) *outlen = (int) (a.zout_ - a.zout_start_);
      return a.zout_start_;
   } else {
      std::free(a.zout_start_);
      return NULL;
   }
}

char *stbi_zlib_decode_malloc(char const *buffer, int len, int *outlen)
{
   return stbi_zlib_decode_malloc_guesssize(buffer, len, 16384, outlen);
}

char *zlib_decode_malloc(const char *buffer, int len, int initial_size, int *outlen, int parse_header)
{
   inflater a;
   char *p = (char *) alloc(initial_size);
   if (p == NULL) return NULL;
   a.zbuffer_ = (byte *) buffer;
   a.zbuffer_end_ = (byte *) buffer + len;
   if (a.run(p, initial_size, 1, parse_header)) {
      if (outlen) *outlen = (int) (a.zout_ - a.zout_start_);
      return a.zout_start_;
   } else {
      std::free(a.zout_start_);
      return NULL;
   }
}

int stbi_zlib_decode_buffer(char *obuffer, int olen, char const *ibuffer, int ilen)
{
   inflater a;
   a.zbuffer_ = (byte *) ibuffer;
   a.zbuffer_end_ = (byte *) ibuffer + ilen;
   if (a.run(obuffer, olen, 0, 1))
      return (int) (a.zout_ - a.zout_start_);
   else
      return -1;
}

char *stbi_zlib_decode_noheader_malloc(char const *buffer, int len, int *outlen)
{
   inflater a;
   char *p = (char *) alloc(16384);
   if (p == NULL) return NULL;
   a.zbuffer_ = (byte *) buffer;
   a.zbuffer_end_ = (byte *) buffer+len;
   if (a.run(p, 16384, 1, 0)) {
      if (outlen) *outlen = (int) (a.zout_ - a.zout_start_);
      return a.zout_start_;
   } else {
      std::free(a.zout_start_);
      return NULL;
   }
}

int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen)
{
   inflater a;
   a.zbuffer_ = (byte *) ibuffer;
   a.zbuffer_end_ = (byte *) ibuffer + ilen;
   if (a.run(obuffer, olen, 0, 0))
      return (int) (a.zout_ - a.zout_start_);
   else
      return -1;
}

}  // namespace stb::codec::detail
