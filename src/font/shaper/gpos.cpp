#include "gpos.hpp"

#include <algorithm>

namespace stb::otl {

namespace {

constexpr std::size_t no_glyph = static_cast<std::size_t>(-1);
// Bounds against a hostile font: lookups kept, subtables kept, attempts per mark.
constexpr std::size_t max_lookups = 256;
constexpr std::size_t max_subtables = 4096;
constexpr std::size_t attempts_per_mark = 4096;

struct anchor {
    bool ok = false;
    int x = 0, y = 0;
};

// An anchor table (formats 1 to 3 start the same way) at `offset` from `base`; 0 is a null offset.
anchor read_anchor(byte_view base, std::size_t offset) {
    if (offset == 0) return {};
    const byte_view a = base.from(offset);
    const unsigned format = a.u16(0);
    if (format < 1 || format > 3 || !a.has(0, 6)) return {};
    return {true, a.i16(2), a.i16(4)};
}

// Where one mark is attached so far: to glyph `parent`, at (x, y) from that glyph's origin.
struct attachment {
    std::size_t parent = no_glyph;
    int x = 0, y = 0;
};

struct mark_context {
    const std::vector<glyph_record>& glyphs;
    std::vector<attachment>& attached;
    const gdef* defs;
    unsigned flags, mark_set;
};

// The class and anchor of mark number `index` in a MarkArray, or false if it has none.
bool mark_record(byte_view mark_array, int index, unsigned class_count, unsigned& cls, anchor& out) {
    if (index < 0 || unsigned(index) >= mark_array.u16(0)) return false;
    const std::size_t rec = 2 + 4 * std::size_t(index);
    cls = mark_array.u16(rec);
    if (cls >= class_count) return false;
    out = read_anchor(mark_array, mark_array.u16(rec + 2));
    return out.ok;
}

// The closest glyph before `i` that is not a mark; no_glyph if there is none.
std::size_t previous_non_mark(const std::vector<glyph_record>& g, std::size_t i) {
    while (i-- > 0)
        if (g[i].gclass != gc_mark) return i;
    return no_glyph;
}

void attach(mark_context& c, std::size_t mark, std::size_t parent, const anchor& to, const anchor& from) {
    c.attached[mark] = {parent, to.x - from.x, to.y - from.y};
}

// 4: mark to base. A subtable has the marks it handles and the bases they may sit on; each pair of
// a base and a mark class has an anchor.
bool mark_to_base(mark_context& c, byte_view t, std::size_t i) {
    const unsigned class_count = t.u16(6);
    const int mark_index = coverage(t.from(t.u16(2))).index(c.glyphs[i].gid);
    if (mark_index < 0) return false;
    const std::size_t base = previous_non_mark(c.glyphs, i);
    if (base == no_glyph) return false;
    const int base_index = coverage(t.from(t.u16(4))).index(c.glyphs[base].gid);
    const byte_view base_array = t.from(t.u16(10));
    if (base_index < 0 || unsigned(base_index) >= base_array.u16(0)) return false;

    unsigned cls;
    anchor mark_anchor;
    if (!mark_record(t.from(t.u16(8)), mark_index, class_count, cls, mark_anchor)) return false;
    const std::size_t slot = std::size_t(base_index) * class_count + cls;
    const anchor base_anchor = read_anchor(base_array, base_array.u16(2 + 2 * slot));
    if (!base_anchor.ok) return false;
    attach(c, i, base, base_anchor, mark_anchor);
    return true;
}

// 5: mark to ligature. Like mark to base, with one set of anchors per component of the ligature. A
// mark that follows the ligature in the text hangs on its last component.
bool mark_to_ligature(mark_context& c, byte_view t, std::size_t i) {
    const unsigned class_count = t.u16(6);
    const int mark_index = coverage(t.from(t.u16(2))).index(c.glyphs[i].gid);
    if (mark_index < 0) return false;
    const std::size_t base = previous_non_mark(c.glyphs, i);
    if (base == no_glyph) return false;
    const int lig_index = coverage(t.from(t.u16(4))).index(c.glyphs[base].gid);
    const byte_view lig_array = t.from(t.u16(10));
    if (lig_index < 0 || unsigned(lig_index) >= lig_array.u16(0)) return false;
    const byte_view lig = lig_array.from(lig_array.u16(2 + 2 * std::size_t(lig_index)));
    const unsigned components = lig.u16(0);
    if (components == 0) return false;

    unsigned cls;
    anchor mark_anchor;
    if (!mark_record(t.from(t.u16(8)), mark_index, class_count, cls, mark_anchor)) return false;
    const std::size_t slot = std::size_t(components - 1) * class_count + cls;
    const anchor lig_anchor = read_anchor(lig, lig.u16(2 + 2 * slot));
    if (!lig_anchor.ok) return false;
    attach(c, i, base, lig_anchor, mark_anchor);
    return true;
}

// 6: mark to mark. The mark hangs on the mark before it, which must itself be attached.
bool mark_to_mark(mark_context& c, byte_view t, std::size_t i) {
    const unsigned class_count = t.u16(6);
    const int mark_index = coverage(t.from(t.u16(2))).index(c.glyphs[i].gid);
    if (mark_index < 0) return false;
    std::size_t before = i;
    do {
        if (before == 0) return false;
        --before;
    } while (lookup_skips(c.flags, c.mark_set, c.defs, c.glyphs[before]));
    if (c.glyphs[before].gclass != gc_mark || c.attached[before].parent == no_glyph) return false;
    const int before_index = coverage(t.from(t.u16(4))).index(c.glyphs[before].gid);
    const byte_view before_array = t.from(t.u16(10));
    if (before_index < 0 || unsigned(before_index) >= before_array.u16(0)) return false;

    unsigned cls;
    anchor mark_anchor;
    if (!mark_record(t.from(t.u16(8)), mark_index, class_count, cls, mark_anchor)) return false;
    const std::size_t slot = std::size_t(before_index) * class_count + cls;
    const anchor before_anchor = read_anchor(before_array, before_array.u16(2 + 2 * slot));
    if (!before_anchor.ok) return false;
    attach(c, i, before, before_anchor, mark_anchor);
    return true;
}

}  // namespace

mark_positioner::mark_positioner(byte_view gpos, std::initializer_list<std::uint32_t> scripts) {
    if (gpos.empty() || gpos.u16(0) != 1) return;
    const std::uint32_t mark = make_tag('m', 'a', 'r', 'k'), mkmk = make_tag('m', 'k', 'm', 'k');
    std::vector<std::uint16_t> wanted;
    for (const feature_entry& f : script_features(gpos, scripts, 0))
        if (f.tag == mark || f.tag == mkmk) wanted.insert(wanted.end(), f.lookups.begin(), f.lookups.end());
    std::sort(wanted.begin(), wanted.end());  // lookups run in the order of the lookup list
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

    const byte_view list = gpos.from(gpos.u16(8));
    std::size_t subtables = 0;
    for (std::uint16_t index : wanted) {
        if (index >= list.u16(0) || lookups_.size() >= max_lookups || subtables >= max_subtables) break;
        const byte_view lk = list.from(list.u16(2 + 2 * std::size_t(index)));
        const unsigned type = lk.u16(0), flags = lk.u16(2), n = lk.u16(4);
        lookup_ref out;
        out.flags = flags;
        out.mark_set = (flags & 0x10) ? lk.u16(6 + 2 * std::size_t(n)) : 0;
        for (unsigned k = 0; k < n && subtables < max_subtables; ++k) {
            byte_view st = lk.from(lk.u16(6 + 2 * std::size_t(k)));
            unsigned real = type;
            if (type == 9) {  // extension: the real subtable is a 32-bit offset away
                if (st.u16(0) != 1) continue;
                real = st.u16(2);
                st = st.from(st.u32(4));
            }
            if ((real < 4 || real > 6) || st.u16(0) != 1) continue;  // not an attachment, or an unknown format
            out.subtables.push_back({real, st});
            ++subtables;
        }
        if (!out.subtables.empty()) lookups_.push_back(std::move(out));
    }
}

void mark_positioner::position(std::vector<glyph_record>& glyphs, const gdef* defs) const {
    if (lookups_.empty()) return;
    std::size_t marks = 0;
    for (const glyph_record& g : glyphs) marks += g.gclass == gc_mark ? 1 : 0;
    if (marks == 0) return;

    std::vector<attachment> attached(glyphs.size());
    std::size_t budget = attempts_per_mark * (marks + 1);
    for (const lookup_ref& lk : lookups_) {
        mark_context c{glyphs, attached, defs, lk.flags, lk.mark_set};
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            if (glyphs[i].gclass != gc_mark || lookup_skips(lk.flags, lk.mark_set, defs, glyphs[i])) continue;
            for (const subtable_ref& st : lk.subtables) {
                if (budget == 0) break;
                --budget;
                const bool done = st.type == 4   ? mark_to_base(c, st.table, i)
                                  : st.type == 5 ? mark_to_ligature(c, st.table, i)
                                                 : mark_to_mark(c, st.table, i);
                if (done) break;
            }
        }
    }

    // A mark that hangs on a mark is placed from the origin of the base under both: add up the
    // chain (a parent always comes before its child, so it is already complete).
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        if (attached[i].parent == no_glyph) continue;
        const glyph_record& parent = glyphs[attached[i].parent];
        glyphs[i].attached = true;
        glyphs[i].mark_x = attached[i].x + (parent.attached ? parent.mark_x : 0);
        glyphs[i].mark_y = attached[i].y + (parent.attached ? parent.mark_y : 0);
    }
}

}  // namespace stb::otl
