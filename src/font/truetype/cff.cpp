// OpenType fonts with PostScript outlines: the CFF table and its Type 2 charstrings.
// Written from the algorithms of stb_truetype.h v1.26 (public domain / MIT, Sean Barrett and
// contributors) and giving the same outlines. Hinting is ignored, and flex operators always draw
// plain beziers.

#include <cmath>

#include "font/truetype/truetype.hpp"

namespace stb::truetype {

using otl::byte_view;
using otl::make_tag;

namespace {

constexpr int max_subroutine_depth = 10;
constexpr int stack_size = 48;

}  // namespace

// ---------------------------------------------------------------------------- CFF structures

face::cff_buffer face::cff_buffer::range(int o, int s) const {
    cff_buffer r;
    if (o < 0 || s < 0 || o > size || s > size - o) return r;
    r.data = data + o;
    r.size = s;
    return r;
}

// The INDEX that starts at the cursor: the cursor ends up after it.
face::cff_buffer face::cff_index(cff_buffer& b) {
    const int start = b.cursor;
    const int count = int(b.get16());
    if (count) {
        const int offset_size = b.get8();
        b.skip(offset_size * count);
        b.skip(int(b.get(offset_size)) - 1);
    }
    return b.range(start, b.cursor - start);
}

int face::cff_index_count(cff_buffer& b) {
    b.seek(0);
    return int(b.get16());
}

face::cff_buffer face::cff_index_get(cff_buffer b, int i) {
    b.seek(0);
    const int count = int(b.get16());
    const int offset_size = b.get8();
    b.skip(i * offset_size);
    const int start = int(b.get(offset_size));
    const int end = int(b.get(offset_size));
    (void)count;
    return b.range(2 + (count + 1) * offset_size + start, end - start);
}

std::uint32_t face::cff_int(cff_buffer& b) {
    const int b0 = b.get8();
    if (b0 >= 32 && b0 <= 246) return std::uint32_t(b0 - 139);
    if (b0 >= 247 && b0 <= 250) return std::uint32_t((b0 - 247) * 256 + b.get8() + 108);
    if (b0 >= 251 && b0 <= 254) return std::uint32_t(-(b0 - 251) * 256 - int(b.get8()) - 108);
    if (b0 == 28) return b.get16();
    if (b0 == 29) return b.get32();
    return 0;
}

// A DICT is a list of "operands, operator": the operand bytes of the entry for `key`, or an
// empty range.
face::cff_buffer face::dict_get(cff_buffer& b, int key) {
    b.seek(0);
    while (b.cursor < b.size) {
        const int start = b.cursor;
        while (b.peek8() >= 28) {
            if (b.peek8() == 30) {  // a real number: nibbles up to an 0xF
                b.skip(1);
                while (b.cursor < b.size) {
                    const int v = b.get8();
                    if ((v & 0xF) == 0xF || (v >> 4) == 0xF) break;
                }
            } else {
                cff_int(b);
            }
        }
        const int end = b.cursor;
        int op = b.get8();
        if (op == 12) op = b.get8() | 0x100;
        if (op == key) return b.range(start, end - start);
    }
    return b.range(0, 0);
}

void face::dict_get_ints(cff_buffer& b, int key, int count, std::uint32_t* out) {
    cff_buffer operands = dict_get(b, key);
    for (int i = 0; i < count && operands.cursor < operands.size; ++i) out[i] = cff_int(operands);
}

// The local subroutines of a font DICT's private DICT.
face::cff_buffer face::private_subrs(cff_buffer cff, cff_buffer font_dict) {
    std::uint32_t subrs_offset = 0, private_loc[2] = {0, 0};
    dict_get_ints(font_dict, 18, 2, private_loc);
    if (!private_loc[1] || !private_loc[0]) return {};
    cff_buffer private_dict = cff.range(int(private_loc[1]), int(private_loc[0]));
    dict_get_ints(private_dict, 19, 1, &subrs_offset);
    if (!subrs_offset) return {};
    cff.seek(int(private_loc[1] + subrs_offset));
    return cff_index(cff);
}

bool face::init_cff(int font_start) {
    const byte_view table = otl::find_table(file_, std::uint32_t(font_start), make_tag('C', 'F', 'F', ' '));
    if (table.empty()) return false;
    is_cff_ = true;

    cff_.data = table.data();
    cff_.size = int(std::min<std::size_t>(table.size(), 0x3fffffff));
    cff_buffer b = cff_;

    b.skip(2);
    b.seek(b.get8());  // the header size
    cff_index(b);      // names: only the first font is used
    cff_buffer top_dict_index = cff_index(b);
    cff_buffer top_dict = cff_index_get(top_dict_index, 0);
    cff_index(b);  // strings
    global_subrs_ = cff_index(b);

    std::uint32_t char_strings = 0, charstring_type = 2, fd_array = 0, fd_select = 0;
    dict_get_ints(top_dict, 17, 1, &char_strings);
    dict_get_ints(top_dict, 0x100 | 6, 1, &charstring_type);
    dict_get_ints(top_dict, 0x100 | 36, 1, &fd_array);
    dict_get_ints(top_dict, 0x100 | 37, 1, &fd_select);
    subrs_ = private_subrs(b, top_dict);

    if (charstring_type != 2 || char_strings == 0) return false;  // only Type 2 charstrings
    if (fd_array) {                                               // a CID-keyed font
        if (!fd_select) return false;
        b.seek(int(fd_array));
        font_dicts_ = cff_index(b);
        fd_select_ = b.range(int(fd_select), b.size - int(fd_select));
    }
    b.seek(int(char_strings));
    char_strings_ = cff_index(b);
    return true;
}

// The local subroutines for a glyph of a CID-keyed font: the font DICT is chosen per glyph.
face::cff_buffer face::glyph_subrs(int glyph) const {
    cff_buffer select = fd_select_;
    int selector = -1;
    select.seek(0);
    const int format = select.get8();
    if (format == 0) {
        select.skip(glyph);
        selector = select.get8();
    } else if (format == 3) {
        const int ranges = int(select.get16());
        int start = int(select.get16());
        for (int i = 0; i < ranges; ++i) {
            const int fd = select.get8();
            const int end = int(select.get16());
            if (glyph >= start && glyph < end) {
                selector = fd;
                break;
            }
            start = end;
        }
    }
    if (selector == -1) return {};
    return private_subrs(cff_, cff_index_get(font_dicts_, selector));
}

// ---------------------------------------------------------------------------- charstrings

// The outline being built (and its bounds) while a charstring runs.
struct face::charstring_context {
    bool started = false;
    float first_x = 0, first_y = 0;
    float x = 0, y = 0;
    std::int32_t min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    std::vector<vertex>* out = nullptr;  // null: only the bounds are wanted
    int num_vertices = 0;

