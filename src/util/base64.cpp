#include <array>
#include <stdexcept>

#include "stb/util.hpp"

namespace stb {
namespace {

// 0..63: the sextet a character stands for; the other values: white space, '=', anything else.
constexpr std::uint8_t k_space = 0xFD, k_pad = 0xFE, k_bad = 0xFF;

constexpr std::array<std::uint8_t, 256> make_decode_table() {
    std::array<std::uint8_t, 256> t{};
    for (auto& v : t) v = k_bad;
    for (int i = 0; i < 26; ++i) {
        t[std::size_t('A' + i)] = std::uint8_t(i);
        t[std::size_t('a' + i)] = std::uint8_t(26 + i);
    }
    for (int i = 0; i < 10; ++i) t[std::size_t('0' + i)] = std::uint8_t(52 + i);
    t[std::size_t('+')] = 62;
    t[std::size_t('/')] = 63;
    t[std::size_t('=')] = k_pad;
    for (const char c : {' ', '\t', '\n', '\f', '\r'}) t[std::size_t(static_cast<unsigned char>(c))] = k_space;
    return t;
}

constexpr auto decode_table = make_decode_table();

}  // namespace

std::optional<std::vector<std::uint8_t>> try_base64_decode(std::string_view text) {
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3 + 3);  // four characters make three bytes: never more room than that is needed

    std::size_t count = 0;  // sextets seen
    std::size_t pads = 0;   // '=' seen
    std::uint32_t acc = 0;  // bits not written yet
    int bits = 0;
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n; ++i) {
        // A clean group of four sextets at a byte boundary (no padding or white space before it) is three bytes
        // with nothing to carry between them: the common case, and most of the time.
        if (bits == 0 && pads == 0 && i + 4 <= n) {
            const std::uint8_t a = decode_table[static_cast<unsigned char>(text[i])];
            const std::uint8_t b = decode_table[static_cast<unsigned char>(text[i + 1])];
            const std::uint8_t c = decode_table[static_cast<unsigned char>(text[i + 2])];
            const std::uint8_t d = decode_table[static_cast<unsigned char>(text[i + 3])];
            if ((a | b | c | d) < 64) {
                out.push_back(std::uint8_t((a << 2) | (b >> 4)));
                out.push_back(std::uint8_t((b << 4) | (c >> 2)));
                out.push_back(std::uint8_t((c << 6) | d));
                count += 4;
                i += 3;
                continue;
            }
        }
        const std::uint8_t v = decode_table[static_cast<unsigned char>(text[i])];
        if (v < 64) {
            if (pads > 0) return std::nullopt;  // data after the padding
            acc = (acc << 6) | v;
            bits += 6;
            ++count;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(std::uint8_t(acc >> bits));
            }
        } else if (v == k_pad) {
            ++pads;
        } else if (v == k_bad) {
            return std::nullopt;
        }  // white space: skipped
    }
    if (count % 4 == 1) return std::nullopt;                   // one character is not even a byte
    if (pads != 0 && (pads > 2 || (count + pads) % 4 != 0)) return std::nullopt;  // padding that does not complete a group
    return out;
}

std::vector<std::uint8_t> base64_decode(std::string_view text) {
    auto out = try_base64_decode(text);
    if (!out) throw std::invalid_argument("base64_decode: not valid base64");
    return std::move(*out);
}

std::string base64_encode(const std::uint8_t* data, std::size_t size) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (size > 0 && !data) throw std::invalid_argument("base64_encode: null data");
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const std::uint32_t v = std::uint32_t(data[i]) << 16 | std::uint32_t(data[i + 1]) << 8 | data[i + 2];
        out.push_back(alphabet[v >> 18]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(alphabet[(v >> 6) & 63]);
        out.push_back(alphabet[v & 63]);
    }
    if (const std::size_t rest = size - i; rest > 0) {  // one or two bytes left
        const std::uint32_t v = std::uint32_t(data[i]) << 16 | (rest == 2 ? std::uint32_t(data[i + 1]) << 8 : 0u);
        out.push_back(alphabet[v >> 18]);
        out.push_back(alphabet[(v >> 12) & 63]);
        out.push_back(rest == 2 ? alphabet[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

}  // namespace stb
