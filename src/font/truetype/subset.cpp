// A font file with only some of the glyphs of another (TrueType outlines): what a PDF writer embeds so that the file
// does not carry a whole font. The glyphs are numbered again, so a caller keeps its own glyph indices in a map
// (font_subset::new_index) and the tables that are indexed by glyph (loca, hmtx) have no gaps.
//
// Tables written: head, hhea, maxp, hmtx, loca, glyf, a one-segment cmap (so that the result loads in libstb and
// in any other reader that insists on a character map; it maps nothing) and, when the original has them, cvt,
// fpgm and prep (the hinting programs of the glyphs stay valid because the points are copied as they are).
// Not written: GSUB, GPOS, kern, name, post, OS/2 and everything else: the text was shaped before the subset was made.
//
// A font is untrusted input: every offset is checked through otl::byte_view and a glyph that does not fit in its
// table is an empty glyph, as in face::glyph_data.

#include <algorithm>

#include "font/truetype/truetype.hpp"
#include "stb/error.hpp"

namespace stb::truetype {

using otl::byte_view;
using otl::make_tag;

namespace {

constexpr std::uint32_t tag_cmap = make_tag('c', 'm', 'a', 'p');
constexpr std::uint32_t tag_cvt = make_tag('c', 'v', 't', ' ');
constexpr std::uint32_t tag_fpgm = make_tag('f', 'p', 'g', 'm');
constexpr std::uint32_t tag_glyf = make_tag('g', 'l', 'y', 'f');
constexpr std::uint32_t tag_head = make_tag('h', 'e', 'a', 'd');
constexpr std::uint32_t tag_hhea = make_tag('h', 'h', 'e', 'a');
constexpr std::uint32_t tag_hmtx = make_tag('h', 'm', 't', 'x');
constexpr std::uint32_t tag_loca = make_tag('l', 'o', 'c', 'a');
constexpr std::uint32_t tag_maxp = make_tag('m', 'a', 'x', 'p');
constexpr std::uint32_t tag_prep = make_tag('p', 'r', 'e', 'p');

constexpr std::size_t head_size = 54;  // the head table is this long, and nothing after it is read
constexpr std::size_t hhea_size = 36;

void put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(std::uint8_t(v >> 8));
    out.push_back(std::uint8_t(v));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    put16(out, v >> 16);
    put16(out, v & 0xFFFFu);
}

void set16(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t v) {
    out[at] = std::uint8_t(v >> 8);
    out[at + 1] = std::uint8_t(v);
}

void set32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t v) {
    set16(out, at, v >> 16);
    set16(out, at + 2, v & 0xFFFFu);
}

void pad4(std::vector<std::uint8_t>& out) {
    while (out.size() % 4 != 0) out.push_back(0);
}

// The sum of the big-endian 32-bit words of `bytes`, the last word padded with zeros (the sfnt checksum).
std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        std::uint32_t word = 0;
        for (std::size_t k = 0; k < 4; ++k) word = (word << 8) | (i + k < bytes.size() ? bytes[i + k] : 0u);
        sum += word;
    }
    return sum;
}

std::vector<std::uint8_t> copy_of(byte_view v) { return std::vector<std::uint8_t>(v.data(), v.data() + v.size()); }

// The bytes of glyph `g` in `glyf`, empty for a glyph without an outline or whose entry in `loca` does not fit.
byte_view glyph_bytes(byte_view loca, byte_view glyf, int loca_format, std::uint32_t g) {
    std::uint32_t start, end;
    if (loca_format == 0) {
        start = loca.u16(std::size_t(g) * 2) * 2u;
        end = loca.u16(std::size_t(g) * 2 + 2) * 2u;
    } else {
        start = loca.u32(std::size_t(g) * 4);
        end = loca.u32(std::size_t(g) * 4 + 4);
    }
    if (end <= start || end > glyf.size()) return {};
    const byte_view bytes = glyf.sub(start, end - start);
    return bytes.size() < 10 ? byte_view() : bytes;  // the header (contour count and box) alone is 10 bytes
}

