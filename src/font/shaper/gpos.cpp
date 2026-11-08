#include "gpos.hpp"

#include <algorithm>

namespace stb::otl {

namespace {

using lookup_ref = positioner::lookup_ref;

constexpr std::size_t npos = static_cast<std::size_t>(-1);
// Bounds against a hostile font: lookups and subtables kept, nesting of lookups, glyphs in a rule, and
// the attempts (a subtable tried at a glyph) per run: so many per glyph, and never more than this.
constexpr std::size_t max_lookups = 1024;
constexpr std::size_t max_subtables = 16384;
constexpr int max_depth = 8;
constexpr unsigned max_rule_glyphs = 255;
constexpr std::size_t attempts_per_glyph = 4096;
constexpr std::size_t max_attempts = std::size_t(1) << 26;

// ---------------------------------------------------------------------------------------- values

struct value {
    int xp = 0, yp = 0, xa = 0;  // placement x and y, advance x (the vertical advance is not used)
};

// Bytes of a ValueRecord with these ValueFormat bits (device and variation offsets count, unread).
std::size_t value_size(unsigned format) {
    std::size_t n = 0;
    for (unsigned bit = 0x01; bit <= 0x80; bit <<= 1) n += (format & bit) ? 1 : 0;
    return 2 * n;
}

value read_value(byte_view t, std::size_t offset, unsigned format) {
    value v;
    if (format & 0x01) { v.xp = t.i16(offset); offset += 2; }
    if (format & 0x02) { v.yp = t.i16(offset); offset += 2; }
    if (format & 0x04) { v.xa = t.i16(offset); }
    return v;
}

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

// ------------------------------------------------------------------------------------- run state

struct adjustment {
    int xp = 0, yp = 0, xa = 0;
};

// Where one mark is attached so far: to glyph `parent`, at (x, y) from that glyph's origin.
struct attachment {
    std::size_t parent = npos;
    int x = 0, y = 0;
};

struct run {
    std::vector<glyph_record>& glyphs;
    const gdef* defs;
    const std::vector<lookup_ref>& lookups;
    std::vector<adjustment> adjust;
    std::vector<attachment> attached;
    std::size_t budget;
};

std::size_t next_unskipped(const run& r, const lookup_ref& lk, std::size_t i) {
    for (std::size_t j = i + 1; j < r.glyphs.size(); ++j)
        if (!lookup_skips(lk.flags, lk.mark_set, r.defs, r.glyphs[j])) return j;
    return npos;
}

std::size_t prev_unskipped(const run& r, const lookup_ref& lk, std::size_t i) {
    while (i-- > 0)
        if (!lookup_skips(lk.flags, lk.mark_set, r.defs, r.glyphs[i])) return i;
    return npos;
}

// The closest glyph before `i` that is not a mark; npos if there is none.
std::size_t previous_non_mark(const run& r, std::size_t i) {
    while (i-- > 0)
        if (r.glyphs[i].gclass != gc_mark) return i;
    return npos;
}

void add_value(run& r, std::size_t i, const value& v) {
    r.adjust[i].xp += v.xp;
    r.adjust[i].yp += v.yp;
    r.adjust[i].xa += v.xa;
}

bool apply_lookup(run& r, const lookup_ref& lk, std::size_t i, std::size_t& next, int depth);

// ------------------------------------------------------------------------ 1: single adjustment

bool single(run& r, byte_view t, std::size_t i) {
    const int index = coverage(t.from(t.u16(2))).index(r.glyphs[i].gid);
    if (index < 0) return false;
    const unsigned format = t.u16(0), vf = t.u16(4);
    if (format == 1) {
        add_value(r, i, read_value(t, 6, vf));
        return true;
    }
    if (format == 2 && unsigned(index) < t.u16(6)) {
        add_value(r, i, read_value(t, 8 + std::size_t(index) * value_size(vf), vf));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------- 2: pair adjustment

// The first glyph of the pair has the first value, the next one the lookup does not skip the second
// (and, if the lookup has a second value, the next pair starts after it).
bool pair(run& r, const lookup_ref& lk, byte_view t, std::size_t i, std::size_t& next) {
    const int index = coverage(t.from(t.u16(2))).index(r.glyphs[i].gid);
    if (index < 0) return false;
    const std::size_t j = next_unskipped(r, lk, i);
    if (j == npos) return false;
    const unsigned format = t.u16(0), vf1 = t.u16(4), vf2 = t.u16(6);
    const std::size_t size1 = value_size(vf1), size2 = value_size(vf2);
    value v1, v2;
    if (format == 1) {
        if (unsigned(index) >= t.u16(8)) return false;
        const byte_view set = t.from(t.u16(10 + 2 * std::size_t(index)));
        const std::size_t record = 2 + size1 + size2, count = set.u16(0);
        const std::uint32_t want = r.glyphs[j].gid;
        std::size_t lo = 0, hi = count;
        bool found = false;
        while (lo < hi) {  // the records are sorted by second glyph
            const std::size_t mid = lo + (hi - lo) / 2, at = 2 + mid * record;
            const std::uint32_t second = set.u16(at);
            if (second == want) {
                v1 = read_value(set, at + 2, vf1);
                v2 = read_value(set, at + 2 + size1, vf2);
                found = true;
                break;
            }
            if (second < want) lo = mid + 1; else hi = mid;
        }
        if (!found) return false;
    } else if (format == 2) {
        const unsigned c1 = class_def(t.from(t.u16(8))).class_of(r.glyphs[i].gid);
        const unsigned c2 = class_def(t.from(t.u16(10))).class_of(r.glyphs[j].gid);
        if (c1 >= t.u16(12) || c2 >= t.u16(14)) return false;
        const std::size_t at = 16 + (std::size_t(c1) * t.u16(14) + c2) * (size1 + size2);
        v1 = read_value(t, at, vf1);
        v2 = read_value(t, at + size1, vf2);
    } else {
        return false;
    }
    add_value(r, i, v1);
    add_value(r, j, v2);
    next = vf2 ? j + 1 : j;
    return true;
}

// --------------------------------------------------------- 4, 5, 6: mark to base, ligature, mark

void attach(run& r, std::size_t mark, std::size_t parent, const anchor& to, const anchor& from) {
    r.attached[mark] = {parent, to.x - from.x, to.y - from.y};
}

// The class and anchor of mark number `index` in a MarkArray, or false if it has none.
bool mark_record(byte_view mark_array, int index, unsigned class_count, unsigned& cls, anchor& out) {
    if (index < 0 || unsigned(index) >= mark_array.u16(0)) return false;
    const std::size_t rec = 2 + 4 * std::size_t(index);
    cls = mark_array.u16(rec);
    if (cls >= class_count) return false;
    out = read_anchor(mark_array, mark_array.u16(rec + 2));
    return out.ok;
}

// A subtable has the marks it handles and the bases they may sit on; each pair of a base and a
// mark class has an anchor.
bool mark_to_base(run& r, byte_view t, std::size_t i) {
    const unsigned class_count = t.u16(6);
    const int mark_index = coverage(t.from(t.u16(2))).index(r.glyphs[i].gid);
    if (mark_index < 0) return false;
    const std::size_t base = previous_non_mark(r, i);
    if (base == npos) return false;
    const int base_index = coverage(t.from(t.u16(4))).index(r.glyphs[base].gid);
    const byte_view base_array = t.from(t.u16(10));
    if (base_index < 0 || unsigned(base_index) >= base_array.u16(0)) return false;

    unsigned cls;
    anchor mark_anchor;
    if (!mark_record(t.from(t.u16(8)), mark_index, class_count, cls, mark_anchor)) return false;
    const std::size_t slot = std::size_t(base_index) * class_count + cls;
    const anchor base_anchor = read_anchor(base_array, base_array.u16(2 + 2 * slot));
    if (!base_anchor.ok) return false;
    attach(r, i, base, base_anchor, mark_anchor);
    return true;
}

// Like mark to base, with one set of anchors per component of the ligature. A mark that follows the
// ligature in the text hangs on its last component.
bool mark_to_ligature(run& r, byte_view t, std::size_t i) {
    const unsigned class_count = t.u16(6);
    const int mark_index = coverage(t.from(t.u16(2))).index(r.glyphs[i].gid);
    if (mark_index < 0) return false;
    const std::size_t base = previous_non_mark(r, i);
    if (base == npos) return false;
    const int lig_index = coverage(t.from(t.u16(4))).index(r.glyphs[base].gid);
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
    attach(r, i, base, lig_anchor, mark_anchor);
    return true;
}

// The mark hangs on the mark before it, which must itself be attached.
bool mark_to_mark(run& r, const lookup_ref& lk, byte_view t, std::size_t i) {
    const unsigned class_count = t.u16(6);
    const int mark_index = coverage(t.from(t.u16(2))).index(r.glyphs[i].gid);
    if (mark_index < 0) return false;
    const std::size_t before = prev_unskipped(r, lk, i);
    if (before == npos || r.glyphs[before].gclass != gc_mark || r.attached[before].parent == npos) return false;
    const int before_index = coverage(t.from(t.u16(4))).index(r.glyphs[before].gid);
    const byte_view before_array = t.from(t.u16(10));
    if (before_index < 0 || unsigned(before_index) >= before_array.u16(0)) return false;

    unsigned cls;
    anchor mark_anchor;
    if (!mark_record(t.from(t.u16(8)), mark_index, class_count, cls, mark_anchor)) return false;
    const std::size_t slot = std::size_t(before_index) * class_count + cls;
    const anchor before_anchor = read_anchor(before_array, before_array.u16(2 + 2 * slot));
    if (!before_anchor.ok) return false;
    attach(r, i, before, before_anchor, mark_anchor);
    return true;
}

// ------------------------------------------------------------ 7, 8: (chained) contextual positioning

// One run of elements of a rule (the backtrack, the input or the lookahead): what each is compared
// with: a glyph id, a class, or a coverage table (an offset from `base`).
struct sequence {
    enum kind { glyph_id, glyph_class, glyph_coverage } mode = glyph_id;
    byte_view array;           // the elements, u16 each, from `offset`
    std::size_t offset = 0;
    unsigned count = 0;
    class_def classes;         // glyph_class
    byte_view base;            // glyph_coverage
};

bool element_matches(const run& r, const sequence& s, unsigned k, std::size_t glyph) {
    const std::uint32_t gid = r.glyphs[glyph].gid;
    const unsigned v = s.array.u16(s.offset + 2 * std::size_t(k));
    switch (s.mode) {
        case sequence::glyph_id: return gid == v;
        case sequence::glyph_class: return s.classes.class_of(gid) == v;
        case sequence::glyph_coverage: return coverage(s.base.from(v)).index(gid) >= 0;
    }
    return false;
}

// A rule: the glyphs it needs around and from `i`, then the lookups to run on the input.
struct rule {
    sequence backtrack, input, lookahead;
    unsigned input_count = 0;     // glyphs of the input, the first included
    unsigned first_in_array = 0;  // 1 if the first input glyph is not in the array (it chose the rule)
    byte_view records;            // SequenceLookupRecords: {sequence index, lookup index} u16 each
    std::size_t records_offset = 0;
    unsigned record_count = 0;
};

// If the rule matches at `i`, runs its lookups and returns true; `next` is where scanning goes on.
bool apply_rule(run& r, const lookup_ref& lk, const rule& ru, std::size_t i, std::size_t& next, int depth) {
    if (ru.input_count == 0 || ru.input_count > max_rule_glyphs || ru.backtrack.count > max_rule_glyphs ||
        ru.lookahead.count > max_rule_glyphs)
        return false;
    std::vector<std::size_t> at(1, i);  // the glyphs of the input
    while (at.size() < ru.input_count) {
        const std::size_t j = next_unskipped(r, lk, at.back());
        if (j == npos) return false;
        at.push_back(j);
    }
    for (unsigned k = ru.first_in_array ? 1 : 0; k < ru.input_count; ++k)
        if (!element_matches(r, ru.input, k - ru.first_in_array, at[k])) return false;
    std::size_t j = i;
    for (unsigned k = 0; k < ru.backtrack.count; ++k) {  // the glyph just before the input comes first
        j = prev_unskipped(r, lk, j);
        if (j == npos || !element_matches(r, ru.backtrack, k, j)) return false;
    }
    j = at.back();
    for (unsigned k = 0; k < ru.lookahead.count; ++k) {
        j = next_unskipped(r, lk, j);
        if (j == npos || !element_matches(r, ru.lookahead, k, j)) return false;
    }
    for (unsigned k = 0; k < ru.record_count; ++k) {
        const std::size_t rec = ru.records_offset + 4 * std::size_t(k);
        const unsigned sequence_index = ru.records.u16(rec), lookup_index = ru.records.u16(rec + 2);
        if (sequence_index >= at.size() || lookup_index >= r.lookups.size()) continue;
        std::size_t ignored = 0;
        apply_lookup(r, r.lookups[lookup_index], at[sequence_index], ignored, depth + 1);
    }
    next = at.back() + 1;
    return true;
}

// Rule of a (non chained) context: glyphCount, seqLookupCount, input[glyphCount - 1], records.
rule read_context_rule(byte_view data, const sequence& proto) {
    rule ru;
    const unsigned glyphs = data.u16(0);
    ru.input_count = glyphs;
    ru.first_in_array = 1;
    ru.input = proto;
    ru.input.array = data;
    ru.input.offset = 4;
    ru.input.count = glyphs ? glyphs - 1 : 0;
    ru.record_count = data.u16(2);
    ru.records = data;
    ru.records_offset = 4 + 2 * std::size_t(ru.input.count);
    return ru;
}

// Rule of a chained context: backtrack, input (all but the first glyph), lookahead, records.
rule read_chain_rule(byte_view data, const sequence& back, const sequence& in, const sequence& ahead) {
    rule ru;
    ru.backtrack = back;
    ru.backtrack.array = data;
    ru.backtrack.offset = 2;
    ru.backtrack.count = data.u16(0);
    std::size_t at = 2 + 2 * std::size_t(ru.backtrack.count);
    ru.input = in;
    ru.input.array = data;
    ru.input.offset = at + 2;
    ru.input_count = data.u16(at);
    ru.first_in_array = 1;
    ru.input.count = ru.input_count ? ru.input_count - 1 : 0;
    at += 2 + 2 * std::size_t(ru.input.count);
    ru.lookahead = ahead;
    ru.lookahead.array = data;
    ru.lookahead.offset = at + 2;
    ru.lookahead.count = data.u16(at);
    at += 2 + 2 * std::size_t(ru.lookahead.count);
    ru.record_count = data.u16(at);
    ru.records = data;
    ru.records_offset = at + 2;
    return ru;
}

// The rules of a rule set (an offset table of rules) are tried in order; the first that matches wins.
template <class ReadRule>
bool apply_rule_set(run& r, const lookup_ref& lk, byte_view set, ReadRule read, std::size_t i, std::size_t& next,
                    int depth) {
    const unsigned count = set.u16(0);
    for (unsigned k = 0; k < count; ++k) {
        const std::size_t offset = set.u16(2 + 2 * std::size_t(k));
        if (offset == 0) continue;
        if (apply_rule(r, lk, read(set.from(offset)), i, next, depth)) return true;
    }
    return false;
}

sequence by_glyph() { return {}; }
sequence by_class(byte_view classes) {
    sequence s;
    s.mode = sequence::glyph_class;
    s.classes = class_def(classes);
    return s;
}
sequence by_coverage(byte_view base) {
    sequence s;
    s.mode = sequence::glyph_coverage;
    s.base = base;
    return s;
}

bool context(run& r, const lookup_ref& lk, byte_view t, std::size_t i, std::size_t& next, int depth) {
    const unsigned format = t.u16(0);
    const std::uint32_t gid = r.glyphs[i].gid;
    if (format == 1) {
        const int index = coverage(t.from(t.u16(2))).index(gid);
        if (index < 0 || unsigned(index) >= t.u16(4)) return false;
        const std::size_t set = t.u16(6 + 2 * std::size_t(index));
        if (set == 0) return false;
        const sequence proto = by_glyph();
        return apply_rule_set(r, lk, t.from(set), [&](byte_view d) { return read_context_rule(d, proto); }, i, next, depth);
    }
    if (format == 2) {
        if (coverage(t.from(t.u16(2))).index(gid) < 0) return false;
        const class_def classes(t.from(t.u16(4)));
        const unsigned cls = classes.class_of(gid);
        if (cls >= t.u16(6)) return false;
        const std::size_t set = t.u16(8 + 2 * std::size_t(cls));
        if (set == 0) return false;
        const sequence proto = by_class(t.from(t.u16(4)));
        return apply_rule_set(r, lk, t.from(set), [&](byte_view d) { return read_context_rule(d, proto); }, i, next, depth);
    }
    if (format == 3) {
        rule ru;
        ru.input_count = t.u16(2);
        ru.record_count = t.u16(4);
        ru.input = by_coverage(t);
        ru.input.array = t;
        ru.input.offset = 6;
        ru.input.count = ru.input_count;
        ru.records = t;
        ru.records_offset = 6 + 2 * std::size_t(ru.input_count);
        return apply_rule(r, lk, ru, i, next, depth);
    }
    return false;
}

bool chain_context(run& r, const lookup_ref& lk, byte_view t, std::size_t i, std::size_t& next, int depth) {
    const unsigned format = t.u16(0);
    const std::uint32_t gid = r.glyphs[i].gid;
    if (format == 1) {
        const int index = coverage(t.from(t.u16(2))).index(gid);
        if (index < 0 || unsigned(index) >= t.u16(4)) return false;
        const std::size_t set = t.u16(6 + 2 * std::size_t(index));
        if (set == 0) return false;
        const sequence none = by_glyph();
        return apply_rule_set(r, lk, t.from(set), [&](byte_view d) { return read_chain_rule(d, none, none, none); }, i,
                              next, depth);
    }
    if (format == 2) {
        if (coverage(t.from(t.u16(2))).index(gid) < 0) return false;
        const sequence back = by_class(t.from(t.u16(4))), in = by_class(t.from(t.u16(6))),
                       ahead = by_class(t.from(t.u16(8)));
        const unsigned cls = in.classes.class_of(gid);
        if (cls >= t.u16(10)) return false;
        const std::size_t set = t.u16(12 + 2 * std::size_t(cls));
        if (set == 0) return false;
        return apply_rule_set(r, lk, t.from(set), [&](byte_view d) { return read_chain_rule(d, back, in, ahead); }, i,
                              next, depth);
    }
    if (format == 3) {  // every element is a coverage table, the first input glyph included
        rule ru;
        ru.backtrack = by_coverage(t);
        ru.backtrack.array = t;
        ru.backtrack.offset = 4;
        ru.backtrack.count = t.u16(2);
        std::size_t at = 4 + 2 * std::size_t(ru.backtrack.count);
        ru.input = by_coverage(t);
        ru.input.array = t;
        ru.input.offset = at + 2;
        ru.input_count = ru.input.count = t.u16(at);
        at += 2 + 2 * std::size_t(ru.input.count);
        ru.lookahead = by_coverage(t);
        ru.lookahead.array = t;
        ru.lookahead.offset = at + 2;
        ru.lookahead.count = t.u16(at);
        at += 2 + 2 * std::size_t(ru.lookahead.count);
        ru.record_count = t.u16(at);
        ru.records = t;
        ru.records_offset = at + 2;
        return apply_rule(r, lk, ru, i, next, depth);
    }
    return false;
}

// ----------------------------------------------------------------------------------- one lookup

// Tries the subtables of a lookup at glyph `i` (the caller has checked that the lookup does not skip
// it); the first that applies wins. `next` is where scanning goes on if it is past i + 1.
bool apply_lookup(run& r, const lookup_ref& lk, std::size_t i, std::size_t& next, int depth) {
    if (depth > max_depth) return false;
    const bool is_mark = r.glyphs[i].gclass == gc_mark;
    for (const positioner::subtable_ref& st : lk.subtables) {
        if (r.budget == 0) return false;
        --r.budget;
        bool done = false;
        switch (st.type) {
            case 1: done = single(r, st.table, i); break;
            case 2: done = pair(r, lk, st.table, i, next); break;
            case 4: done = is_mark && mark_to_base(r, st.table, i); break;
            case 5: done = is_mark && mark_to_ligature(r, st.table, i); break;
            case 6: done = is_mark && mark_to_mark(r, lk, st.table, i); break;
            case 7: done = context(r, lk, st.table, i, next, depth); break;
            case 8: done = chain_context(r, lk, st.table, i, next, depth); break;
            default: break;
        }
        if (done) return true;
    }
    return false;
}

bool wanted_tag(std::uint32_t tag) {
    return tag == make_tag('k', 'e', 'r', 'n') || tag == make_tag('d', 'i', 's', 't') ||
           tag == make_tag('m', 'a', 'r', 'k') || tag == make_tag('m', 'k', 'm', 'k') ||
           tag == make_tag('a', 'b', 'v', 'm') || tag == make_tag('b', 'l', 'w', 'm');
}

}  // namespace

positioner::positioner(byte_view gpos, std::initializer_list<std::uint32_t> scripts) {
    if (gpos.empty() || gpos.u16(0) != 1) return;
    std::vector<std::uint16_t> adjusting;  // the lookups of kern and dist
    for (const feature_entry& f : script_features(gpos, scripts, 0)) {
        if (!wanted_tag(f.tag)) continue;
        top_.insert(top_.end(), f.lookups.begin(), f.lookups.end());
        if (f.tag == make_tag('k', 'e', 'r', 'n') || f.tag == make_tag('d', 'i', 's', 't'))
            adjusting.insert(adjusting.end(), f.lookups.begin(), f.lookups.end());
    }
    if (top_.empty()) return;
    std::sort(top_.begin(), top_.end());  // lookups run in the order of the lookup list
    top_.erase(std::unique(top_.begin(), top_.end()), top_.end());

    const byte_view list = gpos.from(gpos.u16(8));
    const std::size_t count = std::min<std::size_t>(list.u16(0), max_lookups);
    std::size_t subtables = 0;
    lookups_.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        const byte_view lk = list.from(list.u16(2 + 2 * index));
        const unsigned type = lk.u16(0), flags = lk.u16(2), n = lk.u16(4);
        lookup_ref& out = lookups_[index];
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
            if (real != 1 && real != 2 && (real < 4 || real > 8)) continue;  // not read (3 cursive, 9 nested)
            out.subtables.push_back({real, st});
            ++subtables;
        }
    }
    top_.erase(std::remove_if(top_.begin(), top_.end(), [&](std::uint16_t i) { return i >= lookups_.size() || lookups_[i].subtables.empty(); }),
               top_.end());
    for (std::uint16_t i : adjusting)
        if (i < lookups_.size() && !lookups_[i].subtables.empty()) kerns_ = true;
    if (top_.empty()) lookups_.clear();
}

void positioner::position(std::vector<glyph_record>& glyphs, const gdef* defs) const {
    if (top_.empty() || glyphs.empty()) return;
    run r{glyphs, defs, lookups_, std::vector<adjustment>(glyphs.size()), std::vector<attachment>(glyphs.size()),
          std::min(attempts_per_glyph * (glyphs.size() + 1), max_attempts)};
    for (std::uint16_t index : top_) {
        const lookup_ref& lk = lookups_[index];
        for (std::size_t i = 0; i < glyphs.size() && r.budget > 0;) {
            std::size_t next = i + 1;
            if (!lookup_skips(lk.flags, lk.mark_set, defs, glyphs[i]) && apply_lookup(r, lk, i, next, 0))
                i = std::max(next, i + 1);
            else
                ++i;
        }
    }

    // A mark that hangs on a mark is placed from the origin of the base under both: add up the
    // chain (a parent always comes before its child, so it is already complete).
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        glyphs[i].kern = r.adjust[i].xa;
        glyphs[i].shift_x = r.adjust[i].xp;
        glyphs[i].shift_y = r.adjust[i].yp;
        if (r.attached[i].parent == npos) continue;
        const glyph_record& parent = glyphs[r.attached[i].parent];
        glyphs[i].attached = true;
        glyphs[i].mark_x = r.attached[i].x + (parent.attached ? parent.mark_x : 0);
        glyphs[i].mark_y = r.attached[i].y + (parent.attached ? parent.mark_y : 0);
    }
}

}  // namespace stb::otl
