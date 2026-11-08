// GPOS mark positioning: where a mark (a harakat, a shadda, a small sign) sits on its base.
//
// Reads the lookups of the features `mark` and `mkmk`: type 4 (mark to base), 5 (mark to
// ligature), 6 (mark to mark), also through type 9 (extension). Anchors are read in all three
// formats, without their device or variation tables (the default instance of the font).
// Pair adjustment (kerning) is not here: it lives in truetype::face.
//
// Like the other tables of a font this one is untrusted input: every read is range-checked and the
// work for a run is bounded by its number of marks.
//
// Internal header: included only from src/font/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "gsub.hpp"
#include "otl.hpp"

namespace stb::otl {

class mark_positioner {
public:
    mark_positioner() = default;
    // Collects the lookups of `mark` and `mkmk` for the first of `scripts` the table has.
    mark_positioner(byte_view gpos, std::initializer_list<std::uint32_t> scripts);

    bool usable() const noexcept { return !lookups_.empty(); }

    // Attaches the marks of a run in logical order (after GSUB): sets glyph_record::attached and
    // mark_x / mark_y on every mark the font knows an anchor pair for. Other glyphs, and marks
    // without a pair, are left as they are.
    void position(std::vector<glyph_record>& glyphs, const gdef* defs) const;

private:
    struct subtable_ref {
        unsigned type;  // 4, 5 or 6
        byte_view table;
    };
    struct lookup_ref {
        unsigned flags = 0;
        unsigned mark_set = 0;
        std::vector<subtable_ref> subtables;
    };
    std::vector<lookup_ref> lookups_;
};

}  // namespace stb::otl
