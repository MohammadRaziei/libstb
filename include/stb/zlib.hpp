#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "stb/export.hpp"

namespace stb {

// zlib streams (RFC 1950: a 2-byte header, deflate data, an Adler-32 trailer): what PNG, HTTP
// "deflate", many file formats and python's zlib module speak. Compression is the compressor
// libstb uses for PNG output (a rewrite of stb's: hash-chain LZ77 with lazy matching, dynamic
// Huffman blocks); decompression is the inflater of src/codec/decode (from stb_image). Both sit behind a bounded,
// exception-based interface and interoperate with zlib itself: anything zlib can read,
// `zlib_compress` produces, and anything zlib produces, `zlib_decompress` reads.
//
// Speed, measured against zlib 1.x level 6 on one machine (source code, a binary, noisy image
// pixels; measure on your data): `zlib_compress` at the default level was 1.2x to 1.9x faster
// and its output 0.4% to 3.3% larger. `zlib_decompress` is NOT faster than zlib: equal on binary
// and image data, about 2.5x slower on very compressible text. If inflate speed is what you
// need, use zlib; the point here is a small, dependency-free, bounded codec.
//
// Inputs and outputs are limited to 2 GiB (stb's ceiling): larger input throws limit_error.

// Default level of zlib_compress; also what level -1 means.
inline constexpr int default_zlib_level = 6;

// Compress `size` bytes. `level` uses zlib's numbering:
//    0     store: uncompressed blocks, a valid stream a few bytes per 64 KiB larger than the input
//    1     fastest compression (greedy matching, short searches)
//    2..8  increasing effort: lazy matching, longer searches
//    9     smallest and slowest
//   -1     default_zlib_level (6)
// The numbers match zlib's but it is not the same algorithm: at the same level libstb and zlib
// differ in size and speed (see above).
// An empty input gives a valid stream. `data` may be null only when `size` is 0.
// Throws invalid_argument (level outside -1..9, null data), limit_error (input over 2 GiB),
// encode_error (out of memory).
STB_API std::vector<std::uint8_t> zlib_compress(const void* data, std::size_t size, int level = default_zlib_level);
STB_API std::vector<std::uint8_t> zlib_compress(const std::vector<std::uint8_t>& data, int level = default_zlib_level);

// Default output ceiling of zlib_decompress, the same 512 MiB as image decoding.
inline constexpr std::size_t default_zlib_max_bytes = std::size_t(1) << 29;

// Inflate one complete zlib stream. The input must be exactly the stream: a bad header, corrupt
// or truncated data, a wrong Adler-32 or bytes after the end throw decode_error.
//
// `max_bytes` bounds the OUTPUT. A small stream can expand a thousand-fold (a "zip bomb"), so the
// output buffer is grown in doublings and never allowed past `max_bytes` (rounded up by under a
// tenth of a percent): a stream that would produce more throws limit_error, having allocated no
// more than that. Nothing is reserved up front: a small stream costs a small allocation whatever
// max_bytes is. Peak memory is below 2 x max_bytes (the buffer, then the copy that is returned).
// Throws invalid_argument (null data), decode_error, limit_error (also for input over 2 GiB; a
// max_bytes above 2 GiB - 1 is clamped to it), std::bad_alloc.
STB_API std::vector<std::uint8_t> zlib_decompress(const void* data, std::size_t size,
                                          std::size_t max_bytes = default_zlib_max_bytes);
STB_API std::vector<std::uint8_t> zlib_decompress(const std::vector<std::uint8_t>& data,
                                          std::size_t max_bytes = default_zlib_max_bytes);

}  // namespace stb
