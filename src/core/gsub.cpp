#include "gsub.hpp"

#include <algorithm>
#include <utility>

namespace stb::otl {

// ------------------------------------------------------------ apply_context

void apply_context::set_glyph(std::size_t i, std::uint32_t gid, bool ligature) {
    glyph_record& g = glyphs[i];
    g.gid = gid;
    if (defs && defs->has_glyph_classes()) {
        g.gclass = defs->glyph_class(gid);  // the font decides, also for glyphs it does not list
        return;
    }
    if (ligature) g.gclass = gc_ligature;
}

// ------------------------------------------------------------------- lookup

bool lookup::skips(const apply_context& c, const glyph_record& g) const noexcept {
    if (flags_ == 0) return false;
    if ((flags_ & 0x02) && g.gclass == gc_base) return true;
    if ((flags_ & 0x04) && g.gclass == gc_ligature) return true;
    if (g.gclass == gc_mark) {
        if (flags_ & 0x08) return true;
        if (flags_ & 0x10) return !(c.defs && c.defs->in_mark_set(static_cast<std::uint16_t>(mark_set_), g.gid));
        if (flags_ & 0xFF00) return !(c.defs && c.defs->mark_attach_class(g.gid) == (flags_ >> 8));
    }
    return false;
}

std::size_t lookup::next_unskipped(const apply_context& c, std::size_t i) const noexcept {
    for (std::size_t j = i + 1; j < c.glyphs.size(); ++j)
        if (!skips(c, c.glyphs[j])) return j;
    return npos;
}

std::size_t lookup::prev_unskipped(const apply_context& c, std::size_t i) const noexcept {
    for (std::size_t j = i; j-- > 0;)
        if (!skips(c, c.glyphs[j])) return j;
    return npos;
}

bool lookup::apply_at(apply_context& c, std::size_t pos, std::size_t& next) const {
    if (c.ops_left == 0) return false;
    --c.ops_left;
    for (const auto& st : subtables_)
        if (st->apply(c, *this, pos, next)) return true;
    return false;
}

void lookup::build_filter() {
    glyph_set set;
    for (const auto& st : subtables_)
        if (!st->add_first_glyphs(set)) return;
    first_ = std::move(set);
    filtered_ = true;
}

void lookup::run(apply_context& c) const {
    for (std::size_t i = 0; i < c.glyphs.size() && c.ops_left;) {
        const glyph_record& g = c.glyphs[i];
        if ((filtered_ && !first_.has(g.gid)) || !(g.mask & c.mask) || skips(c, g)) {
            ++i;
            continue;
        }
        std::size_t next = i + 1;
        i = apply_at(c, i, next) ? next : i + 1;
    }
}

namespace {

// ------------------------------------------------------------ 1: single

class single_subst final : public subtable {
public:
    explicit single_subst(byte_view v) : v_(v), cov_(v.from(v.u16(2))), format_(v.u16(0)) {}

    bool apply(apply_context& c, const lookup&, std::size_t pos, std::size_t& next) const override {
        const int idx = cov_.index(c.glyphs[pos].gid);
        if (idx < 0) return false;
        std::uint32_t out;
        if (format_ == 1) {
            out = (c.glyphs[pos].gid + std::uint32_t(std::int32_t(v_.i16(4)))) & 0xFFFF;
        } else if (format_ == 2 && unsigned(idx) < v_.u16(4)) {
            out = v_.u16(6 + 2 * std::size_t(idx));
        } else {
            return false;
        }
        c.set_glyph(pos, out);
        next = pos + 1;
        return true;
    }
    bool add_first_glyphs(glyph_set& out) const override { return cov_.add_to(out); }

private:
    byte_view v_;
    coverage cov_;
    unsigned format_;
};

// -------------------------------------------- 2: multiple, 3: alternate

// Both are "coverage -> offset to a list of glyphs" tables.
class glyph_list_subst final : public subtable {
public:
    glyph_list_subst(byte_view v, bool alternate) : v_(v), cov_(v.from(v.u16(2))), alternate_(alternate) {}

