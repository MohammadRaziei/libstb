// Text as vector outlines: the glyph-index API and font::shape, and a text renderer of your own.
//
//   vector_text                    (data/fonts/Vazirmatn-Regular.ttf, the result is ./output/vector_text.png)
//   vector_text font.ttf out.png
//
// draw_text does all of this in pixels. The glyph-level API hands you the pieces in FONT UNITS (the font's
// own design grid) with y pointing UP, so the text can be drawn at any size, angle or shear, filled or
// stroked, through a matrix. Load fonts you trust only.
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>

#include "stb.h"

// The sample pictures and fonts of the repository (data/README.md): the build sets the real path.
#ifndef LIBSTB_DATA_DIR
#define LIBSTB_DATA_DIR "data"
#endif

// Draws one text (lines split on '\n') whose first pen position is (x, baseline), `angle` radians clockwise.
static void draw_vector_text(stb::image& img, const stb::font& font, std::string_view utf8, float size, float x,
                             float baseline, const stb::color& color, float angle = 0.f) {
    const stb::font_design_metrics d = font.design_metrics();
    const float scale = size / float(d.ascent - d.descent);               // font units -> pixels: the scale draw_text uses for its size
    const float line_pitch = float(d.ascent - d.descent + d.line_gap) * scale;
    float pen = 0.f;                                                      // in font units
    int line = 0;
    for (const stb::shaped_glyph& g : font.shape(utf8)) {                // already in visual order
        if (g.glyph == 0) {                                               // glyph 0 is a line break
            ++line;
            pen = 0.f;
            continue;
        }
        // Each call applies to a point FIRST, so read this bottom to top: scale the glyph and flip y (font units
        // are y up, pixels y down), move it to its place on the line, turn the line, then put it on the image.
        const stb::matrix m = stb::matrix()
                                  .translate(x, baseline)
                                  .rotate(angle)
                                  .translate((pen + g.x_offset) * scale, float(line) * line_pitch - g.y_offset * scale)
                                  .scale(scale, -scale);
        stb::draw_options options;
        options.transform = m;
        stb::fill(img, stb::outline(font.outline(g.glyph)), color, stb::fill_rule::non_zero, options);
        pen += g.x_advance;
    }
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) {
        std::cerr << "usage: " << argv[0] << " [<font.ttf> <out.png>]\n";
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path font_path = argc == 3 ? fs::path(argv[1]) : fs::path(LIBSTB_DATA_DIR) / "fonts" / "Vazirmatn-Regular.ttf";
    const fs::path out_path = argc == 3 ? fs::path(argv[2]) : fs::path("output") / "vector_text.png";
    try {
        if (out_path.has_parent_path()) fs::create_directories(out_path.parent_path());
        stb::font font = stb::font::open(font_path);

        // 1. The font's grid and what it holds. font::face_count(path) says how many faces a .ttc has.
        const stb::font_design_metrics d = font.design_metrics();
        std::cout << d.units_per_em << " units per em, ascent " << d.ascent << ", descent " << d.descent << ", "
                  << font.glyph_count() << " glyphs, " << stb::font::face_count(font_path) << " face(s)\n";

        // 2. A character is a glyph index (0 is .notdef, "no glyph"); its metrics and outline are in font units.
        const std::uint32_t a = font.glyph_index(U'A'), v = font.glyph_index(U'V');
        const stb::glyph_metrics gm = font.glyph_info(a);
        std::cout << "A is glyph " << a << ": advance " << gm.advance << ", ink box (" << gm.x_min << ", " << gm.y_min
                  << ")-(" << gm.x_max << ", " << gm.y_max << ")\n";
        const stb::outline glyph_outline(font.outline(a));                // the commands as a stb::outline
        std::cout << "outline of A: " << glyph_outline.size() << " commands\n";

        // 3. Kerning between two glyphs, from the kern table, from GPOS, or none.
        const std::pair<const char*, stb::kerning_mode> modes[] = {{"none", stb::kerning_mode::none},
                                                                   {"kern_table", stb::kerning_mode::kern_table},
                                                                   {"automatic", stb::kerning_mode::automatic}};
        for (const auto& [name, mode] : modes)
            std::cout << "kerning A,V (" << name << "): " << font.glyph_kerning(a, v, mode) << '\n';

        // 4. Shaping: what draw_text decides, as data. Visual order, one entry per glyph, in font units.
        for (const stb::shaped_glyph& g : font.shape("سلام")) {
            std::cout << "glyph " << g.glyph << " from byte " << g.cluster << ", advance " << g.x_advance << '\n';
        }

        // 5. Your own text renderer on top of it.
        stb::image img(560, 300, stb::pixel_format::rgb);
        img.fill(stb::color(245, 243, 238));
        draw_vector_text(img, font, "Hello, AV kerning", 44, 20, 60, stb::color(30, 30, 30));
        draw_vector_text(img, font, "سلام دنیا", 44, 20, 120, stb::color("#1d3557"));
        draw_vector_text(img, font, "one\ntwo lines", 30, 20, 170, stb::color("#e63946"));
        draw_vector_text(img, font, "tilted text", 40, 250, 270, stb::color("#2a9d8f"), -0.35f);
        img.write(out_path);
        std::cout << "wrote " << out_path.string() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
