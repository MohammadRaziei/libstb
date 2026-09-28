#include <libstb.h>

#include <cstdio>
#include <string>

int main() {
    // 2x1 binary PPM: one red pixel, one green pixel.
    const std::string ppm = std::string("P6\n2 1\n255\n") + std::string("\xff\x00\x00\x00\xff\x00", 6);

    libstb::image img = libstb::image::decode(ppm.data(), ppm.size());
    if (img.width() != 2 || img.height() != 1 || img.channels() != 3 || img(0, 0, 0) != 255 ||
        img(1, 0, 1) != 255) {
        std::puts("unexpected decode result");
        return 1;
    }

    // encode -> decode through the encoder hierarchy
    libstb::png_encoder png;
    const libstb::encoder& base = png;
    const auto bytes = img.encode(base);
    libstb::image back = libstb::image::decode(bytes.data(), bytes.size());
    if (back.pixels() != img.pixels()) {
        std::puts("png round trip mismatch");
        return 1;
    }

    // resizer + utf8 (font shares the same static library)
    libstb::image big = libstb::resizer().resize(img, 8, 4);
    if (big.width() != 8 || big.height() != 4 || libstb::utf8_decode("\xC3\xA9").size() != 1) {
        std::puts("resize/utf8 check failed");
        return 1;
    }

    std::printf("libstb found and linked OK: all checks passed (v%d.%d.%d)\n",
                LIBSTB_VERSION_MAJOR, LIBSTB_VERSION_MINOR, LIBSTB_VERSION_PATCH);
    return 0;
}