    bool apply(apply_context& c, const lookup&, std::size_t pos, std::size_t& next) const override {
        if (v_.u16(0) != 1) return false;
        const int idx = cov_.index(c.glyphs[pos].gid);
        if (idx < 0 || unsigned(idx) >= v_.u16(4)) return false;
        const byte_view list = v_.from(v_.u16(6 + 2 * std::size_t(idx)));
        const unsigned n = list.u16(0);
        if (alternate_) {  // the first alternate: what a feature turned on with value 1 selects
            if (n == 0) return false;
            c.set_glyph(pos, list.u16(2));
            next = pos + 1;
            return true;
        }
        if (n == 0) {  // an empty sequence deletes the glyph
            c.glyphs.erase(c.glyphs.begin() + std::ptrdiff_t(pos));
            next = pos;
            return true;
        }
        if (c.glyphs.size() + n - 1 > c.max_glyphs) return false;  // would grow past the limit
        c.set_glyph(pos, list.u16(2));
        if (n > 1) {
            const glyph_record base = c.glyphs[pos];
            c.glyphs.insert(c.glyphs.begin() + std::ptrdiff_t(pos) + 1, n - 1, base);
            for (unsigned k = 1; k < n; ++k) c.set_glyph(pos + k, list.u16(2 + 2 * std::size_t(k)));
        }
        next = pos + n;
        return true;
    }
    bool add_first_glyphs(glyph_set& out) const override { return cov_.add_to(out); }

private:
    byte_view v_;
    coverage cov_;
    bool alternate_;
};

// ------------------------------------------------------------ 4: ligature

class ligature_subst final : public subtable {
public:
    explicit ligature_subst(byte_view v) : v_(v), cov_(v.from(v.u16(2))) {}

    bool apply(apply_context& c, const lookup& owner, std::size_t pos, std::size_t& next) const override {
        if (v_.u16(0) != 1) return false;
        const int idx = cov_.index(c.glyphs[pos].gid);
        if (idx < 0 || unsigned(idx) >= v_.u16(4)) return false;
        const byte_view set = v_.from(v_.u16(6 + 2 * std::size_t(idx)));
        const unsigned count = set.u16(0);
        for (unsigned k = 0; k < count; ++k) {
            const byte_view lig = set.from(set.u16(2 + 2 * std::size_t(k)));
            const unsigned comps = lig.u16(2);  // includes the first glyph
            if (comps == 0 || comps > kMaxComponents) continue;
            std::size_t at[kMaxComponents];
            at[0] = pos;
            bool ok = true;
            for (unsigned j = 1; j < comps && ok; ++j) {
                const std::size_t n = owner.next_unskipped(c, at[j - 1]);
                ok = n != lookup::npos && (c.glyphs[n].mask & c.mask) && c.glyphs[n].gid == lig.u16(4 + 2 * std::size_t(j - 1));
                if (ok) at[j] = n;
            }
            if (!ok) continue;
            form(c, pos, at, comps, lig.u16(0));
            next = pos + 1;
            return true;
        }
        return false;
    }
    bool add_first_glyphs(glyph_set& out) const override { return cov_.add_to(out); }

private:
    static constexpr unsigned kMaxComponents = 16;

    // Replaces the components by the ligature. Skipped glyphs between them (marks) stay, after it.
    static void form(apply_context& c, std::size_t pos, const std::size_t* at, unsigned comps, std::uint32_t gid) {
        const std::size_t last = at[comps - 1];
        std::vector<glyph_record> kept;
        for (std::size_t i = pos + 1; i <= last; ++i) {
            bool component = false;
            for (unsigned j = 1; j < comps; ++j) component = component || at[j] == i;
            if (!component) kept.push_back(c.glyphs[i]);
        }
        c.set_glyph(pos, gid, true);
        std::size_t w = pos + 1;
        for (const glyph_record& g : kept) c.glyphs[w++] = g;
        c.glyphs.erase(c.glyphs.begin() + std::ptrdiff_t(w), c.glyphs.begin() + std::ptrdiff_t(last) + 1);
    }

