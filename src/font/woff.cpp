#include "font/woff.hpp"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <string>

#include "stb/error.hpp"
#include "stb/util.hpp"
#include "stb/zlib.hpp"
#include "util/log.hpp"

namespace stb::detail {

namespace {

constexpr std::uint32_t tag_woff = 0x774F4646u;  // "wOFF"
constexpr std::uint32_t tag_woff2 = 0x774F4632u;  // "wOF2"
constexpr std::uint32_t tag_ttcf = 0x74746366u;  // "ttcf"

constexpr std::size_t header_bytes = 44;  // WOFF header
constexpr std::size_t entry_bytes = 20;  // one WOFF table directory entry
constexpr std::size_t sfnt_header_bytes = 12;
constexpr std::size_t sfnt_entry_bytes = 16;

std::uint32_t be16(const std::uint8_t* p) noexcept { return (std::uint32_t(p[0]) << 8) | std::uint32_t(p[1]); }

std::uint32_t be32(const std::uint8_t* p) noexcept {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

void put16(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) noexcept {
    v[at] = std::uint8_t(x >> 8);
    v[at + 1] = std::uint8_t(x);
}

void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) noexcept {
    v[at] = std::uint8_t(x >> 24);
    v[at + 1] = std::uint8_t(x >> 16);
    v[at + 2] = std::uint8_t(x >> 8);
    v[at + 3] = std::uint8_t(x);
}

// A table tag for a message: printable ASCII as is, anything else '?'.
std::string tag_name(std::uint32_t tag) {
    std::string s(4, '?');
    for (int i = 0; i < 4; ++i) {
        const std::uint32_t c = (tag >> (24 - 8 * i)) & 0xFFu;
        if (c >= 0x20u && c < 0x7Fu) s[std::size_t(i)] = char(c);
    }
    return s;
}

struct table_entry {
    std::uint32_t tag = 0;
    std::uint32_t offset = 0;  // of the (maybe compressed) data, in the WOFF file
    std::uint32_t comp_length = 0;
    std::uint32_t orig_length = 0;
    std::uint32_t checksum = 0;
};

}  // namespace

web_font_kind web_font_signature(const std::uint8_t* data, std::size_t size) noexcept {
    if (data == nullptr || size < 4) return web_font_kind::none;
    const std::uint32_t sig = be32(data);
    if (sig == tag_woff) return web_font_kind::woff1;
    if (sig == tag_woff2) return web_font_kind::woff2;
    return web_font_kind::none;
}

std::vector<std::uint8_t> woff1_to_sfnt(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < header_bytes) throw decode_error("WOFF: file is shorter than its header");
    if (be32(data) != tag_woff) throw decode_error("WOFF: bad signature");
    const std::uint32_t flavor = be32(data + 4);
    if (flavor == tag_ttcf) throw decode_error("WOFF: font collections are not supported");
    if (be32(data + 8) != size) throw decode_error("WOFF: the length field does not match the file size");
    const std::size_t count = be16(data + 12);
    if (count == 0) throw decode_error("WOFF: the font has no tables");
    const std::size_t dir_end = header_bytes + count * entry_bytes;
    if (dir_end > size) throw decode_error("WOFF: the table directory runs past the end of the file");

    // The directory: sorted by tag (sfnt readers binary-search it), every table inside the file
    // after the directory, never larger compressed than not.
    std::vector<table_entry> tables(count);
    std::uint64_t sfnt_size = sfnt_header_bytes + sfnt_entry_bytes * std::uint64_t(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* e = data + header_bytes + i * entry_bytes;
        table_entry& t = tables[i];
        t.tag = be32(e);
        t.offset = be32(e + 4);
        t.comp_length = be32(e + 8);
        t.orig_length = be32(e + 12);
        t.checksum = be32(e + 16);
        const std::string name = tag_name(t.tag);
        if (i > 0 && t.tag <= tables[i - 1].tag)
            throw decode_error(sformat("WOFF: the table directory is not sorted by tag (at '%s')", name.c_str()));
        if (t.comp_length > t.orig_length)
            throw decode_error(sformat("WOFF: table '%s' is larger compressed than not", name.c_str()));
        if (t.offset < dir_end || std::uint64_t(t.offset) + t.comp_length > size)
            throw decode_error(sformat("WOFF: table '%s' is outside the file", name.c_str()));
        sfnt_size += (std::uint64_t(t.orig_length) + 3u) & ~std::uint64_t(3);  // each table is padded to 4 bytes
    }
    if (sfnt_size > woff1_max_sfnt_bytes)
        throw limit_error(sformat("WOFF: the font would unpack to %llu bytes (limit %zu)",
                                  static_cast<unsigned long long>(sfnt_size), woff1_max_sfnt_bytes));

