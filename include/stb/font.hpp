#pragma once

// The font engine (TrueType / CFF outlines, the rasterizer, the rectangle packer) lives in
// src/font/ only; `font` hides it behind a pimpl, so no engine type appears in this header.
//
// !!! SECURITY: the font reader checks its bounds, but it has not been fuzzed. Only load
// !!! fonts you trust (your own, or vetted system fonts).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "stb/export.hpp"
#include "stb/color.hpp"
#include "stb/error.hpp"
#include "stb/image.hpp"
#include "stb/outline.hpp"

namespace stb {

// Vertical metrics in pixels at a given pixel height.
struct font_metrics {
    float ascent = 0;    // above the baseline (positive)
    float descent = 0;   // below the baseline (negative)
    float line_gap = 0;
    float line_height() const noexcept { return ascent - descent + line_gap; }
};

// One rasterised character: an 8-bit coverage mask (1 channel; empty for
// blank glyphs such as space) plus where it sits relative to the pen.
struct glyph {
    image bitmap;
    int x_offset = 0;      // pen -> left edge of the bitmap
    int y_offset = 0;      // baseline -> top edge (negative = above the baseline)
    float advance = 0;     // how far the pen moves afterwards
};

// Where draw_text puts `x` (and how its lines line up): left = x is the left edge of every line,
// center = x is the horizontal centre of every line, right = x is the right edge of every line.
enum class text_align { left, center, right };

struct text_size {
    float width = 0;
    float height = 0;
    int lines = 1;
};

// A rendered line of text. The bitmap covers the layout box (0,0)-(width,
// height) plus any ink overhanging it; (origin_x, origin_y) is the pixel of
// the bitmap where the layout box's top-left corner sits.
struct text_bitmap {
    image bitmap;
    int origin_x = 0;
    int origin_y = 0;
};

// Where one character lives in a glyph atlas, plus the quad offsets a
// renderer needs (relative to the pen at the baseline).
struct atlas_glyph {
    char32_t codepoint = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // pixel rectangle in the atlas
    float xoff = 0, yoff = 0;            // top-left of the quad
    float xoff2 = 0, yoff2 = 0;          // bottom-right of the quad
    float advance = 0;
};

// A packed glyph sheet: one 1-channel image plus the lookup table.
class STB_API atlas {
public:
    atlas(image bitmap, std::vector<atlas_glyph> glyphs);

