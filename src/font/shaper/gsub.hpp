// GSUB: glyph substitution. Parses the lookups of a font's GSUB table and runs them over a run
// of glyphs in logical order.
//
// Supported lookup types: 1 single, 2 multiple, 3 alternate (first alternate), 4 ligature,
// 5 context, 6 chaining context (all three formats of 5 and 6) and 7 extension. Type 8 (reverse
// chaining) is not read; such a lookup is skipped. Lookup flags (ignore base / ligatures / marks,
// mark attachment type, mark filtering sets) are honoured through GDEF.
//
// Internal header: included only from src/font/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

#include "otl.hpp"

namespace stb::otl {

// One glyph of the run being shaped.
struct glyph_record {
    std::uint32_t gid = 0;
    char32_t cp = 0;                         // first source character of the cluster
    std::uint32_t mask = 1;                  // features that may act on this glyph (see shaping_plan)
    std::uint8_t gclass = gc_unclassified;  // glyph_class
};

class lookup;
class lookup_list;

// What a lookup works with while it runs.
struct apply_context {
    std::vector<glyph_record>& glyphs;
    const lookup_list& lookups;
    const otl::gdef* defs;  // GDEF, may be null: then glyph classes are only what the caller set
    std::uint32_t mask;  // a glyph takes part only if (glyph.mask & mask) != 0
    unsigned depth = 0;  // nesting of lookups called from context lookups

    // Limits against a damaged or hostile font: how many glyphs the run may grow to, and how many
    // lookup attempts (including nested ones) may still be made. When the budget is spent the
    // remaining lookups do nothing, so shaping always ends in time proportional to the text.
    std::size_t max_glyphs = static_cast<std::size_t>(-1);
    std::size_t ops_left = static_cast<std::size_t>(-1);

    // Replaces the glyph id at `i`, keeping its place in the cluster. The class follows GDEF
    // when the font has glyph classes; otherwise a ligature becomes a ligature glyph and any other
    // glyph keeps its class.
    void set_glyph(std::size_t i, std::uint32_t gid, bool ligature = false);
};

// One subtable of a lookup.
class subtable {
public:
    virtual ~subtable() = default;
    // Tries to substitute at glyphs[pos]. On success returns true and sets `next` to where the
    // lookup goes on (always > pos, except when the glyph at pos was deleted).
    virtual bool apply(apply_context& c, const lookup& owner, std::size_t pos, std::size_t& next) const = 0;
    // Adds every glyph this subtable can start at to `out`; false if that cannot be told cheaply.
    virtual bool add_first_glyphs(glyph_set& out) const = 0;
};

class lookup {
public:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    lookup(unsigned flags, unsigned mark_filtering_set) : flags_(flags), mark_set_(mark_filtering_set) {}
    void add(std::unique_ptr<subtable> st) { subtables_.push_back(std::move(st)); }
    // Builds the set of glyphs the lookup can start at, so run() passes over all the others with
    // one bit test. Without it (or if some subtable cannot say) every glyph is tried.
    void build_filter();

    // Runs the lookup over the whole run.
    void run(apply_context& c) const;
    // Tries every subtable at one position (no skipping check on that glyph).
    bool apply_at(apply_context& c, std::size_t pos, std::size_t& next) const;

    // Whether the lookup flags make this glyph invisible to the lookup.
    bool skips(const apply_context& c, const glyph_record& g) const noexcept;
    // The closest glyph after / before `i` that the lookup does not skip, or npos.
    std::size_t next_unskipped(const apply_context& c, std::size_t i) const noexcept;
    std::size_t prev_unskipped(const apply_context& c, std::size_t i) const noexcept;

private:
    unsigned flags_;
    unsigned mark_set_;
    std::vector<std::unique_ptr<subtable>> subtables_;
    glyph_set first_;
    bool filtered_ = false;
};

// All lookups of a GSUB table, parsed once.
class lookup_list {
public:
    lookup_list() = default;
    explicit lookup_list(byte_view gsub);
    // Null if the index is out of range or the lookup could not be read.
    const lookup* get(std::size_t index) const noexcept { return index < lookups_.size() ? lookups_[index].get() : nullptr; }

private:
    std::vector<std::unique_ptr<lookup>> lookups_;
};

// What to apply: the lookups of the font's features, grouped in stages that run one after the
// other. A stage with a form mask only touches glyphs carrying that bit.
struct stage_request {
    std::vector<std::uint32_t> features;  // tags, see make_tag
    std::uint32_t mask;
};

class shaping_plan {
public:
    struct stage {
        std::uint32_t mask;
        std::vector<std::uint16_t> lookups;  // ascending, no duplicates
    };
    // True if the font defines the feature (with at least one lookup) for the chosen script.
    bool has_feature(std::uint32_t tag) const noexcept;
    const std::vector<stage>& stages() const noexcept { return stages_; }

private:
    friend class gsub_table;
    std::vector<stage> stages_;
    std::vector<std::uint32_t> tags_;
};

class gsub_table {
public:
    gsub_table() = default;
    explicit gsub_table(byte_view table);

    bool valid() const noexcept { return !table_.empty(); }

    // Chooses the first of `scripts` the font has (else none), the language system `language`
    // (0: the script's default), and collects the lookups of the requested features.
    shaping_plan make_plan(std::initializer_list<std::uint32_t> scripts, std::uint32_t language,
                           const std::vector<stage_request>& stages) const;

    // Runs the plan over `glyphs` (a run in logical order).
    void apply(const shaping_plan& plan, std::vector<glyph_record>& glyphs, const otl::gdef* defs) const;

private:
    byte_view table_;
    lookup_list lookups_;
};

}  // namespace stb::otl