    // No two tables share bytes (a table with no bytes shares none).
    std::vector<std::size_t> by_offset;
    by_offset.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        if (tables[i].comp_length > 0) by_offset.push_back(i);
    std::sort(by_offset.begin(), by_offset.end(),
              [&tables](std::size_t a, std::size_t b) { return tables[a].offset < tables[b].offset; });
    for (std::size_t k = 1; k < by_offset.size(); ++k) {
        const table_entry& prev = tables[by_offset[k - 1]];
        if (std::uint64_t(prev.offset) + prev.comp_length > tables[by_offset[k]].offset)
            throw decode_error(sformat("WOFF: tables '%s' and '%s' overlap", tag_name(prev.tag).c_str(),
                                       tag_name(tables[by_offset[k]].tag).c_str()));
    }

    // The sfnt: its header, a directory of (tag, checksum, offset, length) and the tables in
    // directory order, each padded with zeros to 4 bytes. The output grows with the data really
    // produced: nothing is reserved on the strength of a length field alone, so a small file
    // cannot make us allocate a lot before it has proven it holds that much.
    std::vector<std::uint8_t> out(sfnt_header_bytes + sfnt_entry_bytes * count, 0);
    out.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(sfnt_size, std::uint64_t(size) * 8u)));
    std::uint32_t entry_selector = 0;
    while ((std::size_t(2) << entry_selector) <= count) ++entry_selector;
    const std::uint32_t search_range = (std::uint32_t(1) << entry_selector) * 16u;
    put32(out, 0, flavor);
    put16(out, 4, std::uint32_t(count));
    put16(out, 6, search_range & 0xFFFFu);  // wraps for a (nonsensical) 4096 tables or more, like any sfnt writer
    put16(out, 8, entry_selector);
    put16(out, 10, (std::uint32_t(count) * 16u - search_range) & 0xFFFFu);

    for (std::size_t i = 0; i < count; ++i) {
        const table_entry& t = tables[i];
        const std::uint8_t* src = data + t.offset;
        const std::size_t at = sfnt_header_bytes + i * sfnt_entry_bytes;
        put32(out, at, t.tag);
        put32(out, at + 4, t.checksum);
        put32(out, at + 8, std::uint32_t(out.size()));  // fits: sfnt_size <= 512 MiB
        put32(out, at + 12, t.orig_length);

        if (t.comp_length == t.orig_length) {
            out.insert(out.end(), src, src + t.comp_length);  // stored as it is
        } else {
            const std::string name = tag_name(t.tag);
            std::vector<std::uint8_t> raw;
            try {
                raw = zlib_decompress(src, t.comp_length, t.orig_length);
            } catch (const limit_error&) {
                throw decode_error(sformat("WOFF: table '%s' unpacks to more than its origLength", name.c_str()));
            } catch (const decode_error& ex) {
                throw decode_error(sformat("WOFF: table '%s' is damaged: %s", name.c_str(), ex.what()));
            }
            if (raw.size() != t.orig_length)  // zlib_decompress may let a stream run a little past its bound
                throw decode_error(sformat("WOFF: table '%s' unpacks to a different size than its origLength", name.c_str()));
            out.insert(out.end(), raw.begin(), raw.end());
        }
        out.resize((out.size() + 3u) & ~std::size_t(3), 0);
    }

    loggers::font().debug("WOFF 1.0: %zu table(s), %zu bytes unpacked to a %zu-byte font", count, size, out.size());
    return out;
}

}  // namespace stb::detail