    const image& bitmap() const noexcept { return bitmap_; }
    const std::vector<atlas_glyph>& glyphs() const noexcept { return glyphs_; }
    // nullptr if the codepoint is not in the atlas.
    const atlas_glyph* find(char32_t codepoint) const noexcept;

private:
    image bitmap_;
    std::vector<atlas_glyph> glyphs_;
    std::unordered_map<char32_t, std::size_t> index_;
};

// ------------------------------------------------------ outlines and font-unit metrics
//
// Everything below is in FONT UNITS, the font's own design grid (font::design_metrics().units_per_em
// per em, 1000 or 2048 usually), with y pointing UP like the font itself and no scaling or hinting
// applied: what a vector renderer needs. To draw at `size` as the em in output units multiply by
// size / units_per_em; to draw at the size draw_text() calls pixel_height (the height from the descent
// to the ascent, see font::metrics) multiply by pixel_height / (ascent - descent). Flip y if the output
// grows downwards.

// Outlines use stb::point and stb::outline_command from stb/outline.hpp, so a glyph outline goes
// straight into a stb::outline (outline::add_commands) and from there to the rasterizer.

// One glyph's horizontal metrics and ink box.
struct glyph_metrics {
    float advance = 0;       // how far the pen moves after the glyph
    float lsb = 0;           // left side bearing: pen -> the glyph's left edge (hmtx)
    float x_min = 0, y_min = 0, x_max = 0, y_max = 0;  // the ink box; all 0 when has_outline is false
    bool has_outline = false;                          // false for a glyph with nothing to draw, e.g. space
};

// The font's vertical metrics and bounding box in font units (font::metrics gives the same
// vertical numbers scaled to pixels).
struct font_design_metrics {
    int units_per_em = 0;
    int ascent = 0;    // above the baseline (positive), hhea
    int descent = 0;   // below the baseline (negative), hhea
    int line_gap = 0;  // hhea
    int x_min = 0, y_min = 0, x_max = 0, y_max = 0;  // the box that holds every glyph (head)
};

// Which kerning a glyph pair gets (font::glyph_kerning).
//   none        no kerning
//   kern_table  the legacy `kern` table only, even when the font also has GPOS
//   automatic   GPOS pair adjustment if the font has GPOS, else the `kern` table (what kerning() and
//               text rendering use)
enum class kerning_mode { none, kern_table, automatic };

// ------------------------------------------------------------------------ shaping
//
// font::shape turns UTF-8 text into the glyphs to draw, in font units like the outline API above,
// so a vector renderer can place them without going through pixels.

// The direction of the paragraph (UAX #9): automatic is that of the first strong letter, which is
// what draw_text does. Forcing it matters for text that starts with a neutral or a number, or when
// the caller knows better (a Persian UI string that opens with a Latin word).
enum class text_direction { automatic, ltr, rtl };

struct shape_options {
    text_direction direction = text_direction::automatic;
    kerning_mode kerning = kerning_mode::automatic;
    // false: the optional ligature features of the font (liga, clig) do not run, so Latin-style
    // ligatures such as the Allah glyph stay as separate letters. The required ones (rlig: lam
    // + alef) and the joining shapes always do: without them Arabic script is wrong, not plain.
    bool ligatures = true;
};

// One glyph of the result, with the pen implied: a glyph is drawn at (pen.x + x_offset, pen.y +
// y_offset) and the pen then moves right by x_advance. All in font units, y up.
struct shaped_glyph {
    std::uint32_t glyph = 0;    // glyph index of the font (what outline() and glyph_info() take); 0 for a line break
    std::uint32_t cluster = 0;  // byte offset in the UTF-8 text of the first character this glyph stands for
    float x_advance = 0;        // the glyph's advance plus the kerning to the next glyph; 0 for a mark
    float x_offset = 0;         // mark placement: from the pen to where the glyph is drawn
    float y_offset = 0;
};

// A TrueType/OpenType font. Cheap to copy (copies share one immutable
// parsed font) and safe to use from many threads at once.
//
// Text arguments are UTF-8; '\n' starts a new line. Characters the font has
// no glyph for render as its ".notdef" glyph (check with has_glyph()).
class STB_API font {
public:
    static constexpr float max_pixel_height = 2048.0f;

    // Takes ownership of the file contents. `index` selects a face inside a
    // .ttc collection. A WOFF 1.0 file (.woff) is unpacked to the font it holds, in memory, and
    // read like any other (it has one face, so `index` must be 0); WOFF2 is not supported.
    // Throws invalid_argument (index < 0), decode_error (a damaged or WOFF2 file), limit_error
    // (a WOFF that would unpack to more than 512 MiB).
    static font from_memory(std::vector<std::uint8_t> data, int index = 0);
    // Also throws io_error / limit_error.
    static font open(const std::filesystem::path& path, int index = 0);

    // pixel_height is the height in pixels from the font's descent to its ascent (hhea ascent minus
    // descent, which is the em only in fonts where the two happen to be equal), in (0, max_pixel_height];
    // so ascent - descent of the result is pixel_height. anything else throws invalid_argument.
    font_metrics metrics(float pixel_height) const;
    bool has_glyph(char32_t codepoint) const;
    float advance(char32_t codepoint, float pixel_height) const;
    // Kerning (kern table or GPOS pair adjustment) in pixels, usually <= 0.
    float kerning(char32_t left, char32_t right, float pixel_height) const;