    void track(std::int32_t px, std::int32_t py) {
        if (px > max_x || !started) max_x = px;
        if (py > max_y || !started) max_y = py;
        if (px < min_x || !started) min_x = px;
        if (py < min_y || !started) min_y = py;
        started = true;
    }

    void add(std::uint8_t type, std::int32_t px, std::int32_t py, std::int32_t cx, std::int32_t cy, std::int32_t cx1,
             std::int32_t cy1) {
        track(px, py);
        if (type == vertex::cubic) {
            track(cx, cy);
            track(cx1, cy1);
        }
        if (out) {
            vertex v;
            v.type = type;
            v.x = static_cast<std::int16_t>(px);
            v.y = static_cast<std::int16_t>(py);
            v.cx = static_cast<std::int16_t>(cx);
            v.cy = static_cast<std::int16_t>(cy);
            v.cx1 = static_cast<std::int16_t>(cx1);
            v.cy1 = static_cast<std::int16_t>(cy1);
            out->push_back(v);
        }
        ++num_vertices;
    }

    void close_shape() {
        if (first_x != x || first_y != y) add(vertex::line, int(first_x), int(first_y), 0, 0, 0, 0);
    }

    void move_to(float dx, float dy) {
        close_shape();
        first_x = x = x + dx;
        first_y = y = y + dy;
        add(vertex::move, int(x), int(y), 0, 0, 0, 0);
    }

    void line_to(float dx, float dy) {
        x += dx;
        y += dy;
        add(vertex::line, int(x), int(y), 0, 0, 0, 0);
    }

