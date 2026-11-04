#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "stb/export.hpp"

namespace stb {

// Right-to-left text (Persian, Arabic, Urdu, Hebrew) for a renderer that draws glyphs left to
// right, one code point at a time, like font::draw_text. font does this for you; the function is
// public so it can be tested and reused.
//
// shape_for_display turns the LOGICAL text (the order it is typed and stored in) into the
// VISUAL one (the order the glyphs are drawn, left to right):
//
//   1. Arabic-script letters join: each letter becomes its isolated, initial, medial or final
//      form (the "Arabic Presentation Forms" code points U+FB50..U+FDFF and U+FE70..U+FEFE), and
//      lam + alef becomes the lam-alef ligature. This function works on code points and knows
//      no font tables: `has_glyph` says which presentation forms the font has, a form it lacks
//      falls back to the plain letter. (font::draw_text, measure and render do more: they read
//      the font's GSUB table, see font.hpp and docs/guide.md; they do not go through this
//      function.)
//   2. Zero width joiner and non-joiner stop or force joining and are then removed, as are the
//      other invisible bidi controls (LRM, RLM, embeddings, isolates).
//   3. The line is reordered by the Unicode bidirectional algorithm (UAX #9, without explicit
//      embeddings and isolates): the paragraph direction is that of the first strong letter,
//      Latin words and digit runs inside right-to-left text stay left to right, brackets are
//      mirrored. Combining marks stay after their letter.
//
// Text without right-to-left characters is returned unchanged, so left-to-right text costs
// nothing and draws exactly as before. `logical` is one line: a '\n' is kept in place and
// separates lines that are shaped and ordered independently.
//
// `has_glyph(cp)` says whether the font can draw a code point (font::has_glyph); it picks
// between a presentation form and the plain letter. If empty, every form is assumed present.
STB_API std::u32string shape_for_display(std::u32string_view logical, const std::function<bool(char32_t)>& has_glyph = {});

// True if the text has a right-to-left letter or an Arabic-Indic digit, i.e. if shape_for_display
// would change anything.
STB_API bool has_rtl(std::u32string_view text);

// True for the nonspacing marks of the Arabic blocks (harakat, shadda, small signs): the code
// points font::draw_text puts over or under the letter before them instead of after it.
STB_API bool is_arabic_mark(char32_t cp);

// True if the mark sits below its letter (kasra, kasratan, hamza below, ...); false if above.
STB_API bool is_arabic_mark_below(char32_t cp);

}  // namespace stb
