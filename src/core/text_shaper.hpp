// Turns logical text into the glyphs to draw, in visual order.
//
// Pipeline for one line, only when it has a right-to-left letter:
//   1. joining analysis (arabic::joining_forms) on the logical text;
//   2. one glyph per character: if the font has GSUB features for the shapes (isol, init, medi,
//      fina) they decide, and a shape without a feature is the plain letter; else the shape is the
//      Arabic Presentation Form in the cmap, else the plain letter; lam + alef is left to GSUB (rlig) or the presentation ligature;
//   3. the rest of the font's GSUB features run over each Arabic run (ccmp, rlig, calt, liga ...);
//   4. the line is ordered with the bidirectional algorithm, brackets are mirrored, and a
//      letter's marks stay after it.
// Lines without a right-to-left letter come back as one glyph per character.
//
// Internal header: included only from src/core/*.cpp.
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "gsub.hpp"
#include "otl.hpp"

namespace stb {

// Bits of glyph_record::mask: which features may act on a glyph.
enum : std::uint32_t { mask_all = 1, mask_isol = 2, mask_fina = 4, mask_medi = 8, mask_init = 16 };

// The GSUB side of one font for Arabic script: parsed once, then shared by every shaping call.
struct arabic_layout {
    otl::gdef defs;
    otl::gsub_table gsub;
    otl::shaping_plan plan;
    bool usable = false;       // the font has GSUB lookups for Arabic (or default) script
    bool has_forms = false;    // GSUB defines any of isol, fina, medi, init: then it decides every shape
    bool has_rlig = false;     // required ligatures (lam-alef) come from GSUB

    static arabic_layout build(otl::byte_view file, std::uint32_t font_offset);
};

// What the shaper needs from a font.
class glyph_source {
public:
    virtual ~glyph_source() = default;
    // The glyph for a code point, 0 if the font has none.
    virtual std::uint32_t glyph(char32_t cp) const = 0;
    // GSUB data for Arabic script, or null (then presentation forms are used).
    virtual const arabic_layout* arabic() const { return nullptr; }
};

struct shaped_glyph {
    std::uint32_t gid;  // 0: none, draw `cp` as it is (or nothing for '\n')
    char32_t cp;        // the character this glyph stands for (first one, for a ligature)
};

class text_shaper {
public:
    explicit text_shaper(const glyph_source& source) : src_(source) {}

    // Shapes `text`; every '\n' becomes a glyph with gid 0 and cp '\n' and starts a new line.
    std::vector<shaped_glyph> shape(std::u32string_view text) const;

private:
    void shape_line(std::u32string_view line, std::vector<shaped_glyph>& out) const;

    const glyph_source& src_;
};

}  // namespace stb
