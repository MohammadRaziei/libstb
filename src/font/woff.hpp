#pragma once

// Internal: WOFF 1.0 (https://www.w3.org/TR/WOFF/) to sfnt. A WOFF file is a TrueType/OpenType
// font whose tables are zlib-compressed one by one, behind a 44-byte header; unpacking it gives
// the plain font file the rest of libstb already reads. font::from_memory and font::open do this
// when a file starts with "wOFF".

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stb::detail {

enum class web_font_kind { none, woff1, woff2 };

// Looks at the first four bytes only.
web_font_kind web_font_signature(const std::uint8_t* data, std::size_t size) noexcept;

// The largest font woff1_to_sfnt produces: what a font may expand to, bounded the same way as
// zlib_decompress bounds its output (a small file must not be able to ask for gigabytes).
inline constexpr std::size_t woff1_max_sfnt_bytes = std::size_t(1) << 29;

// Unpacks one complete WOFF 1.0 file into an sfnt (the bytes of a .ttf / .otf). The table data is
// byte for byte what the original font had; the table directory is rebuilt from the WOFF one.
// The header's totalSfntSize and the metadata and private blocks are not used.
// Throws decode_error (a damaged or non-conforming file), limit_error (the font would be over
// woff1_max_sfnt_bytes), std::bad_alloc.
std::vector<std::uint8_t> woff1_to_sfnt(const std::uint8_t* data, std::size_t size);

}  // namespace stb::detail