    byte_view v_;
    coverage cov_;
};

// --------------------------------------------- 5: context, 6: chaining context

// The three formats of both lookup types share one engine. A rule is described by where its
// backtrack, input and lookahead sequences and its lookup records sit; what each sequence
// entry means (a glyph, a class or a coverage table) depends on the format.
class context_subst final : public subtable {
public:
    context_subst(byte_view v, bool chain)
        : v_(v), chain_(chain), format_(v.u16(0)) {
        if (format_ == 1 || format_ == 2) cov_ = coverage(v.from(v.u16(2)));
        if (format_ == 2) {
            if (chain_) {
                backtrack_classes_ = class_def(v.from(v.u16(4)));
                input_classes_ = class_def(v.from(v.u16(6)));
                lookahead_classes_ = class_def(v.from(v.u16(8)));
            } else {
                input_classes_ = class_def(v.from(v.u16(4)));
            }
        }
    }

    bool apply(apply_context& c, const lookup& owner, std::size_t pos, std::size_t& next) const override {
        switch (format_) {
            case 1: return apply_sets(c, owner, pos, next, entry::glyph);
            case 2: return apply_sets(c, owner, pos, next, entry::klass);
            case 3: return apply_coverage(c, owner, pos, next);
            default: return false;
        }
    }
    bool add_first_glyphs(glyph_set& out) const override {
        if (format_ == 1 || format_ == 2) return cov_.add_to(out);
        if (format_ != 3) return true;
        // Format 3: the coverage of the first input position. Context (type 5): glyph count at 2,
        // coverage offsets from 6. Chaining (type 6): backtrack count at 2, backtrack offsets,
        // then the input count and its offsets.
        const std::size_t count_at = chain_ ? 4 + 2 * std::size_t(v_.u16(2)) : 2;
        const std::size_t first_at = chain_ ? count_at + 2 : 6;
        if (v_.u16(count_at) == 0) return true;
        return coverage(v_.from(v_.u16(first_at))).add_to(out);
    }

private:
    enum class entry { glyph, klass, coverage };

    // Where the parts of one rule are. `v` is the view the offsets are relative to.
    struct rule {
        byte_view v;
        std::size_t backtrack_at = 0, backtrack_n = 0;
        std::size_t input_at = 0, input_n = 0;  // input_n counts the first glyph
        bool input_has_first = false;           // format 3 lists the first glyph too
        std::size_t lookahead_at = 0, lookahead_n = 0;
        std::size_t records_at = 0, records_n = 0;
    };

    static constexpr std::size_t kMaxInput = 32;

    bool matches(entry kind, const class_def& classes, const byte_view& v, std::size_t at, std::size_t k,
                 std::uint32_t gid) const noexcept {
        const unsigned x = v.u16(at + 2 * k);
        switch (kind) {
            case entry::glyph: return x == gid;
            case entry::klass: return classes.class_of(gid) == x;
            case entry::coverage: return coverage(v_.from(x)).index(gid) >= 0;
        }
        return false;
    }

