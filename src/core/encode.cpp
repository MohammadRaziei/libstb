#include <algorithm>
#include <cctype>
#include <climits>
#include <mutex>
#include <stdexcept>
#include <string>

#include "file.hpp"
#include "stb/image.hpp"

// STATIC: internal linkage for every stbi_write_* function (see image.cpp).
// NO_STDIO: we only encode to memory; files are written by detail::write_file.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

// image::to_* / write_* / save live here, not in image.cpp: this is the one
// place that knows stb_image_write, and it keeps that header out of image.cpp.

namespace stb {
namespace {

// stb_image_write configures PNG compression level and TGA RLE through
// plain process-wide globals. Setting them and encoding happens under this
// lock, so concurrent calls with different settings cannot see each
// other's values. (JPEG quality and BMP take no global state: no lock.)
std::mutex g_stb_globals;

void sink(void* ctx, void* data, int size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(ctx);
    const auto* p = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

// `write` gets the sink context and returns stb's 0-on-failure status.
template <class Write>
std::vector<std::uint8_t> run(const char* format, Write&& write) {
    std::vector<std::uint8_t> out;
    if (!write(&out)) throw encode_error(std::string(format) + " encoding failed");
    return out;
}

// The checks common to every format.
void check_encodable(const image& img) {
    if (img.empty()) throw std::invalid_argument("cannot encode an empty image");
    // stb_image_write sizes everything with int.
    if (img.size_bytes() > static_cast<std::size_t>(INT_MAX))
        throw limit_error("image larger than 2 GiB cannot be encoded");
}

}  // namespace

// ------------------------------------------------------------------ to_*

std::vector<std::uint8_t> image::to_png(int compression) const {
    if (compression < 1 || compression > 9) throw std::invalid_argument("png compression must be in 1..9");
    check_encodable(*this);
    std::lock_guard<std::mutex> lock(g_stb_globals);
    stbi_write_png_compression_level = compression;
    return run("png", [&](void* ctx) {
        return stbi_write_png_to_func(sink, ctx, width(), height(), channels(), data(),
                                      static_cast<int>(stride()));
    });
}

std::vector<std::uint8_t> image::to_jpg(int quality) const {
    if (quality < 1 || quality > 100) throw std::invalid_argument("jpg quality must be in 1..100");
    check_encodable(*this);
    return run("jpg", [&](void* ctx) {
        return stbi_write_jpg_to_func(sink, ctx, width(), height(), channels(), data(), quality);
    });
}

std::vector<std::uint8_t> image::to_bmp() const {
    check_encodable(*this);
    return run("bmp", [&](void* ctx) {
        return stbi_write_bmp_to_func(sink, ctx, width(), height(), channels(), data());
    });
}

std::vector<std::uint8_t> image::to_tga(bool rle) const {
    check_encodable(*this);
    std::lock_guard<std::mutex> lock(g_stb_globals);
    stbi_write_tga_with_rle = rle ? 1 : 0;
    return run("tga", [&](void* ctx) {
        return stbi_write_tga_to_func(sink, ctx, width(), height(), channels(), data());
    });
}

// ---------------------------------------------------------------- write_*
// Each is its to_* plus detail::write_file. Encoding happens first, so a
// failure (bad argument, encode_error) never leaves a half-written file.

void image::write_png(const std::filesystem::path& path, int compression) const {
    detail::write_file(path, to_png(compression));
}

void image::write_jpg(const std::filesystem::path& path, int quality) const {
    detail::write_file(path, to_jpg(quality));
}

void image::write_bmp(const std::filesystem::path& path) const { detail::write_file(path, to_bmp()); }

void image::write_tga(const std::filesystem::path& path, bool rle) const {
    detail::write_file(path, to_tga(rle));
}

// ------------------------------------------------------------------- save

void image::save(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".png") return write_png(path);
    if (ext == ".jpg" || ext == ".jpeg") return write_jpg(path);
    if (ext == ".bmp") return write_bmp(path);
    if (ext == ".tga") return write_tga(path);
    throw std::invalid_argument("unsupported image extension '" + ext +
                                "' (supported: .png .jpg .jpeg .bmp .tga)");
}

}  // namespace stb
