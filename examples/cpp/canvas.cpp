// Draw several texts on one image with stb::canvas.
//
//   canvas_example photo.jpg font.ttf out.png
#include <iostream>
#include <utility>

#include "stb.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: " << argv[0] << " <image> <font.ttf> <out.png>\n";
        return 2;
    }
    try {
        stb::image photo = stb::image::open(argv[1]);
        stb::font f = stb::font::open(argv[2]);

        // The canvas copies `photo` once. Every draw_text blends into that copy.
        stb::canvas c(photo);
        c.set_font(f, 48).set_color("white");

        // Current font, size, colour and alignment (left: x is the left edge).
        c.draw_text("Title", 20, 20);

        // Colour for this call only.
        c.draw_text("Subtitle", 20, 80, "#ffcc00");

        // center: x is the middle of every line.
        const int mid = photo.width() / 2;
        c.set_align(stb::text_align::center);
        c.draw_text("first line\nsecond, longer line", mid, photo.height() - 140);

        // Translucent text: the alpha of the colour scales the glyph coverage.
        c.draw_text("watermark", mid, photo.height() - 50, {255, 255, 255, 120});

        // No copy here: the canvas is empty afterwards.
        stb::image out = std::move(c).finish();
        out.write_png(argv[3]);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