// Calls `on_component(offset of the component's glyph index, the glyph index)` for each component of a composite
// glyph. Stops at the end of the data if the list is cut short.
template <class F>
void each_component(byte_view g, F on_component) {
    std::size_t p = 10;
    for (bool more = true; more && g.has(p, 4);) {
        const unsigned flags = g.u16(p);
        on_component(p + 2, g.u16(p + 2));
        p += 4 + ((flags & 0x0001u) ? 4 : 2);  // ARG_1_AND_2_ARE_WORDS
        if (flags & 0x0008u) p += 2;           // WE_HAVE_A_SCALE
        else if (flags & 0x0040u) p += 4;      // WE_HAVE_AN_X_AND_Y_SCALE
        else if (flags & 0x0080u) p += 8;      // WE_HAVE_A_TWO_BY_TWO
        more = (flags & 0x0020u) != 0;         // MORE_COMPONENTS
    }
}

// A cmap with one format 4 subtable (Windows, Unicode BMP) and one segment, 0xFFFF, which maps to glyph 0.
std::vector<std::uint8_t> empty_cmap() {
    std::vector<std::uint8_t> t;
    put16(t, 0);  // version
    put16(t, 1);  // one encoding record
    put16(t, 3);  // platform: Windows
    put16(t, 1);  // encoding: Unicode BMP
    put32(t, 12);
    put16(t, 4);   // format
    put16(t, 24);  // length
    put16(t, 0);   // language
    put16(t, 2);   // segCountX2
    put16(t, 2);   // searchRange
    put16(t, 0);   // entrySelector
    put16(t, 0);   // rangeShift
    put16(t, 0xFFFF);  // endCode
    put16(t, 0);       // reservedPad
    put16(t, 0xFFFF);  // startCode
    put16(t, 1);       // idDelta: 0xFFFF + 1 = glyph 0
    put16(t, 0);       // idRangeOffset
    return t;
}

struct table {
    std::uint32_t tag;
    std::vector<std::uint8_t> bytes;
};

}  // namespace

bool can_subset(const face& f) {
    if (f.is_varied() || f.num_glyphs() <= 0) return false;
    const auto table_of = [&](std::uint32_t tag) { return otl::find_table(f.file(), std::uint32_t(f.font_start()), tag); };
    return !table_of(tag_glyf).empty() && !table_of(tag_loca).empty() && table_of(tag_head).size() >= head_size &&
           table_of(tag_head).u16(50) <= 1 && table_of(tag_hhea).size() >= hhea_size && !table_of(tag_hmtx).empty() &&
           table_of(tag_maxp).size() >= 6;
}

