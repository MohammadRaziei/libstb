// A TrueType / OpenType font file: tables, character map, metrics, kerning and glyph outlines.
// Written from the algorithms of stb_truetype.h v1.26 (public domain / MIT, Sean Barrett and
// contributors) and giving the same results for every valid font.

#include <algorithm>
#include <cmath>

#include "font/truetype/truetype.hpp"

namespace stb::truetype {

using otl::byte_view;
using otl::make_tag;

namespace {

constexpr int max_composite_depth = 16;  // a glyph made of glyphs made of glyphs ...; fonts never go this deep

bool is_font_header(byte_view f) {
    const std::uint32_t v = f.u32(0);
    return v == 0x31000000u  // '1' 0 0 0: TrueType 1
           || v == make_tag('t', 'y', 'p', '1') || v == make_tag('O', 'T', 'T', 'O')  // OpenType with CFF
           || v == 0x00010000u                                                         // OpenType 1.0
           || v == make_tag('t', 'r', 'u', 'e');                                       // Apple TrueType
}

vertex make_vertex(std::uint8_t type, std::int32_t x, std::int32_t y, std::int32_t cx, std::int32_t cy) {
    vertex v;
    v.type = type;
    v.x = static_cast<std::int16_t>(x);
    v.y = static_cast<std::int16_t>(y);
    v.cx = static_cast<std::int16_t>(cx);
    v.cy = static_cast<std::int16_t>(cy);
    return v;
}

// The end of a TrueType contour: back to its start point, through the pending off-curve point.
void close_contour(std::vector<vertex>& out, bool was_off, bool start_off, std::int32_t sx, std::int32_t sy,
                   std::int32_t scx, std::int32_t scy, std::int32_t cx, std::int32_t cy) {
    if (start_off) {
        if (was_off) out.push_back(make_vertex(vertex::curve, (cx + scx) >> 1, (cy + scy) >> 1, cx, cy));
        out.push_back(make_vertex(vertex::curve, sx, sy, scx, scy));
    } else if (was_off) {
        out.push_back(make_vertex(vertex::curve, sx, sy, cx, cy));
    } else {
        out.push_back(make_vertex(vertex::line, sx, sy, 0, 0));
    }
}

}  // namespace

// ---------------------------------------------------------------------------------- the file

int face::offset_for_index(byte_view file, int index) {
    if (is_font_header(file)) return index == 0 ? 0 : -1;  // a plain font has one
    if (file.u32(0) == make_tag('t', 't', 'c', 'f')) {
        const std::uint32_t version = file.u32(4);
        if (version == 0x00010000u || version == 0x00020000u) {
            const std::int32_t n = static_cast<std::int32_t>(file.u32(8));
            if (index < 0 || index >= n) return -1;
            return static_cast<int>(file.u32(12 + std::size_t(index) * 4));
        }
    }
    return -1;
}

bool face::init(byte_view file, int font_start) {
    *this = face();
    file_ = file;
    font_start_ = font_start;
    const auto table = [&](char a, char b, char c, char d) {
        return otl::find_table(file, std::uint32_t(font_start), make_tag(a, b, c, d));
    };
    const byte_view cmap = table('c', 'm', 'a', 'p');
    loca_ = table('l', 'o', 'c', 'a');
    head_ = table('h', 'e', 'a', 'd');
    glyf_ = table('g', 'l', 'y', 'f');
    hhea_ = table('h', 'h', 'e', 'a');
    hmtx_ = table('h', 'm', 't', 'x');
    kern_ = table('k', 'e', 'r', 'n');
    gpos_ = table('G', 'P', 'O', 'S');

    if (cmap.empty() || head_.empty() || hhea_.empty() || hmtx_.empty()) return false;
    if (!glyf_.empty()) {
        if (loca_.empty()) return false;  // TrueType outlines need their index
    } else if (!init_cff(font_start)) {   // OpenType with CFF outlines
        return false;
    }

    const byte_view maxp = table('m', 'a', 'x', 'p');
    num_glyphs_ = maxp.empty() ? 0xffff : maxp.u16(4);

    // Pick a Unicode character map: Microsoft Unicode (BMP or full), or any Unicode-platform one.
    // The last suitable record wins.
    const unsigned num_tables = cmap.u16(2);
    for (unsigned i = 0; i < num_tables; ++i) {
        const std::size_t record = 4 + 8 * std::size_t(i);
        const unsigned platform = cmap.u16(record), encoding = cmap.u16(record + 2);
        const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
        if (unicode) cmap_ = cmap.from(cmap.u32(record + 4));
    }
    if (cmap_.empty()) return false;

    index_to_loc_format_ = head_.u16(50);
    return true;
}

int face::glyph_index(int codepoint) const {
    const byte_view& m = cmap_;
    const unsigned format = m.u16(0);
    if (format == 0) {  // Apple byte encoding
        const int bytes = m.u16(2);
        return codepoint >= 0 && codepoint < bytes - 6 ? m.u8(6 + std::size_t(codepoint)) : 0;
    }
    if (format == 6) {  // trimmed table
        const std::uint32_t first = m.u16(6), count = m.u16(8), cp = static_cast<std::uint32_t>(codepoint);
        return cp >= first && cp < first + count ? m.u16(10 + std::size_t(cp - first) * 2) : 0;
    }
    if (format == 4) {  // segment mapping to delta values: binary search over the segments
        const unsigned seg_count = m.u16(6) >> 1;
        unsigned search_range = m.u16(8) >> 1;
        unsigned entry_selector = m.u16(10);
        const unsigned range_shift = m.u16(12) >> 1;
        if (codepoint < 0 || codepoint > 0xffff) return 0;
        const std::size_t end_count = 14;
        std::size_t search = end_count;
        // the segments lie in endCount .. endCount + segCount, but searchRange is the nearest power of two
        if (unsigned(codepoint) >= m.u16(search + range_shift * 2)) search += range_shift * 2;
        search -= 2;  // bias to find the smallest
        while (entry_selector) {
            search_range >>= 1;
            const unsigned end = m.u16(search + search_range * 2);
            if (unsigned(codepoint) > end) search += search_range * 2;
            --entry_selector;
        }
        search += 2;
        const unsigned item = static_cast<std::uint16_t>((search - end_count) >> 1);
        const unsigned start = m.u16(14 + seg_count * 2 + 2 + 2 * std::size_t(item));
        const unsigned last = m.u16(end_count + 2 * std::size_t(item));
        if (unsigned(codepoint) < start || unsigned(codepoint) > last) return 0;
        const unsigned offset = m.u16(14 + seg_count * 6 + 2 + 2 * std::size_t(item));
        if (offset == 0)
            return static_cast<std::uint16_t>(codepoint + m.i16(14 + seg_count * 4 + 2 + 2 * std::size_t(item)));
        return m.u16(offset + (unsigned(codepoint) - start) * 2 + 14 + seg_count * 6 + 2 + 2 * std::size_t(item));
    }
    if (format == 12 || format == 13) {  // groups of consecutive code points
        const std::int32_t groups = static_cast<std::int32_t>(m.u32(12));
        std::int32_t low = 0, high = groups;
        const std::uint32_t cp = static_cast<std::uint32_t>(codepoint);
        while (low < high) {
            const std::int32_t mid = low + ((high - low) >> 1);
            const std::size_t group = 16 + std::size_t(mid) * 12;
            const std::uint32_t start_char = m.u32(group), end_char = m.u32(group + 4);
            if (cp < start_char) {
                high = mid;
            } else if (cp > end_char) {
                low = mid + 1;
            } else {
                const std::uint32_t start_glyph = m.u32(group + 8);
                return static_cast<int>(format == 12 ? start_glyph + cp - start_char : start_glyph);
            }
        }
    }
    return 0;  // not found, or a format this reader does not know (format 2: CJK high-byte mapping)
}

void face::v_metrics(int& ascent, int& descent, int& line_gap) const {
    ascent = hhea_.i16(4);
    descent = hhea_.i16(6);
    line_gap = hhea_.i16(8);
}

void face::h_metrics(int glyph, int& advance, int& left_bearing) const {
    const int long_metrics = hhea_.u16(34);
    if (glyph < long_metrics) {
        advance = hmtx_.i16(4 * std::size_t(glyph));
        left_bearing = hmtx_.i16(4 * std::size_t(glyph) + 2);
    } else {  // the last advance repeats, only the bearings are listed
        advance = hmtx_.i16(4 * std::size_t(long_metrics - 1));
        left_bearing = hmtx_.i16(4 * std::size_t(long_metrics) + 2 * std::size_t(glyph - long_metrics));
    }
}

float face::scale_for_pixel_height(float height) const {
    const int font_height = hhea_.i16(4) - hhea_.i16(6);
    return height / static_cast<float>(font_height);
}

// ---------------------------------------------------------------------------------- kerning

// The first `kern` subtable, if it is horizontal and format 0 (a sorted list of pairs).
int face::kern_pair_advance(int left, int right) const {
    if (kern_.empty() || kern_.u16(2) < 1 || kern_.u16(8) != 1) return 0;
    std::int64_t l = 0, r = std::int64_t(kern_.u16(10)) - 1;
    const std::uint32_t needle = static_cast<std::uint32_t>(left) << 16 | static_cast<std::uint32_t>(right);
    while (l <= r) {
        const std::int64_t m = (l + r) >> 1;
        const std::uint32_t straw = kern_.u32(18 + std::size_t(m) * 6);
        if (needle < straw) r = m - 1;
        else if (needle > straw) l = m + 1;
        else return kern_.i16(22 + std::size_t(m) * 6);
    }
    return 0;
}

// GPOS pair adjustment (lookup type 2), formats 1 (glyph pairs) and 2 (class pairs), with only
// the first glyph's x advance (value format 4).
int face::gpos_pair_advance(int left, int right) const {
    if (gpos_.empty() || gpos_.u16(0) != 1 || gpos_.u16(2) != 0) return 0;
    const byte_view lookup_list = gpos_.from(gpos_.u16(8));
    const unsigned lookup_count = lookup_list.u16(0);
    for (unsigned i = 0; i < lookup_count; ++i) {
        const byte_view lookup = lookup_list.from(lookup_list.u16(2 + 2 * std::size_t(i)));
        if (lookup.u16(0) != 2) continue;  // not a pair adjustment
        const unsigned sub_count = lookup.u16(4);
        for (unsigned s = 0; s < sub_count; ++s) {
            const byte_view table = lookup.from(lookup.u16(6 + 2 * std::size_t(s)));
            const unsigned format = table.u16(0);
            const int coverage_index = otl::coverage(table.from(table.u16(2))).index(std::uint32_t(left));
            if (coverage_index == -1) continue;
            const unsigned value_format1 = table.u16(4), value_format2 = table.u16(6);
            if (format == 1) {
                if (!(value_format1 == 4 && value_format2 == 0)) return 0;
                const unsigned pair_set_count = table.u16(8);
                const byte_view pair_set = table.from(table.u16(10 + 2 * std::size_t(coverage_index)));
                if (unsigned(coverage_index) >= pair_set_count) return 0;
                std::int64_t l = 0, r = std::int64_t(pair_set.u16(0)) - 1;
                while (l <= r) {
                    const std::int64_t m = (l + r) >> 1;
                    const std::size_t pair = 2 + 4 * std::size_t(m);  // second glyph, x advance
                    const int second = pair_set.u16(pair);
                    if (right < second) r = m - 1;
                    else if (right > second) l = m + 1;
                    else return pair_set.i16(pair + 2);
                }
            } else if (format == 2) {
                if (!(value_format1 == 4 && value_format2 == 0)) return 0;
                const unsigned left_class = otl::class_def(table.from(table.u16(8))).class_of(std::uint32_t(left));
                const unsigned right_class = otl::class_def(table.from(table.u16(10))).class_of(std::uint32_t(right));
                const unsigned class1_count = table.u16(12), class2_count = table.u16(14);
                if (left_class >= class1_count || right_class >= class2_count) return 0;  // malformed
                return table.i16(16 + 2 * (std::size_t(left_class) * class2_count + right_class));
            } else {
                return 0;  // an unknown position format
            }
        }
    }
    return 0;
}

int face::kern_advance(int left, int right) const {
    if (!gpos_.empty()) return gpos_pair_advance(left, right);
    if (!kern_.empty()) return kern_pair_advance(left, right);
    return 0;
}

// ---------------------------------------------------------------------------------- outlines

byte_view face::glyph_data(int glyph) const {
    if (glyph < 0 || glyph >= num_glyphs_ || index_to_loc_format_ >= 2) return {};
    std::uint32_t g1, g2;
    if (index_to_loc_format_ == 0) {
        g1 = loca_.u16(std::size_t(glyph) * 2) * 2u;
        g2 = loca_.u16(std::size_t(glyph) * 2 + 2) * 2u;
    } else {
        g1 = loca_.u32(std::size_t(glyph) * 4);
        g2 = loca_.u32(std::size_t(glyph) * 4 + 4);
    }
    return g1 == g2 ? byte_view() : glyf_.from(g1);  // no data: a glyph without an outline
}

// A simple glyph: contours of on- and off-curve points (quadratic splines whose on-curve points
// may be implied between two off-curve ones).
std::vector<vertex> face::simple_glyph(byte_view g, int contours) const {
    struct raw_point {
        std::uint8_t flags = 0;
        std::int16_t x = 0, y = 0;
    };
    const std::size_t end_points = 10;
    const unsigned instructions = g.u16(end_points + std::size_t(contours) * 2);
    std::size_t p = end_points + std::size_t(contours) * 2 + 2 + instructions;
    const int n = 1 + g.u16(end_points + std::size_t(contours) * 2 - 2);

    std::vector<raw_point> pts(std::size_t(n) + 1);  // one spare, read when a contour starts off-curve at its last point

    // flags (with repeats), then the x coordinates, then the y coordinates
    std::uint8_t flags = 0;
    int repeat = 0;
    for (int i = 0; i < n; ++i) {
        if (repeat == 0) {
            flags = g.u8(p++);
            if (flags & 8) repeat = g.u8(p++);
        } else {
            --repeat;
        }
        pts[i].flags = flags;
    }
    std::int32_t x = 0;
    for (int i = 0; i < n; ++i) {
        const std::uint8_t f = pts[i].flags;
        if (f & 2) {
            const std::int16_t dx = g.u8(p++);
            x += (f & 16) ? dx : -dx;
        } else if (!(f & 16)) {
            x += g.i16(p);
            p += 2;
        }
        pts[i].x = static_cast<std::int16_t>(x);
    }
    std::int32_t y = 0;
    for (int i = 0; i < n; ++i) {
        const std::uint8_t f = pts[i].flags;
        if (f & 4) {
            const std::int16_t dy = g.u8(p++);
            y += (f & 32) ? dy : -dy;
        } else if (!(f & 32)) {
            y += g.i16(p);
            p += 2;
        }
        pts[i].y = static_cast<std::int16_t>(y);
    }

    // convert to moves, lines and quadratic curves
    std::vector<vertex> out;
    out.reserve(std::size_t(n) + 2 * std::size_t(contours));
    int next_move = 0, contour = 0;
    bool was_off = false, start_off = false;
    std::int32_t cx = 0, cy = 0, sx = 0, sy = 0, scx = 0, scy = 0;
    for (int i = 0; i < n; ++i) {
        const std::uint8_t f = pts[i].flags;
        x = pts[i].x;
        y = pts[i].y;
        if (next_move == i) {
            if (i != 0) close_contour(out, was_off, start_off, sx, sy, scx, scy, cx, cy);
            start_off = !(f & 1);
            if (start_off) {
                // the contour starts off the curve: begin at the midpoint to the next point (or at
                // the next point if it is on the curve) and remember this one for wrapping around
                scx = x;
                scy = y;
                if (!(pts[i + 1].flags & 1)) {
                    sx = (x + std::int32_t(pts[i + 1].x)) >> 1;
                    sy = (y + std::int32_t(pts[i + 1].y)) >> 1;
                } else {
                    sx = pts[i + 1].x;
                    sy = pts[i + 1].y;
                    ++i;
                }
            } else {
                sx = x;
                sy = y;
            }
            out.push_back(make_vertex(vertex::move, sx, sy, 0, 0));
            was_off = false;
            next_move = 1 + g.u16(end_points + std::size_t(contour) * 2);
            ++contour;
        } else if (!(f & 1)) {  // an off-curve point
            if (was_off)  // two in a row: the on-curve point is implied halfway between them
                out.push_back(make_vertex(vertex::curve, (cx + x) >> 1, (cy + y) >> 1, cx, cy));
            cx = x;
            cy = y;
            was_off = true;
        } else {
            if (was_off) out.push_back(make_vertex(vertex::curve, x, y, cx, cy));
            else out.push_back(make_vertex(vertex::line, x, y, 0, 0));
            was_off = false;
        }
    }
    close_contour(out, was_off, start_off, sx, sy, scx, scy, cx, cy);
    return out;
}

// A composite glyph: other glyphs, each scaled / translated.
std::vector<vertex> face::composite_glyph(byte_view g, int depth) const {
    std::vector<vertex> out;
    if (depth >= max_composite_depth) return out;
    std::size_t p = 10;
    bool more = true;
    while (more) {
        const std::uint16_t flags = static_cast<std::uint16_t>(g.i16(p));
        const std::uint16_t component = static_cast<std::uint16_t>(g.i16(p + 2));
        p += 4;
        float mtx[6] = {1, 0, 0, 1, 0, 0};
        if (flags & 2) {  // x, y offsets (matching points are not supported)
            if (flags & 1) {
                mtx[4] = g.i16(p);
                mtx[5] = g.i16(p + 2);
                p += 4;
            } else {
                mtx[4] = static_cast<std::int8_t>(g.u8(p));
                mtx[5] = static_cast<std::int8_t>(g.u8(p + 1));
                p += 2;
            }
        }
        if (flags & (1 << 3)) {  // a uniform scale
            mtx[0] = mtx[3] = g.i16(p) / 16384.0f;
            p += 2;
            mtx[1] = mtx[2] = 0;
        } else if (flags & (1 << 6)) {  // separate x and y scales
            mtx[0] = g.i16(p) / 16384.0f;
            mtx[3] = g.i16(p + 2) / 16384.0f;
            p += 4;
            mtx[1] = mtx[2] = 0;
        } else if (flags & (1 << 7)) {  // a 2x2 matrix
            mtx[0] = g.i16(p) / 16384.0f;
            mtx[1] = g.i16(p + 2) / 16384.0f;
            mtx[2] = g.i16(p + 4) / 16384.0f;
            mtx[3] = g.i16(p + 6) / 16384.0f;
            p += 8;
        }
        const float m = static_cast<float>(std::sqrt(mtx[0] * mtx[0] + mtx[1] * mtx[1]));
        const float n = static_cast<float>(std::sqrt(mtx[2] * mtx[2] + mtx[3] * mtx[3]));

        std::vector<vertex> part = outline_truetype(component, depth + 1);
        for (vertex& v : part) {
            std::int16_t px = v.x, py = v.y;
            v.x = static_cast<std::int16_t>(m * (mtx[0] * px + mtx[2] * py + mtx[4]));
            v.y = static_cast<std::int16_t>(n * (mtx[1] * px + mtx[3] * py + mtx[5]));
            px = v.cx;
            py = v.cy;
            v.cx = static_cast<std::int16_t>(m * (mtx[0] * px + mtx[2] * py + mtx[4]));
            v.cy = static_cast<std::int16_t>(n * (mtx[1] * px + mtx[3] * py + mtx[5]));
        }
        out.insert(out.end(), part.begin(), part.end());
        more = (flags & (1 << 5)) != 0;
    }
    return out;
}

std::vector<vertex> face::outline_truetype(int glyph, int depth) const {
    const byte_view g = glyph_data(glyph);
    if (g.empty()) return {};
    const int contours = g.i16(0);
    if (contours > 0) return simple_glyph(g, contours);
    if (contours < 0) return composite_glyph(g, depth);
    return {};
}

std::vector<vertex> face::outline(int glyph) const {
    return is_cff_ ? outline_cff(glyph) : outline_truetype(glyph, 0);
}

bool face::glyph_box(int glyph, box& out) const {
    if (is_cff_) {
        out = box_cff(glyph);
        return true;  // a CFF glyph always has a (possibly empty) box
    }
    const byte_view g = glyph_data(glyph);
    if (g.empty()) return false;
    out = box{g.i16(2), g.i16(4), g.i16(6), g.i16(8)};
    return true;
}

}  // namespace stb::truetype
