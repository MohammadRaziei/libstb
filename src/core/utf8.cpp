#include "libstb/utf8.hpp"

#include <stdexcept>

namespace libstb {

std::u32string utf8_decode(std::string_view s) {
    static const char32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};  // smallest value per length
    std::u32string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        std::size_t len;
        if (c < 0x80) {
            cp = c, len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F, len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F, len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07, len = 4;
        } else {
            throw std::invalid_argument("invalid UTF-8: bad lead byte");
        }
        if (i + len > n) throw std::invalid_argument("invalid UTF-8: truncated sequence");
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) throw std::invalid_argument("invalid UTF-8: bad continuation byte");
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < kMin[len]) throw std::invalid_argument("invalid UTF-8: overlong encoding");
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            throw std::invalid_argument("invalid UTF-8: code point out of range");
        out.push_back(cp);
        i += len;
    }
    return out;
}

}  // namespace libstb