subset_output subset(const face& f, const std::vector<std::uint32_t>& glyphs) {
    if (!can_subset(f)) throw error("this font cannot be subset (it has no TrueType outlines, or a variation is applied)");
    const std::uint32_t total = std::uint32_t(f.num_glyphs());
    const auto table_of = [&](std::uint32_t tag) { return otl::find_table(f.file(), std::uint32_t(f.font_start()), tag); };
    const byte_view head = table_of(tag_head), hhea = table_of(tag_hhea), maxp = table_of(tag_maxp);
    const byte_view loca = table_of(tag_loca), glyf = table_of(tag_glyf);
    const int loca_format = head.u16(50);

    // The glyphs to keep: those asked for, .notdef, and (a composite names others) everything they are made of.
    std::vector<bool> keep(total, false);
    std::vector<std::uint32_t> pending{0};
    keep[0] = true;
    for (const std::uint32_t g : glyphs) {
        if (g >= total) throw error("subset: glyph index past the end of the font");
        if (!keep[g]) {
            keep[g] = true;
            pending.push_back(g);
        }
    }
    while (!pending.empty()) {
        const std::uint32_t g = pending.back();
        pending.pop_back();
        const byte_view bytes = glyph_bytes(loca, glyf, loca_format, g);
        if (bytes.empty() || bytes.i16(0) >= 0) continue;
        each_component(bytes, [&](std::size_t, std::uint32_t component) {
            if (component >= total) throw decode_error("subset: a composite glyph names a glyph the font does not have");
            if (!keep[component]) {
                keep[component] = true;
                pending.push_back(component);
            }
        });
    }

    subset_output out;
    std::vector<std::uint32_t> new_index(total, 0xFFFFFFFFu);
    for (std::uint32_t g = 0; g < total; ++g) {
        if (!keep[g]) continue;
        new_index[g] = std::uint32_t(out.kept.size());
        out.kept.push_back(g);
    }
    const std::uint32_t count = std::uint32_t(out.kept.size());

    // glyf, and where each glyph starts in it. A composite names its parts by their new index.
    std::vector<std::uint8_t> new_glyf;
    std::vector<std::uint32_t> starts;
    starts.reserve(count + 1);
    for (const std::uint32_t g : out.kept) {
        starts.push_back(std::uint32_t(new_glyf.size()));
        const byte_view bytes = glyph_bytes(loca, glyf, loca_format, g);
        if (bytes.empty()) continue;
        const std::size_t at = new_glyf.size();
        new_glyf.insert(new_glyf.end(), bytes.data(), bytes.data() + bytes.size());
        if (bytes.i16(0) < 0)
            each_component(bytes, [&](std::size_t offset, std::uint32_t component) { set16(new_glyf, at + offset, new_index[component]); });
        pad4(new_glyf);
    }
    starts.push_back(std::uint32_t(new_glyf.size()));
    // Every glyph may be empty (only a .notdef without an outline was asked for), but a table of length 0 is read as a
    // missing table: four unused bytes keep the glyf table there and no entry of loca points into them.
    if (new_glyf.empty()) new_glyf.assign(4, 0);

    // ponytail: the short loca format is used whenever it fits (offsets / 2 in 16 bits); a glyph's bytes keep the
    // padding the original had, no glyph is trimmed to its real end. Trimming is a byte or two per glyph.
    const bool short_loca = new_glyf.size() <= 0x1FFFEu;
    std::vector<std::uint8_t> new_loca;
    for (const std::uint32_t s : starts) {
        if (short_loca) put16(new_loca, s / 2);
        else put32(new_loca, s);
    }

    // Every kept glyph gets a full (advance, left side bearing) pair, so numberOfHMetrics is the glyph count.
    std::vector<std::uint8_t> new_hmtx;
    for (const std::uint32_t g : out.kept) {
        int advance = 0, bearing = 0;
        f.h_metrics(int(g), advance, bearing);
        put16(new_hmtx, std::uint32_t(advance) & 0xFFFFu);
        put16(new_hmtx, std::uint32_t(bearing) & 0xFFFFu);
    }

    std::vector<std::uint8_t> new_head = copy_of(head.sub(0, head_size));
    set32(new_head, 8, 0);  // checkSumAdjustment, set once the whole file is known
    set16(new_head, 50, short_loca ? 0 : 1);

    std::vector<std::uint8_t> new_hhea = copy_of(hhea.sub(0, hhea_size));
    set16(new_hhea, 34, count);

    std::vector<std::uint8_t> new_maxp = copy_of(maxp);
    set16(new_maxp, 4, count);

    std::vector<table> tables;
    tables.push_back({tag_cmap, empty_cmap()});
    tables.push_back({tag_glyf, std::move(new_glyf)});
    tables.push_back({tag_head, std::move(new_head)});
    tables.push_back({tag_hhea, std::move(new_hhea)});
    tables.push_back({tag_hmtx, std::move(new_hmtx)});
    tables.push_back({tag_loca, std::move(new_loca)});
    tables.push_back({tag_maxp, std::move(new_maxp)});
    for (const std::uint32_t tag : {tag_cvt, tag_fpgm, tag_prep}) {
        const byte_view program = table_of(tag);
        if (!program.empty()) tables.push_back({tag, copy_of(program)});
    }
    std::sort(tables.begin(), tables.end(), [](const table& a, const table& b) { return a.tag < b.tag; });

    // The file: the offset table, the directory, then the tables on 4-byte boundaries.
    const std::uint32_t n = std::uint32_t(tables.size());
    std::uint32_t power = 1, log2_power = 0;
    while (power * 2 <= n) {
        power *= 2;
        ++log2_power;
    }
    std::vector<std::uint8_t>& file = out.data;
    put32(file, 0x00010000u);
    put16(file, n);
    put16(file, power * 16);
    put16(file, log2_power);
    put16(file, n * 16 - power * 16);
    std::size_t offset = 12 + std::size_t(n) * 16;
    for (const table& t : tables) {
        put32(file, t.tag);
        put32(file, checksum(t.bytes));
        put32(file, std::uint32_t(offset));
        put32(file, std::uint32_t(t.bytes.size()));
        offset += (t.bytes.size() + 3) / 4 * 4;
    }
    std::size_t head_at = 0;
    for (const table& t : tables) {
        if (t.tag == tag_head) head_at = file.size();
        file.insert(file.end(), t.bytes.begin(), t.bytes.end());
        pad4(file);
    }
    set32(file, head_at + 8, 0xB1B0AFBAu - checksum(file));  // the whole file was summed with the adjustment at 0
    return out;
}

}  // namespace stb::truetype
