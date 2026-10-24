#pragma once

// stb_truetype / stb_rect_pack live in src/font/font.cpp only; `font` hides
// them behind a pimpl, so no stbtt_* type appears in this header.
//
// !!! SECURITY: stb_truetype does no bounds checking of its own. Its author
// !!! states: "NO SECURITY GUARANTEE -- DO NOT USE THIS ON UNTRUSTED FONT
// !!! FILES". Only load fonts you trust (your own, or vetted system fonts).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "stb/color.hpp"
#include "stb/error.hpp"
#include "stb/image.hpp"

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
class atlas {
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

// A TrueType/OpenType font. Cheap to copy (copies share one immutable
// parsed font) and safe to use from many threads at once.
//
// Text arguments are UTF-8; '\n' starts a new line. Characters the font has
// no glyph for render as its ".notdef" glyph (check with has_glyph()).
class font {
public:
    static constexpr float max_pixel_height = 2048.0f;

    // Takes ownership of the file contents. `index` selects a face inside a
    // .ttc collection. Throws invalid_argument (index < 0), decode_error.
    static font from_memory(std::vector<std::uint8_t> data, int index = 0);
    // Also throws io_error / limit_error.
    static font open(const std::filesystem::path& path, int index = 0);

    // pixel_height is the em-height in pixels, in (0, max_pixel_height];
    // anything else throws invalid_argument.
    font_metrics metrics(float pixel_height) const;
    bool has_glyph(char32_t codepoint) const;
    float advance(char32_t codepoint, float pixel_height) const;
    // Kerning (kern table or GPOS pair adjustment) in pixels, usually <= 0.
    float kerning(char32_t left, char32_t right, float pixel_height) const;

    glyph render_glyph(char32_t codepoint, float pixel_height) const;

    // Throws invalid_argument on malformed UTF-8.
    text_size measure(std::string_view utf8, float pixel_height) const;

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
    // The same, drawn into `dst` itself (no copy; see image::composite_in_place).
    void draw_text_in_place(image& dst, std::string_view utf8, float pixel_height, int x, int y,
                            const color& text_color = color(),
                            text_align align = text_align::left) const;

    // Packs the given characters (duplicates ignored) into one width x height
    // sheet with `padding` pixels between glyphs. Throws limit_error if they
    // do not all fit.
    atlas make_atlas(std::u32string_view codepoints, float pixel_height, int width, int height,
                     int padding = 1) const;

    // Opaque pimpl (holds the font file bytes and the stbtt_fontinfo). Public
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
font_metrics estimate_metrics(float pixel_height, font_family family = font_family::sans);

// Same result type and line rules as font::measure ('\n' starts a new line, '\r' is ignored).
// Throws invalid_argument on malformed UTF-8 or a bad pixel_height.
text_size estimate_text(std::string_view utf8, float pixel_height,
                        font_family family = font_family::sans);

}  // namespace stb
