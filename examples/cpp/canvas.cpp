// Draw several texts and shapes on one image with stb::canvas.
//
//   canvas                         (the picture and font of data/, the result is ./output/canvas.png)
//   canvas photo.jpg font.ttf out.png
#include <filesystem>
#include <iostream>
#include <utility>

#include "stb.h"

// The sample pictures and fonts of the repository (data/README.md): the build sets the real path.
#ifndef LIBSTB_DATA_DIR
#define LIBSTB_DATA_DIR "data"
#endif

int main(int argc, char** argv) {
    if (argc != 1 && argc != 4) {
        std::cerr << "usage: " << argv[0] << " [<image> <font.ttf> <out.png>]\n";
        return 2;
    }
    namespace fs = std::filesystem;
    const fs::path photo_path = argc == 4 ? fs::path(argv[1]) : fs::path(LIBSTB_DATA_DIR) / "images" / "astronaut.png";
    const fs::path font_path = argc == 4 ? fs::path(argv[2]) : fs::path(LIBSTB_DATA_DIR) / "fonts" / "Vazirmatn-Regular.ttf";
    const fs::path out_path = argc == 4 ? fs::path(argv[3]) : fs::path("output") / "canvas.png";
    try {
        if (out_path.has_parent_path()) fs::create_directories(out_path.parent_path());
        stb::image photo = stb::image::open(photo_path);
        stb::font f = stb::font::open(font_path);

        // The canvas copies `photo` once. The texts are collected and drawn in finish().
        stb::canvas c(photo);

        // Defaults for the texts added from now on.
        c.set_font(f, 48).set_color("white");

        // Shapes: smooth edges, filled with the canvas colour unless told otherwise.
        c.add_rect(10, 10, 220, 60).fill({0, 0, 0, 140}).radius(12).stroke(2, "white");
        c.add_line(10, 78, 230, 78).stroke(3, "#ffcc00");

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
        out.write_png(out_path);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