    // Formats 1 and 2: the first glyph picks a rule set (by glyph or by class).
    bool apply_sets(apply_context& c, const lookup& owner, std::size_t pos, std::size_t& next, entry kind) const {
        const std::uint32_t gid = c.glyphs[pos].gid;
        std::size_t index;
        std::size_t count_at, offsets_at;
        if (kind == entry::glyph) {
            const int idx = cov_.index(gid);
            if (idx < 0) return false;
            index = std::size_t(idx);
            count_at = 4;
            offsets_at = 6;
        } else {
            if (cov_.index(gid) < 0) return false;
            index = input_classes_.class_of(gid);
            count_at = chain_ ? 10 : 6;
            offsets_at = chain_ ? 12 : 8;
        }
        if (index >= v_.u16(count_at)) return false;
        const std::size_t set_off = v_.u16(offsets_at + 2 * index);
        if (set_off == 0) return false;
        const byte_view set = v_.from(set_off);
        const unsigned count = set.u16(0);
        for (unsigned k = 0; k < count; ++k) {
            const std::size_t rule_off = set.u16(2 + 2 * std::size_t(k));
            if (rule_off == 0) continue;
            rule r;
            r.v = set.from(rule_off);
            if (chain_) {
                r.backtrack_n = r.v.u16(0);
                r.backtrack_at = 2;
                std::size_t p = 2 + 2 * r.backtrack_n;
                r.input_n = r.v.u16(p);
                r.input_at = p + 2;
                p += 2 + 2 * (r.input_n ? r.input_n - 1 : 0);
                r.lookahead_n = r.v.u16(p);
                r.lookahead_at = p + 2;
                p += 2 + 2 * r.lookahead_n;
                r.records_n = r.v.u16(p);
                r.records_at = p + 2;
            } else {
                r.input_n = r.v.u16(0);
                r.records_n = r.v.u16(2);
                r.input_at = 4;
                r.records_at = 4 + 2 * (r.input_n ? r.input_n - 1 : 0);
            }
            if (try_rule(c, owner, pos, next, r, kind)) return true;
        }
        return false;
    }

    // Format 3: one coverage table per position.
    bool apply_coverage(apply_context& c, const lookup& owner, std::size_t pos, std::size_t& next) const {
        rule r;
        r.v = v_;
        r.input_has_first = true;
        if (chain_) {
            r.backtrack_n = v_.u16(2);
            r.backtrack_at = 4;
            std::size_t p = 4 + 2 * r.backtrack_n;
            r.input_n = v_.u16(p);
            r.input_at = p + 2;
            p += 2 + 2 * r.input_n;
            r.lookahead_n = v_.u16(p);
            r.lookahead_at = p + 2;
            p += 2 + 2 * r.lookahead_n;
            r.records_n = v_.u16(p);
            r.records_at = p + 2;
        } else {
            r.input_n = v_.u16(2);
            r.records_n = v_.u16(4);
            r.input_at = 6;
            r.records_at = 6 + 2 * r.input_n;
        }
        return try_rule(c, owner, pos, next, r, entry::coverage);
    }

    bool try_rule(apply_context& c, const lookup& owner, std::size_t pos, std::size_t& next, const rule& r,
                  entry kind) const {
        if (r.input_n == 0 || r.input_n > kMaxInput) return false;
        std::size_t at[kMaxInput];
        at[0] = pos;

        // Input sequence. The first glyph was matched by the coverage / rule set, except in format 3.
        if (r.input_has_first && !matches(kind, input_classes_, r.v, r.input_at, 0, c.glyphs[pos].gid)) return false;
        for (std::size_t k = 1; k < r.input_n; ++k) {
            const std::size_t j = owner.next_unskipped(c, at[k - 1]);
            if (j == lookup::npos || !(c.glyphs[j].mask & c.mask)) return false;
            const std::size_t entry_index = r.input_has_first ? k : k - 1;
            if (!matches(kind, input_classes_, r.v, r.input_at, entry_index, c.glyphs[j].gid)) return false;
            at[k] = j;
        }
        // Backtrack: the glyphs before the match, nearest first.
        std::size_t j = pos;
        for (std::size_t k = 0; k < r.backtrack_n; ++k) {
            j = owner.prev_unskipped(c, j);
            if (j == lookup::npos || !matches(kind, backtrack_classes_, r.v, r.backtrack_at, k, c.glyphs[j].gid)) return false;
        }
        // Lookahead: the glyphs after the match.
        j = at[r.input_n - 1];
        for (std::size_t k = 0; k < r.lookahead_n; ++k) {
            j = owner.next_unskipped(c, j);
            if (j == lookup::npos || !matches(kind, lookahead_classes_, r.v, r.lookahead_at, k, c.glyphs[j].gid)) return false;
        }

        // Matched: run the nested lookups at their positions in the input, adjusting the later
        // positions when a lookup changes the number of glyphs.
        std::size_t end = at[r.input_n - 1];
        for (std::size_t k = 0; k < r.records_n; ++k) {
            const std::size_t rec = r.records_at + 4 * k;
            const std::size_t seq = r.v.u16(rec);
            const lookup* nested = c.lookups.get(r.v.u16(rec + 2));
            if (seq >= r.input_n || !nested || c.depth >= kMaxDepth) continue;
            const std::size_t before = c.glyphs.size();
            std::size_t ignored = 0;
            ++c.depth;
            nested->apply_at(c, at[seq], ignored);
            --c.depth;
            const std::ptrdiff_t delta = std::ptrdiff_t(c.glyphs.size()) - std::ptrdiff_t(before);
            if (delta != 0) {
                for (std::size_t m = seq + 1; m < r.input_n; ++m) at[m] = std::size_t(std::ptrdiff_t(at[m]) + delta);
                end = std::size_t(std::ptrdiff_t(end) + delta);
            }
        }
        next = std::max(pos + 1, end + 1);
        return true;
    }

