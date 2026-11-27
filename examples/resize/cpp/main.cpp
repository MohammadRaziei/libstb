#include <filesystem>
#include <string>

#include "stb.h"

namespace fs = std::filesystem;

int main() {
    const fs::path out = "out";
    fs::create_directories(out);

    const stb::image img = stb::image::open("astronaut.png");

    // Shrink with the default filter, then enlarge the tiny result with each filter by name.
    const stb::image tiny = img.resize(64, 64);
    tiny.write(out / "tiny.png");

    for (const char* name : {"nearest", "linear", "cubic", "bspline", "mitchell"}) {
        tiny.resize(256, 256, name).write(out / (std::string(name) + ".png"));
    }
}
