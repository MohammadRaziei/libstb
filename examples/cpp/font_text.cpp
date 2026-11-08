// Fonts in pixels: metrics, measuring, drawing a text (Latin, Persian, mixed), a text bitmap and a glyph atlas.
//
//   font_text                      (data/fonts/Vazirmatn-Regular.ttf, the results go to ./output/font_text)
//   font_text font.ttf outdir/
//
// Load fonts you trust only: the font reader checks its bounds but has not been fuzzed. A font that has
// no Arabic letters draws boxes for Persian.
#include <filesystem>
#include <iostream>

#include "stb.h"

// The sample pictures and fonts of the repository (data/README.md): the build sets the real path.
#ifndef LIBSTB_DATA_DIR
#define LIBSTB_DATA_DIR "data"
#endif

int main(int argc, char** argv) {
    if (argc != 1 && argc != 3) {
        std::cerr << "usage: " << argv[0] << " [<font.ttf> <outdir>]\n";
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path font_path = argc == 3 ? fs::path(argv[1]) : fs::path(LIBSTB_DATA_DIR) / "fonts" / "Vazirmatn-Regular.ttf";
    const fs::path out_dir = argc == 3 ? fs::path(argv[2]) : fs::path("output") / "font_text";
    fs::create_directories(out_dir);

    try {
        stb::font font = stb::font::open(font_path);   // font::open(path, index) picks a face of a .ttc
        const float size = 32.f;                      // pixel height of the text

        // 1. Numbers in pixels for a pixel height.
        const stb::font_metrics m = font.metrics(size);
        std::cout << "ascent " << m.ascent << ", descent " << m.descent << ", line height " << m.line_height() << '\n';
        std::cout << "advance of A " << font.advance(U'A', size) << ", kerning A,V " << font.kerning(U'A', U'V', size)
                  << ", has U+0633: " << font.has_glyph(U'\u0633') << '\n';
        const stb::text_size box = font.measure("Hello\nworld", size);   // lines are split on '\n'
        std::cout << "\"Hello\\nworld\" is " << box.width << " x " << box.height << " px, " << box.lines << " lines\n";

        // 2. draw_text puts a text on a COPY of the image: (x, y) is the top-left of the text box.
        stb::image background = stb::image::open(fs::path(LIBSTB_DATA_DIR) / "images" / "astronaut.png");
        stb::image labeled = font.draw_text(background, "Hello\nworld", size, 20, 16, stb::color("white"));

        // Alignment: x is the left edge, the middle or the right edge of every line.
        labeled = font.draw_text(labeled, "centered", 24, 256, 80, stb::color("#ffcc00"), stb::text_align::center);
        labeled = font.draw_text(labeled, "right", 24, 492, 80, stb::color("#ffcc00"), stb::text_align::right);

        // draw_text_in_place changes the image it is given: no copy, for a loop over many labels.
        font.draw_text_in_place(labeled, "in place", 20, 20, 80, stb::color(255, 255, 255, 200));

        // 3. Persian and Arabic: write the text as you would type it. Joining forms, ligatures and the
        //    right-to-left order are done for you, and mixed text lays out in the right order.
        labeled = font.draw_text(labeled, "سلام دنیا", 40, 492, 120, stb::color("#ffd166"), stb::text_align::right);
        labeled = font.draw_text(labeled, "libstb یعنی کتابخانه 2026", 28, 492, 190, stb::color("#9be7ff"),
                                 stb::text_align::right);
        labeled.write(out_dir / "labeled.png");

        // 4. render: a whole text as a 1-channel coverage bitmap (your own colours, gradients, masks).
        const stb::text_bitmap text = font.render("Hello", size);
        std::cout << "text bitmap " << text.bitmap.width() << "x" << text.bitmap.height() << ", origin ("
                  << text.origin_x << ", " << text.origin_y << ")\n";
        text.bitmap.write(out_dir / "hello_coverage.png");

        // 5. One glyph, and a packed glyph sheet with the lookup table a renderer needs.
        const stb::glyph one = font.render_glyph(U'A', size);
        std::cout << "glyph A " << one.bitmap.width() << "x" << one.bitmap.height() << ", advance " << one.advance << '\n';

        const stb::atlas sheet = font.make_atlas(U"ABCabc123", size, 256, 256);
        sheet.bitmap().write(out_dir / "atlas.png");
        if (const stb::atlas_glyph* g = sheet.find(U'A'))
            std::cout << "atlas A at (" << g->x0 << ", " << g->y0 << ")-(" << g->x1 << ", " << g->y1 << ")\n";
    } catch (const stb::decode_error& e) {
        std::cerr << "not a font libstb can read: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