    static constexpr unsigned kMaxDepth = 6;

    byte_view v_;
    bool chain_;
    unsigned format_;
    coverage cov_;
    class_def backtrack_classes_, input_classes_, lookahead_classes_;
};

std::unique_ptr<subtable> make_subtable(unsigned type, byte_view v) {
    if (v.empty()) return nullptr;
    switch (type) {
        case 1: return std::make_unique<single_subst>(v);
        case 2: return std::make_unique<glyph_list_subst>(v, false);
        case 3: return std::make_unique<glyph_list_subst>(v, true);
        case 4: return std::make_unique<ligature_subst>(v);
        case 5: return std::make_unique<context_subst>(v, false);
        case 6: return std::make_unique<context_subst>(v, true);
        default: return nullptr;  // 8 (reverse chaining) and unknown types
    }
}

}  // namespace

// -------------------------------------------------------------- lookup_list

lookup_list::lookup_list(byte_view gsub) {
    constexpr unsigned kMaxFiltered = 512;  // bounds the memory of the filters (at most 8 KB each)
    const byte_view list = gsub.from(gsub.u16(8));
    const unsigned count = list.u16(0);
    lookups_.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t off = list.u16(2 + 2 * std::size_t(i));
        const byte_view lk = off ? list.from(off) : byte_view();
        if (lk.empty()) {
            lookups_.push_back(nullptr);
            continue;
        }
        const unsigned type = lk.u16(0), flags = lk.u16(2), n = lk.u16(4);
        const unsigned mark_set = (flags & 0x10) ? lk.u16(6 + 2 * std::size_t(n)) : 0;
        auto out = std::make_unique<lookup>(flags, mark_set);
        for (unsigned k = 0; k < n; ++k) {
            byte_view st = lk.from(lk.u16(6 + 2 * std::size_t(k)));
            unsigned real = type;
            if (type == 7) {  // extension: the real subtable is a 32-bit offset away
                if (st.u16(0) != 1) continue;
                real = st.u16(2);
                st = st.from(st.u32(4));
            }
            if (auto sub = make_subtable(real, st)) out->add(std::move(sub));
        }
        if (i < kMaxFiltered) out->build_filter();
        lookups_.push_back(std::move(out));
    }
}

// ------------------------------------------------------------- shaping_plan

bool shaping_plan::has_feature(std::uint32_t tag) const noexcept {
    return std::find(tags_.begin(), tags_.end(), tag) != tags_.end();
}

// --------------------------------------------------------------- gsub_table

