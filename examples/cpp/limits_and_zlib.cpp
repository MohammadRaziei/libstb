// Untrusted input: decode from memory, size limits decided from the header, typed errors, pixels you own, zlib.
//
//   limits_and_zlib
//
// The hostile files are built here (fake_png); the good one is a picture of data/.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "stb.h"

// The sample pictures and fonts of the repository (data/README.md): the build sets the real path.
#ifndef LIBSTB_DATA_DIR
#define LIBSTB_DATA_DIR "data"
#endif

// A PNG whose header says width x height and which has no pixels behind it: what a malicious upload looks like.
static std::vector<std::uint8_t> fake_png(std::uint32_t width, std::uint32_t height) {
    auto crc32 = [](const std::uint8_t* p, std::size_t n) {
        std::uint32_t c = ~0u;
        for (std::size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        return ~c;
    };
    auto put32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) {
        for (int shift = 24; shift >= 0; shift -= 8) v.push_back(std::uint8_t(x >> shift));
    };
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    auto chunk = [&](const char* type, const std::vector<std::uint8_t>& body) {
        put32(png, std::uint32_t(body.size()));
        std::vector<std::uint8_t> typed(type, type + 4);
        typed.insert(typed.end(), body.begin(), body.end());
        png.insert(png.end(), typed.begin(), typed.end());
        put32(png, crc32(typed.data(), typed.size()));
    };
    std::vector<std::uint8_t> ihdr;
    put32(ihdr, width);
    put32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});          // 8 bits, RGB, the only methods PNG has, no interlace
    chunk("IHDR", ihdr);
    chunk("IDAT", stb::zlib_compress(std::vector<std::uint8_t>{}));
    chunk("IEND", {});
    return png;
}

// Decodes `data` and says what happened. Each error has its own type: a bad upload is an exception you can name.
static void try_decode(const std::string& what, const std::vector<std::uint8_t>& data, const stb::load_options& opt = {}) {
    try {
        const stb::image img = stb::image::decode(data.data(), data.size(), opt);
        std::cout << what << ": decoded " << img.width() << "x" << img.height() << '\n';
    } catch (const stb::limit_error& e) {
        std::cout << what << ": limit_error (" << e.what() << ")\n";
    } catch (const stb::decode_error& e) {
        std::cout << what << ": decode_error (" << e.what() << ")\n";
    } catch (const stb::error& e) {                     // io_error, encode_error ...
        std::cout << what << ": " << e.what() << '\n';
    }
}

int main() {
    try {
        // 1. A whole file in memory, and back. The format is found from the bytes, not from a name.
        const stb::image img = stb::image::open(LIBSTB_DATA_DIR "/images/chelsea.png");
        const std::vector<std::uint8_t> png = img.to_png(9);
        try_decode("a real png", png);

        // 2. Header only: the size and format, with no pixel memory at all.
        const std::vector<std::uint8_t> bomb = fake_png(15000, 15000);   // would be 675 MB of RGB
        const stb::image_info info = stb::image_info::read(bomb.data(), bomb.size());
        std::cout << "the header of the bomb says " << info.width << "x" << info.height << " "
                  << stb::pixel_format_name(info.format) << ", read without decoding anything\n";

        // 3. Limits are checked from that header, BEFORE any pixel memory exists. The default is 512 MiB.
        try_decode("the bomb, default limit", bomb);
        stb::load_options strict;
        strict.max_bytes = 1u << 20;                                      // 1 MiB of pixels at most
        try_decode("a real png, 1 MiB limit", png, strict);               // 451*300*3 bytes: fits

        // 4. Typed errors for what is not an image, or not all of one.
        try_decode("garbage", std::vector<std::uint8_t>(100, 0x42));
        try_decode("half a png", std::vector<std::uint8_t>(png.begin(), png.begin() + png.size() / 2));
        try {
            stb::image::open("no/such/file.png");
        } catch (const stb::io_error& e) {
            std::cout << "missing file: io_error (" << e.what() << ")\n";
        }

        // 5. Pixels somebody else owns: wrap() is a view with no copy (the memory must outlive it);
        //    copy() makes an image that owns its pixels.
        std::vector<std::uint8_t> mine(8 * 8 * 4, 255);
        stb::image view = stb::image::wrap(8, 8, stb::pixel_format::rgba, mine.data());
        view(0, 0, 0) = 0;                                                // red of pixel (0, 0): writes into `mine`
        stb::image owned = view.copy();
        mine[0] = 99;
        std::cout << "wrap sees " << int(view(0, 0, 0)) << ", copy kept " << int(owned(0, 0, 0)) << '\n';

        // 6. zlib, with the same bound: a zip bomb is a limit_error, not an out-of-memory.
        const std::vector<std::uint8_t> text(100000, 'a');
        const std::vector<std::uint8_t> packed = stb::zlib_compress(text, 9);
        const std::vector<std::uint8_t> raw = stb::zlib_decompress(packed, 1u << 20);
        std::cout << "zlib: " << text.size() << " -> " << packed.size() << " -> " << raw.size() << " bytes\n";
        const std::vector<std::uint8_t> zeros(64u << 20, 0);              // 64 MiB of zeros packs to about 64 KiB
        const std::vector<std::uint8_t> zip_bomb = stb::zlib_compress(zeros, 9);
        try {
            stb::zlib_decompress(zip_bomb, 1u << 20);                     // says no after 1 MiB
        } catch (const stb::limit_error& e) {
            std::cout << "zip bomb of " << zip_bomb.size() << " bytes: limit_error (" << e.what() << ")\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
