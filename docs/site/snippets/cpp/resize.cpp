#include <filesystem>
#include <iostream>
#include <string>

#include "stb.h"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: resize <data-dir> <out-dir>\n";
        return 2;
    }
    const fs::path data = argv[1], out = argv[2];
    fs::create_directories(out);

    const stb::image img = stb::image::open(data / "images" / "astronaut.png");

    // Shrink with the default filter, then enlarge the tiny result with each filter by name.
    const stb::image tiny = img.resize(64, 64);
    tiny.write(out / "tiny.png");

    for (const char* name : {"nearest", "linear", "cubic", "bspline", "mitchell"}) {
        tiny.resize(256, 256, name).write(out / (std::string(name) + ".png"));
    }
}
