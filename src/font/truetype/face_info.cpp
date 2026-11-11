// The family name and the bold / italic flags of a font, from its `name`, `OS/2` and `head` tables.
// Every read goes through otl::byte_view, so a damaged table gives an empty name or false flags
// instead of reading outside the file.

#include <cstddef>

#include "font/truetype/truetype.hpp"

namespace stb::truetype {

using otl::byte_view;
using otl::make_tag;

namespace {

constexpr std::uint16_t name_family = 1;                // the family a style-linked group shares ("Roboto Light")
constexpr std::uint16_t name_typographic_family = 16;   // the family of the whole design ("Roboto"), if the font has one

// Mac OS Roman bytes 0x80..0xFF as Unicode code points.
constexpr char16_t mac_roman_high[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xF0 | (cp >> 18)));
        out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

// UTF-16 big endian (the Windows and Unicode platforms). A lone surrogate becomes U+FFFD.
std::string from_utf16be(byte_view s) {
    std::string out;
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
        char32_t c = s.u16(i);
        if (c >= 0xD800 && c < 0xDC00) {
            const char32_t lo = i + 3 < s.size() ? s.u16(i + 2) : 0;
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xDC00 && c < 0xE000) {
            c = 0xFFFD;
        }
        append_utf8(out, c);
    }
    return out;
}

std::string from_mac_roman(byte_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const std::uint8_t b = s.u8(i);
        append_utf8(out, b < 0x80 ? char32_t(b) : char32_t(mac_roman_high[b - 0x80]));
    }
    return out;
}

// How much a record is worth for a family name; 0 = unusable. English on the Windows platform is
// what almost every font has and what other font loaders pick, then any other Windows language,
// then the Unicode platform, then Mac Roman.
int record_rank(unsigned platform, unsigned encoding, unsigned language) {
    if (platform == 3 && (encoding == 1 || encoding == 10)) return language == 0x0409 ? 5 : 4;
    if (platform == 0) return 3;
    if (platform == 1 && encoding == 0) return language == 0 ? 2 : 1;
    return 0;
}

// The best-ranked record of `name_id`, decoded to UTF-8; empty if there is none.
std::string find_name(byte_view name, std::uint16_t name_id) {
    const unsigned count = name.u16(2);
    const std::size_t strings = name.u16(4);
    int best = 0;
    std::string result;
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t rec = 6 + 12 * std::size_t(i);
        if (name.u16(rec + 6) != name_id) continue;
        const unsigned platform = name.u16(rec), encoding = name.u16(rec + 2), language = name.u16(rec + 4);
        const int rank = record_rank(platform, encoding, language);
        if (rank <= best) continue;
        const byte_view text = name.sub(strings + name.u16(rec + 10), name.u16(rec + 8));
        if (text.empty()) continue;
        best = rank;
        result = platform == 1 ? from_mac_roman(text) : from_utf16be(text);
    }
    return result;
}

}  // namespace

bool read_face_info(byte_view file, std::uint32_t font_start, face_info& out) {
    out = face_info();
    const auto table = [&](char a, char b, char c, char d) {
        return otl::find_table(file, font_start, make_tag(a, b, c, d));
    };
    const byte_view head = table('h', 'e', 'a', 'd');
    const byte_view os2 = table('O', 'S', '/', '2');
    if (head.empty() && os2.empty()) return false;

    const byte_view name = table('n', 'a', 'm', 'e');
    if (!name.empty()) {
        out.legacy_family_name = find_name(name, name_family);
        out.family_name = find_name(name, name_typographic_family);
        if (out.family_name.empty()) out.family_name = out.legacy_family_name;
        if (out.legacy_family_name.empty()) out.legacy_family_name = out.family_name;
    }

    // OS/2 fsSelection (offset 62): bit 0 italic, bit 5 bold, bit 9 oblique. A version 0 table
    // that stops before it, or no table, falls back to head.macStyle (offset 44): bit 0 bold, bit 1 italic.
    if (os2.size() >= 64) {
        const unsigned selection = os2.u16(62);
        out.bold = (selection & 0x20u) != 0;
        out.italic = (selection & 0x01u) != 0 || (selection & 0x200u) != 0;
    } else {
        const unsigned mac_style = head.u16(44);
        out.bold = (mac_style & 0x01u) != 0;
        out.italic = (mac_style & 0x02u) != 0;
    }

    // OS/2 usWeightClass (offset 4) and usWidthClass (offset 6); a value outside the defined range
    // (or no table) is as if the font had said nothing.
    out.weight = out.bold ? 700 : 400;
    if (os2.size() >= 8) {
        const unsigned weight = os2.u16(4), width = os2.u16(6);
        if (weight >= 1 && weight <= 1000) out.weight = int(weight);
        if (width >= 1 && width <= 9) out.width = int(width);
    }
    return true;
}

}  // namespace stb::truetype