    glyph render_glyph(char32_t codepoint, float pixel_height) const;

    // ---- glyph-index API: outlines and metrics in font units (see the section above the class).
    // The glyph indices are the font's own, the ones shape_glyphs() returns. An index at or past
    // glyph_count() is not a glyph: these functions give an empty outline, zero metrics and no
    // kerning for it instead of throwing, so they are safe in a loop over untrusted indices.

    // The glyph of a character; 0 (".notdef") if the font has none (has_glyph() tells). Throws
    // invalid_argument for a code point above U+10FFFF.
    std::uint32_t glyph_index(char32_t codepoint) const;
    std::uint32_t glyph_count() const noexcept;
    glyph_metrics glyph_info(std::uint32_t glyph) const;
    font_design_metrics design_metrics() const noexcept;

    // The outline of a glyph: every contour is move_to ... close, in the font's own point order and
    // curve types (quad_to for TrueType, cubic_to for CFF). Empty for a glyph with no outline. The
    // second form fills `out` (cleared first, its capacity kept), for drawing many glyphs without an
    // allocation each.
    std::vector<outline_command> outline(std::uint32_t glyph) const;
    void outline(std::uint32_t glyph, std::vector<outline_command>& out) const;

    // Kerning between two glyphs, in font units (usually <= 0). kerning() is the same for characters
    // and in pixels, always in automatic mode.
    float glyph_kerning(std::uint32_t left, std::uint32_t right, kerning_mode mode = kerning_mode::automatic) const noexcept;

    // How many fonts a file holds: 1 for a .ttf / .otf / .woff, the number of faces for a .ttc
    // collection (font::from_memory / open take an index below it). It does not check that every
    // face is usable, nor unpack a WOFF. Throws decode_error if the data is none of these (or is
    // WOFF2), io_error / limit_error for a path.
    static int face_count(const std::vector<std::uint8_t>& data);
    static int face_count(const std::filesystem::path& path);

    // Throws invalid_argument on malformed UTF-8.
    text_size measure(std::string_view utf8, float pixel_height) const;

    // Shapes `utf8` into positioned glyphs in VISUAL order (left to right on the page), in font
    // units with y up. It makes the same decisions as draw_text (and shape_glyphs): joined letter
    // shapes, ligatures and the other GSUB features of the font, mirrored brackets, the
    // right-to-left order, and kerning; what differs is that nothing is rounded to pixels.
    //
    //   - cluster says where a glyph came from, so a caller can map a glyph back to the text (a caret,
    //     a selection, a link). It is the byte offset of the first character the glyph stands for:
    //     a ligature takes the offset of its first letter, and in a right-to-left run the offsets
    //     go down as the glyphs go right. A character that no glyph carries on its own (the second
    //     letter of a ligature) is the part of the text between one cluster and the next one in
    //     text order. Zero width joiners and the other invisible bidi controls disappear from a
    //     right-to-left line, and '\r' always does; no glyph names them.
    //   - A '\n' is a glyph with glyph 0, no advance and a cluster of its own: the caller starts the
    //     next line (its pitch is design_metrics() ascent - descent + line_gap) and the pen goes
    //     back to 0. Each line is shaped and ordered on its own.
    //   - Kerning (options.kerning) is added to the advance of the first glyph of the pair, so the
    //     pen needs nothing else. Marks (the harakat of Arabic script) take no advance and do not
    //     break a pair; they are centred on the ink of the letter before them and stacked above or
    //     below it with x_offset and y_offset. The font's GPOS mark attachment is not read, and a
    //     mark may therefore sit up to a pixel off where draw_text puts it, which snaps it to its
    //     pixel grid.
    //
    // Only right-to-left scripts get the GSUB treatment so far: other text is one glyph per
    // character. Throws invalid_argument on malformed UTF-8, limit_error for text over 4 GiB.
    std::vector<shaped_glyph> shape(std::string_view utf8, const shape_options& options = {}) const;

