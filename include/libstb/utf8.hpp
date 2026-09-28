#pragma once

#include <string>
#include <string_view>

namespace libstb {

// Strict UTF-8 -> code points. Throws std::invalid_argument on malformed
// input: truncated or stray continuation bytes, overlong encodings,
// surrogates (U+D800..DFFF) and values above U+10FFFF.
std::u32string utf8_decode(std::string_view text);

}  // namespace libstb
