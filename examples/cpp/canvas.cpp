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

        // The canvas copies `photo` once. The texts are collected and drawn in finish().
        stb::canvas c(photo);

        // Defaults for the texts added from now on.
        c.set_font(f, 48).set_color("white");

        c.add_text("Title", 20, 20);                                  // uses the defaults

        // Settings for one text only (a font of its own: .font(other_font)).
        c.add_text("Subtitle", 20, 80).color("#ffcc00").bg_color("#222").padding(10);

        // center: x is the middle of every line.
        const int mid = photo.width() / 2;
        c.add_text("first line\nsecond, longer line", mid, photo.height() - 140)
            .align(stb::text_align::center);

        // Translucent text on a translucent box: the alpha of a colour is honoured.
        c.add_text("watermark", mid, photo.height() - 50)
            .align(stb::text_align::center)
            .color({255, 255, 255, 120})
            .bg_color({0, 0, 0, 90})
            .padding(6)
            .size(24);

        // Everything is drawn here, in the order added.
        stb::image out = c.finish();
        out.write_png(argv[3]);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
