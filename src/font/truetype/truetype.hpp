// Reading TrueType and OpenType fonts and rasterising their glyphs. libstb's own code, written
// from the algorithms of stb_truetype.h v1.26 (public domain / MIT, Sean Barrett and contributors)
// and giving the same pixels:
//
//   face.cpp      the file: tables, cmap, metrics, kerning, glyph outlines (glyf and CFF)
//   raster.cpp    outline -> 8-bit coverage bitmap (curve flattening, scanline area coverage)
//   atlas.cpp     packing many glyph bitmaps into one image
//
// A font is untrusted input, so every read goes through otl::byte_view: out-of-range reads give
// 0 instead of touching memory outside the file.
//
// Internal header: included only from src/font/.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "font/shaper/otl.hpp"

namespace stb::truetype {

// One point of a glyph outline, in font units. y points up.
struct vertex {
    enum kind : std::uint8_t { move = 1, line = 2, curve = 3, cubic = 4 };

    std::int16_t x = 0, y = 0;
    std::int16_t cx = 0, cy = 0;    // control point of a quadratic curve (or the first of a cubic)
    std::int16_t cx1 = 0, cy1 = 0;  // second control point of a cubic
    std::uint8_t type = 0;
};

struct box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// A glyph's coverage: width * height bytes, 255 = fully covered, drawn at (x_offset, y_offset)
// from the pen position on the baseline (y grows downwards).
struct bitmap {
    int width = 0, height = 0;
    int x_offset = 0, y_offset = 0;
    std::vector<std::uint8_t> pixels;  // empty when the glyph has no pixels (a space)
};

// One font of a file. A view onto the file's bytes, which must outlive the face.
class face {
public:
    // The offset of font `index` in `file`: a plain .ttf / .otf holds one font (index 0 at offset
    // 0), a .ttc collection several. -1 if there is no such font.
    static int offset_for_index(otl::byte_view file, int index);

    face() = default;

    // False when the data is not a font this reader understands (TrueType outlines or CFF
    // Type 2 charstrings, with a Unicode cmap).
    bool init(otl::byte_view file, int font_start);

    int font_start() const { return font_start_; }

    // The glyph of a Unicode code point, 0 (.notdef) when the font has none.
    int glyph_index(int codepoint) const;

    // hhea ascender, descender (negative) and line gap, in font units.
    void v_metrics(int& ascent, int& descent, int& line_gap) const;
    // Advance width and left side bearing of a glyph, in font units.
    void h_metrics(int glyph, int& advance, int& left_bearing) const;
    // Kerning between two glyphs in font units: GPOS pair adjustment if the font has it, else the
    // `kern` table.
    int kern_advance(int left, int right) const;

    // The scale that makes (ascent - descent) `height` pixels tall.
    float scale_for_pixel_height(float height) const;

    // The outline's bounding box in font units; false for a glyph without one (a space).
    bool glyph_box(int glyph, box& out) const;
    // The outline, empty for a glyph without one.
    std::vector<vertex> outline(int glyph) const;

    // The pixels the glyph covers at this scale, shifted by a subpixel amount.
    box bitmap_box(int glyph, float scale_x, float scale_y, float shift_x = 0, float shift_y = 0) const;
    bitmap render(int glyph, float scale_x, float scale_y, float shift_x = 0, float shift_y = 0) const;
    // Rasterises into a w * h bitmap that starts at `out` (rows `stride` bytes apart). The bitmap's
    // top left is the first pixel of bitmap_box. Pixels the glyph does not touch are set to 0.
    void render_into(int glyph, std::uint8_t* out, int w, int h, int stride, float scale_x, float scale_y,
                     float shift_x = 0, float shift_y = 0) const;

    // A cursor over a CFF structure (INDEX, DICT or charstring): reads past the end give 0.
    struct cff_buffer {
        const std::uint8_t* data = nullptr;
        int size = 0;
        int cursor = 0;

        std::uint8_t get8() { return cursor >= size ? 0 : data[cursor++]; }
        std::uint8_t peek8() const { return cursor >= size ? 0 : data[cursor]; }
        void seek(int o) { cursor = (o > size || o < 0) ? size : o; }
        void skip(int o) { seek(cursor + o); }
        std::uint32_t get(int n) {
            std::uint32_t v = 0;
            for (int i = 0; i < n; ++i) v = (v << 8) | get8();
            return v;
        }
        std::uint32_t get16() { return get(2); }
        std::uint32_t get32() { return get(4); }
        cff_buffer range(int o, int s) const;
    };

private:
    // The outline being collected while a charstring runs.
    struct charstring_context;

    static cff_buffer subroutine(cff_buffer index, int n);
    static cff_buffer cff_index(cff_buffer& b);
    static cff_buffer cff_index_get(cff_buffer b, int i);
    static int cff_index_count(cff_buffer& b);
    static std::uint32_t cff_int(cff_buffer& b);
    static cff_buffer dict_get(cff_buffer& b, int key);
    static void dict_get_ints(cff_buffer& b, int key, int count, std::uint32_t* out);
    static cff_buffer private_subrs(cff_buffer cff, cff_buffer font_dict);
    cff_buffer glyph_subrs(int glyph) const;
    bool init_cff(int font_start);
    bool run_charstring(int glyph, charstring_context& ctx) const;

    otl::byte_view glyph_data(int glyph) const;
    std::vector<vertex> outline_truetype(int glyph, int depth) const;
    std::vector<vertex> outline_cff(int glyph) const;
    box box_cff(int glyph) const;
    std::vector<vertex> simple_glyph(otl::byte_view g, int contours) const;
    std::vector<vertex> composite_glyph(otl::byte_view g, int depth) const;
    int glyph_index_in_cmap(int codepoint) const;
    int kern_pair_advance(int left, int right) const;
    int gpos_pair_advance(int left, int right) const;

    otl::byte_view file_;
    int font_start_ = 0;
    int num_glyphs_ = 0;
    int index_to_loc_format_ = 0;

    otl::byte_view head_, hhea_, hmtx_, loca_, glyf_, kern_, gpos_;
    otl::byte_view cmap_;  // the encoding subtable that was picked
    bool is_cff_ = false;

    cff_buffer cff_, char_strings_, global_subrs_, subrs_, font_dicts_, fd_select_;
};

// ---- raster.cpp

// Fills `pixels` (w * h, rows `stride` apart) with the coverage of `outline` scaled to pixels and
// shifted: the glyph's top left pixel is (x_off, y_off). `invert` flips y (fonts have y up).
void rasterize(std::uint8_t* pixels, int w, int h, int stride, float flatness_in_pixels,
               const std::vector<vertex>& outline, float scale_x, float scale_y, float shift_x, float shift_y,
               int x_off, int y_off, bool invert);

// ---- atlas.cpp

// Where one glyph of an atlas went: its pixels are [x0, x1) x [y0, y1) of the atlas image; draw
// them at (pen + x_offset, baseline + y_offset) .. (pen + x_offset2, baseline + y_offset2).
struct packed_glyph {
    std::int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float x_offset = 0, y_offset = 0;
    float x_advance = 0;
    float x_offset2 = 0, y_offset2 = 0;
};

// Rasterises the glyphs of `codepoints` at `pixel_height` into `pixels` (width * height bytes,
// zeroed here) and records where each went. `padding` empty pixels surround every glyph. All glyphs
// the font lacks share the one .notdef image. False if they did not all fit.
bool pack_glyphs(const face& font, std::uint8_t* pixels, int width, int height, int padding, float pixel_height,
                 const std::vector<int>& codepoints, std::vector<packed_glyph>& out);

}  // namespace stb::truetype
