#pragma once

// Internal. The only door to stb's zlib code: stb_image.h and stb_image_write.h are compiled once,
// in image.cpp, so the compressor and the inflater exist once in the library (PNG and the public
// zlib API share them). zlib.cpp has no stb includes; it calls these two functions.

#include <cstddef>

namespace stb::detail {

// stb's compressor (the one that writes PNG). level 0..10. Returns a malloc'd buffer (free() it),
// or null when out of memory.
unsigned char* stb_deflate(const unsigned char* data, int size, int level, int* out_size);

enum class inflate_status { ok, corrupt, too_large, out_of_memory };

// stb's inflater for one zlib stream. The output buffer starts at `initial` bytes and doubles as
// needed; it is never allowed past `ceiling` bytes (too_large instead). Returns a malloc'd buffer
// (free() it) on ok. stb does not verify the Adler-32 trailer: the caller does.
unsigned char* stb_inflate(const unsigned char* in, int size, std::size_t initial, std::size_t ceiling,
                           int* out_size, inflate_status* status);

}  // namespace stb::detail
