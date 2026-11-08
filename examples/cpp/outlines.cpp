// Vector outlines: fill, stroke (caps, joins, dashes) and a canvas with outlines, drawn into one image.
//
//   outlines                       (the result is ./output/outlines.png)
//   outlines out.png
#include <cmath>
#include <filesystem>
#include <iostream>

#include "stb.h"

int main(int argc, char** argv) {
    if (argc > 2) {
        std::cerr << "usage: " << argv[0] << " [<out.png>]\n";
        return 2;
    }
    const std::filesystem::path out_path = argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path("output") / "outlines.png";
    try {
        if (out_path.has_parent_path()) std::filesystem::create_directories(out_path.parent_path());
        stb::image img(360, 240, stb::pixel_format::rgba);
        img.fill(stb::color(250, 248, 240));

        // A heart from two cubic curves, filled and then outlined with a dashed round-joined line.
        stb::outline heart;
        heart.move_to(60, 190).cubic_to(-20, 120, 30, 40, 60, 90).cubic_to(90, 40, 140, 120, 60, 190).close();
        stb::fill(img, heart, stb::color("#e63946"));
        stb::stroke_style dashed{3.f};
        dashed.join = stb::line_join::round;
        dashed.cap = stb::line_cap::round;
        dashed.dashes = {8.f, 6.f};
        stb::stroke(img, heart, stb::color("#1d3557"), dashed);

        // A star filled with the even-odd rule (a hole in the middle) and moved by a matrix.
        stb::outline star;
        for (int i = 0; i < 5; ++i) {
            const float a = 2.f * 3.14159265f * float(i * 2) / 5.f;
            const float x = 50.f * std::sin(a), y = -50.f * std::cos(a);
            if (i == 0) star.move_to(x, y); else star.line_to(x, y);
        }
        star.close();
        stb::draw_options placed;
        placed.transform = stb::matrix().translate(200, 90).rotate(0.2f);
        stb::fill(img, star, stb::color("#f4a261"), stb::fill_rule::even_odd, placed);
        stb::stroke(img, star, stb::color("#264653"), stb::stroke_style{2.f}, placed);

        // Caps and joins side by side, and the same shapes with a canvas.
        stb::outline zigzag;
        zigzag.move_to(0, 40).line_to(25, 0).line_to(50, 40).line_to(75, 0);
        stb::canvas c(img);
        const stb::line_cap caps[] = {stb::line_cap::butt, stb::line_cap::round, stb::line_cap::square};
        const stb::line_join joins[] = {stb::line_join::miter, stb::line_join::round, stb::line_join::bevel};
        for (int i = 0; i < 3; ++i) {
            c.add_outline(zigzag)
                .no_fill()
                .stroke(10, "#2a9d8f")
                .cap(caps[i])
                .join(joins[i])
                .transform(stb::matrix::translation(255.f, 20.f + 62.f * float(i)))
                .opacity(0.9f);
        }
        c.add_outline(stb::outline().add_rounded_rect(15, 15, 120, 40, 12)).fill(stb::color(0, 0, 0, 40)).stroke(1.5f, "#555");
        c.finish().write(out_path);
        std::cout << "wrote " << out_path.string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
