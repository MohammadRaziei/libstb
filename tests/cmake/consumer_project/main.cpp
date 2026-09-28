#include <libstb.h>

#include <cstdio>
#include <string>

int main() {
    // 2x1 binary PPM: one red pixel, one green pixel.
    const std::string ppm = std::string("P6\n2 1\n255\n") + std::string("\xff\x00\x00\x00\xff\x00", 6);

    libstb::image img = libstb::load(ppm.data(), ppm.size());
    if (img.width != 2 || img.height != 1 || img.channels != 3 || img.data.size() != 6 ||
        img.data[0] != 255 || img.data[4] != 255) {
        std::puts("unexpected decode result");
        return 1;
    }
    std::printf("libstb found and linked OK: all checks passed (v%d.%d.%d)\n",
                LIBSTB_VERSION_MAJOR, LIBSTB_VERSION_MINOR, LIBSTB_VERSION_PATCH);
    return 0;
}