    // The glyph indices of the font that `utf8` is drawn with, in visual order ('\n' is 0 and
    // starts a new line). This is what shaping decides: joined letter shapes, ligatures, mirrored
    // brackets and the right-to-left order. For diagnostics and tests; marks follow their letter.
    std::vector<std::uint32_t> shape_glyphs(std::string_view utf8) const;
    // Single 1-channel coverage image of the whole text. Throws
    // invalid_argument (malformed UTF-8, or empty text), limit_error
    // (result would exceed max_bytes).
    text_bitmap render(std::string_view utf8, float pixel_height,
                       std::size_t max_bytes = std::size_t(1) << 28) const;

    // A copy of `dst` with `utf8` drawn on it in `text_color`. The colour's alpha scales the glyph
    // coverage, so color(255, 255, 255, 128) gives translucent text ("red"_c and color("#ff8800")
    // work too). (x, y) is where the text's layout box starts (its top-left corner); with
    // text_align::center or right, `x` is instead the centre or right edge of each line (see
    // text_align). The text may hang over the edges and is clipped. `dst` may have 1 to 4
    // channels: gray gets the luminance of the colour, and alpha is blended with the usual "over"
    // operator (see image::composite). Throws what render() throws, plus invalid_argument for an
    // empty `dst`.
    image draw_text(const image& dst, std::string_view utf8, float pixel_height, int x, int y,
                    const color& text_color = color(), text_align align = text_align::left) const;
    // The same, drawn into `dst` itself (no copy; see image::blend).
    void draw_text_in_place(image& dst, std::string_view utf8, float pixel_height, int x, int y,
                            const color& text_color = color(),
                            text_align align = text_align::left) const;

    // Packs the given characters (duplicates ignored) into one width x height
    // sheet with `padding` pixels between glyphs. Throws limit_error if they
    // do not all fit.
    atlas make_atlas(std::u32string_view codepoints, float pixel_height, int width, int height,
                     int padding = 1) const;

    // Opaque pimpl (holds the font file bytes and the parsed font face). Public
    // only so that helpers in font.cpp can name it; it is incomplete here, so
    // nothing outside stb_core can look inside.
    struct impl;

private:
    explicit font(std::shared_ptr<const impl> p) noexcept : impl_(std::move(p)) {}

    std::shared_ptr<const impl> impl_;
};

// ---------------------------------------------------------------- estimates
//
// Rough text size WITHOUT a font file, for layout code that only needs to know how much room a
// string takes (label boxes, SVG sizing, tests that must give the same numbers on every
// machine). Advance widths come from fixed tables of three metric-compatible families:
//   sans  ~ Arial / Helvetica      serif ~ Times New Roman      mono ~ Courier New
// (derived from Liberation Sans / Serif / Mono, SIL OFL; no font is bundled).
//
// It is an estimate: no kerning, no shaping, no bold or italic. Basic Latin, Latin-1 and common
// punctuation use the table; CJK, Hangul, fullwidth forms and emoji count as 1 em; combining
// marks and control characters as 0; any other character (Cyrillic, Greek, Arabic, ...) gets the
// family's average letter width, so such text is only roughly right. For exact numbers load a
// real font and use font::measure.

enum class font_family { sans, serif, mono };

// Vertical metrics at `pixel_height` (the em height; same meaning as font::metrics).
// Throws invalid_argument unless 0 < pixel_height <= font::max_pixel_height.
STB_API font_metrics estimate_metrics(float pixel_height, font_family family = font_family::sans);

// Same result type and line rules as font::measure ('\n' starts a new line, '\r' is ignored).
// Throws invalid_argument on malformed UTF-8 or a bad pixel_height.
STB_API text_size estimate_text(std::string_view utf8, float pixel_height,
                        font_family family = font_family::sans);

}  // namespace stb
