#pragma once

// The zlib / deflate decoder (RFC 1950 and 1951): PNG's pixel data, and the public zlib API through
// codec::zlib_inflate. Derived from stb_image.h v2.30 (public domain / MIT).

#include "codec/decode/formats.hpp"

namespace stb::codec::detail {

#define ZFAST_BITS  9 // accelerate all cases in default tables
#define ZFAST_MASK  ((1 << ZFAST_BITS) - 1)
#define ZNSYMS 288 // number of symbols in literal/length alphabet

// zlib-style huffman encoding
// (jpegs packs from left, zlib from right, so can't share code)
class zhuffman {
public:
   std::uint16_t fast[1 << ZFAST_BITS];
   std::uint16_t firstcode[16];
   int maxcode[17];
   std::uint16_t firstsymbol[16];
   byte  size[ZNSYMS];
   std::uint16_t value[ZNSYMS];

    int build(const byte *sizelist, int num);
};

//    memory buffer
class inflater {
public:
   byte *zbuffer, *zbuffer_end;
   int num_bits;
   int hit_zeof_once;
   std::uint32_t code_buffer;

   char *zout;
   char *zout_start;
   char *zout_end;
   int   z_expandable;

   zhuffman z_length, z_distance;

    int eof();
    byte get8();
    void fill_bits();
    unsigned int receive(int n);
    int huffman_decode_slowpath(zhuffman *z);
    int huffman_decode(zhuffman *z);
    int expand(char *zout, int n);
    int parse_huffman_block();
    int compute_huffman_codes();
    int parse_uncompressed_block();
    int read_header();
    int parse(int parse_header);
    int run(char *obuf, int olen, int exp, int parse_header);
};

}  // namespace stb::codec::detail