gsub_table::gsub_table(byte_view table) : table_(table) {
    if (table_.empty()) return;
    lookups_ = lookup_list(table_);
}

shaping_plan gsub_table::make_plan(std::initializer_list<std::uint32_t> scripts, std::uint32_t language,
                                   const std::vector<stage_request>& stages) const {
    shaping_plan plan;
    if (!valid()) return plan;
    const byte_view script_list = table_.from(table_.u16(4));
    const byte_view feature_list = table_.from(table_.u16(6));

    // Script table: the first requested script the font has.
    byte_view script;
    for (std::uint32_t want : scripts) {
        const unsigned n = script_list.u16(0);
        for (unsigned i = 0; i < n && script.empty(); ++i) {
            const std::size_t rec = 2 + 6 * std::size_t(i);
            if (script_list.u32(rec) == want) script = script_list.from(script_list.u16(rec + 4));
        }
        if (!script.empty()) break;
    }
    if (script.empty()) return plan;

    // Language system: the requested one, else the script's default.
    byte_view lang;
    if (language) {
        const unsigned n = script.u16(2);
        for (unsigned i = 0; i < n && lang.empty(); ++i) {
            const std::size_t rec = 4 + 6 * std::size_t(i);
            if (script.u32(rec) == language) lang = script.from(script.u16(rec + 4));
        }
    }
    if (lang.empty() && script.u16(0)) lang = script.from(script.u16(0));
    if (lang.empty()) return plan;

    // The features of that language system: (tag, lookups).
    struct feature {
        std::uint32_t tag;
        std::vector<std::uint16_t> lookups;
    };
    std::vector<feature> features;
    auto add_feature = [&](unsigned index) {
        const std::size_t rec = 2 + 6 * std::size_t(index);
        if (index >= feature_list.u16(0)) return;
        const byte_view f = feature_list.from(feature_list.u16(rec + 4));
        feature out{feature_list.u32(rec), {}};
        const unsigned n = f.u16(2);
        for (unsigned k = 0; k < n; ++k) out.lookups.push_back(f.u16(4 + 2 * std::size_t(k)));
        if (!out.lookups.empty()) features.push_back(std::move(out));
    };
    const unsigned required = lang.u16(2);
    if (required != 0xFFFF) add_feature(required);
    const unsigned count = lang.u16(4);
    for (unsigned i = 0; i < count; ++i) add_feature(lang.u16(6 + 2 * std::size_t(i)));

    for (const feature& f : features) plan.tags_.push_back(f.tag);
    for (const stage_request& req : stages) {
        shaping_plan::stage st{req.mask, {}};
        for (const feature& f : features)
            if (std::find(req.features.begin(), req.features.end(), f.tag) != req.features.end())
                st.lookups.insert(st.lookups.end(), f.lookups.begin(), f.lookups.end());
        std::sort(st.lookups.begin(), st.lookups.end());
        st.lookups.erase(std::unique(st.lookups.begin(), st.lookups.end()), st.lookups.end());
        if (!st.lookups.empty()) plan.stages_.push_back(std::move(st));
    }
    return plan;
}

void gsub_table::apply(const shaping_plan& plan, std::vector<glyph_record>& glyphs, const otl::gdef* defs) const {
    apply_context c{glyphs, lookups_, defs, 0};
    std::size_t lookup_count = 0;
    for (const shaping_plan::stage& st : plan.stages()) lookup_count += st.lookups.size();
    // A sound font needs about one attempt per glyph per lookup; allow 16 times that, and a text
    // that grows to 16 times its length plus a margin (a ligature font may decompose a lot).
    c.max_glyphs = glyphs.size() * 16 + 64;
    c.ops_left = (glyphs.size() + 1) * (lookup_count + 4) * 16;
    for (const shaping_plan::stage& st : plan.stages()) {
        c.mask = st.mask;
        for (std::uint16_t index : st.lookups)
            if (const lookup* l = lookups_.get(index)) l->run(c);
    }
}

}  // namespace stb::otl
