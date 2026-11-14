#include "stb/util.hpp"

#include <stdexcept>

namespace stb {

namespace {

// Decodes the sequence that starts at s[i]. Returns nullptr when it is well formed (cp and len are set), else why not.
const char* decode_one(std::string_view s, std::size_t i, char32_t& cp, std::size_t& len) {
    static const char32_t min_for_length[5] = {0, 0, 0x80, 0x800, 0x10000};  // smallest value per length
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        cp = c, len = 1;
        return nullptr;
    }
    if ((c & 0xE0) == 0xC0) {
        cp = c & 0x1F, len = 2;
    } else if ((c & 0xF0) == 0xE0) {
        cp = c & 0x0F, len = 3;
    } else if ((c & 0xF8) == 0xF0) {
        cp = c & 0x07, len = 4;
    } else {
        return "invalid UTF-8: bad lead byte";
    }
    if (i + len > s.size()) return "invalid UTF-8: truncated sequence";
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return "invalid UTF-8: bad continuation byte";
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < min_for_length[len]) return "invalid UTF-8: overlong encoding";
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return "invalid UTF-8: code point out of range";
    return nullptr;
}

}  // namespace

std::u32string utf8_decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        char32_t cp;
        std::size_t len;
        if (const char* why = decode_one(s, i, cp, len)) throw std::invalid_argument(why);
        out.push_back(cp);
        i += len;
    }
    return out;
}

// ponytail: one U+FFFD per bad byte, then decoding resumes at the next byte. The WHATWG decoder gives one U+FFFD per
// "maximal subpart" of a truncated sequence; take that if a caller ever compares replacement counts with a browser.
std::u32string utf8_decode_lossy(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        char32_t cp;
        std::size_t len;
        if (decode_one(s, i, cp, len)) {
            out.push_back(U'\uFFFD');
            ++i;
        } else {
            out.push_back(cp);
            i += len;
        }
    }
    return out;
}

void utf8_append(std::string& out, char32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string utf8_encode(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char32_t cp : text) utf8_append(out, cp);
    return out;
}

}  // namespace stb
