// GPOS positioning of a run of glyphs: kerning and mark attachment.
//
// Runs the lookups of the features kern, dist (adjustments), mark, mkmk, abvm and blwm (attachment)
// that the font lists for the script, in the order of the lookup list, over the glyphs in logical
// order, as other shapers do:
//   1  single adjustment            2  pair adjustment (glyph and class pairs)
//   4  mark to base                 5  mark to ligature          6  mark to mark
//   7  contextual positioning       8  chained contextual positioning (all three formats of both)
//   9  extension, wrapping any of the above
// Not read: 3 (cursive attachment), device and variation tables of values and anchors (the default
// instance of a variable font).
//
// The font is untrusted input: every read is range-checked, the nesting of lookups is bounded and the
// work for a run is bounded by its length.
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

class positioner {
public:
    positioner() = default;
    // Collects the lookups of the wanted features for the first of `scripts` the table has.
    positioner(byte_view gpos, std::initializer_list<std::uint32_t> scripts);

    bool usable() const noexcept { return !top_.empty(); }
    // Whether the font has adjustment lookups (kern, dist) for the script, so that the kerning of a
    // run is this positioner's and not a lookup of glyph pairs.
    bool kerns() const noexcept { return kerns_; }

    // Positions the glyphs of a run in logical order (after GSUB): sets glyph_record::kern, shift_x,
    // shift_y, and attached, mark_x, mark_y on every mark the font knows an anchor pair for. Other
    // glyphs, and marks without a pair, are left as they are.
    void position(std::vector<glyph_record>& glyphs, const gdef* defs) const;

    // A lookup as read from the table (used by the implementation).
    struct subtable_ref {
        unsigned type;  // 1, 2, 4, 5, 6, 7 or 8
        byte_view table;
    };
    struct lookup_ref {
        unsigned flags = 0;
        unsigned mark_set = 0;
        std::vector<subtable_ref> subtables;
    };

private:
    std::vector<lookup_ref> lookups_;      // by lookup index, those the table has (up to a bound)
    std::vector<std::uint16_t> top_;       // the lookups the features run, in order
    bool kerns_ = false;
};

}  // namespace stb::otl
