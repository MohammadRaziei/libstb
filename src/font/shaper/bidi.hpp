// Unicode bidirectional algorithm (UAX #9) for one line, without explicit embeddings or isolates.
//
// Internal header: included only from src/font/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace stb::bidi {

enum class cls : std::uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, WS, ON };

cls classify(char32_t cp);
// The code point drawn in place of `cp` inside a right-to-left run (brackets); `cp` if none.
char32_t mirrored(char32_t cp);
// True if the text has a right-to-left letter or an Arabic-Indic digit.
bool has_rtl(std::u32string_view text);

// The paragraph direction: automatic is that of the first strong character (UAX #9, P2 and P3).
enum class base_dir : std::uint8_t { automatic, ltr, rtl };

struct result {
    std::vector<std::uint32_t> order;  // visual position -> logical index
    std::vector<std::uint8_t> level;   // embedding level of every logical index
};

// Resolves one line given the class of each logical character. A character of class NSM stays
// right after the character before it (a letter and its marks travel together). `base` forces the
// paragraph direction instead of reading it off the text.
result resolve(const std::vector<cls>& original, base_dir base = base_dir::automatic);

}  // namespace stb::bidi