    void curve_to(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        const float cx1 = x + dx1, cy1 = y + dy1;
        const float cx2 = cx1 + dx2, cy2 = cy1 + dy2;
        x = cx2 + dx3;
        y = cy2 + dy3;
        add(vertex::cubic, int(x), int(y), int(cx1), int(cy1), int(cx2), int(cy2));
    }
};

// Subroutine n of an INDEX (numbers are biased by the INDEX size).
face::cff_buffer face::subroutine(cff_buffer index, int n) {
    const int count = cff_index_count(index);
    int bias = 107;
    if (count >= 33900) bias = 32768;
    else if (count >= 1240) bias = 1131;
    n += bias;
    if (n < 0 || n >= count) return {};
    return cff_index_get(index, n);
}

// Runs one glyph's charstring. False if it is malformed (stack errors, unknown operators, ...).
bool face::run_charstring(int glyph, charstring_context& c) const {
    cff_buffer glyph_index = char_strings_;
    if (glyph < 0 || glyph >= cff_index_count(glyph_index)) return false;
    bool in_header = true, has_subrs = false;
    int mask_bits = 0, subr_stack_height = 0, sp = 0;
    float s[stack_size];
    cff_buffer subr_stack[max_subroutine_depth];
    cff_buffer subrs = subrs_;

    // the initial width is ignored: hmtx has it
    cff_buffer b = cff_index_get(char_strings_, glyph);
    while (b.cursor < b.size) {
        int i = 0;
        bool clear_stack = true;
        const int b0 = b.get8();
        switch (b0) {
            case 0x13:  // hintmask
            case 0x14:  // cntrmask
                if (in_header) mask_bits += sp / 2;  // an implicit vstem
                in_header = false;
                b.skip((mask_bits + 7) / 8);
                break;

            case 0x01:  // hstem
            case 0x03:  // vstem
            case 0x12:  // hstemhm
            case 0x17:  // vstemhm
                mask_bits += sp / 2;
                break;

            case 0x15:  // rmoveto
                in_header = false;
                if (sp < 2) return false;
                c.move_to(s[sp - 2], s[sp - 1]);
                break;
            case 0x04:  // vmoveto
                in_header = false;
                if (sp < 1) return false;
                c.move_to(0, s[sp - 1]);
                break;
            case 0x16:  // hmoveto
                in_header = false;
                if (sp < 1) return false;
                c.move_to(s[sp - 1], 0);
                break;

            case 0x05:  // rlineto
                if (sp < 2) return false;
                for (; i + 1 < sp; i += 2) c.line_to(s[i], s[i + 1]);
                break;

            case 0x07:  // vlineto
            case 0x06: {  // hlineto: the two alternate horizontal and vertical, from a different start
                if (sp < 1) return false;
                bool horizontal = b0 == 0x06;
                for (; i < sp; ++i, horizontal = !horizontal) {
                    if (horizontal) c.line_to(s[i], 0);
                    else c.line_to(0, s[i]);
                }
                break;
            }

            case 0x1F:  // hvcurveto
            case 0x1E: {  // vhcurveto
                if (sp < 4) return false;
                bool vertical_first = b0 == 0x1E;
                for (; i + 3 < sp; i += 4, vertical_first = !vertical_first) {
                    const float last = (sp - i == 5) ? s[i + 4] : 0.0f;  // the final curve may carry one extra delta
                    if (vertical_first) c.curve_to(0, s[i], s[i + 1], s[i + 2], s[i + 3], last);
                    else c.curve_to(s[i], 0, s[i + 1], s[i + 2], last, s[i + 3]);
                }
                break;
            }

            case 0x08:  // rrcurveto
                if (sp < 6) return false;
                for (; i + 5 < sp; i += 6) c.curve_to(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
                break;

            case 0x18:  // rcurveline
                if (sp < 8) return false;
                for (; i + 5 < sp - 2; i += 6) c.curve_to(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
                if (i + 1 >= sp) return false;
                c.line_to(s[i], s[i + 1]);
                break;

            case 0x19:  // rlinecurve
                if (sp < 8) return false;
                for (; i + 1 < sp - 6; i += 2) c.line_to(s[i], s[i + 1]);
                if (i + 5 >= sp) return false;
                c.curve_to(s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
                break;

            case 0x1A:  // vvcurveto
            case 0x1B: {  // hhcurveto
                if (sp < 4) return false;
                float f = 0.0f;
                if (sp & 1) f = s[i++];
                for (; i + 3 < sp; i += 4) {
                    if (b0 == 0x1B) c.curve_to(s[i], f, s[i + 1], s[i + 2], s[i + 3], 0.0f);
                    else c.curve_to(f, s[i], s[i + 1], s[i + 2], 0.0f, s[i + 3]);
                    f = 0.0f;
                }
                break;
            }

            case 0x0A:  // callsubr
            case 0x1D: {  // callgsubr
                if (b0 == 0x0A && !has_subrs) {
                    if (fd_select_.size) subrs = glyph_subrs(glyph);
                    has_subrs = true;
                }
                if (sp < 1) return false;
                const int number = int(s[--sp]);
                if (subr_stack_height >= max_subroutine_depth) return false;
                subr_stack[subr_stack_height++] = b;
                b = subroutine(b0 == 0x0A ? subrs : global_subrs_, number);
                if (b.size == 0) return false;
                b.cursor = 0;
                clear_stack = false;
                break;
            }

            case 0x0B:  // return
                if (subr_stack_height <= 0) return false;
                b = subr_stack[--subr_stack_height];
                clear_stack = false;
                break;

            case 0x0E:  // endchar
                c.close_shape();
                return true;

            case 0x0C: {  // a two-byte operator
                float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
                switch (b.get8()) {
                    case 0x22:  // hflex
                        if (sp < 7) return false;
                        dx1 = s[0], dx2 = s[1], dy2 = s[2], dx3 = s[3], dx4 = s[4], dx5 = s[5], dx6 = s[6];
                        c.curve_to(dx1, 0, dx2, dy2, dx3, 0);
                        c.curve_to(dx4, 0, dx5, -dy2, dx6, 0);
                        break;
                    case 0x23:  // flex
                        if (sp < 13) return false;
                        dx1 = s[0], dy1 = s[1], dx2 = s[2], dy2 = s[3], dx3 = s[4], dy3 = s[5];
                        dx4 = s[6], dy4 = s[7], dx5 = s[8], dy5 = s[9], dx6 = s[10], dy6 = s[11];
                        c.curve_to(dx1, dy1, dx2, dy2, dx3, dy3);
                        c.curve_to(dx4, dy4, dx5, dy5, dx6, dy6);
                        break;
                    case 0x24:  // hflex1
                        if (sp < 9) return false;
                        dx1 = s[0], dy1 = s[1], dx2 = s[2], dy2 = s[3], dx3 = s[4], dx4 = s[5], dx5 = s[6];
                        dy5 = s[7], dx6 = s[8];
                        c.curve_to(dx1, dy1, dx2, dy2, dx3, 0);
                        c.curve_to(dx4, 0, dx5, dy5, dx6, -(dy1 + dy2 + dy5));
                        break;
                    case 0x25: {  // flex1
                        if (sp < 11) return false;
                        dx1 = s[0], dy1 = s[1], dx2 = s[2], dy2 = s[3], dx3 = s[4], dy3 = s[5];
                        dx4 = s[6], dy4 = s[7], dx5 = s[8], dy5 = s[9];
                        dx6 = dy6 = s[10];
                        const float dx = dx1 + dx2 + dx3 + dx4 + dx5, dy = dy1 + dy2 + dy3 + dy4 + dy5;
                        if (std::fabs(dx) > std::fabs(dy)) dy6 = -dy;
                        else dx6 = -dx;
                        c.curve_to(dx1, dy1, dx2, dy2, dx3, dy3);
                        c.curve_to(dx4, dy4, dx5, dy5, dx6, dy6);
                        break;
                    }
                    default: return false;
                }
                break;
            }

            default: {
                if (b0 != 255 && b0 != 28 && b0 < 32) return false;  // a reserved operator
                float f;
                if (b0 == 255) {
                    f = static_cast<float>(static_cast<std::int32_t>(b.get32())) / 0x10000;  // 16.16 fixed point
                } else {
                    b.skip(-1);
                    f = static_cast<float>(static_cast<std::int16_t>(cff_int(b)));
                }
                if (sp >= stack_size) return false;
                s[sp++] = f;
                clear_stack = false;
                break;
            }
        }
        if (clear_stack) sp = 0;
    }
    return false;  // no endchar
}

std::vector<vertex> face::outline_cff(int glyph) const {
    std::vector<vertex> out;
    charstring_context ctx;
    ctx.out = &out;
    if (!run_charstring(glyph, ctx)) out.clear();
    return out;
}

// The bounds of the charstring's points (all zero for a malformed one).
box face::box_cff(int glyph) const {
    charstring_context ctx;
    if (!run_charstring(glyph, ctx)) return {};
    return box{ctx.min_x, ctx.min_y, ctx.max_x, ctx.max_y};
}

}  // namespace stb::truetype
