#include "stb/encoder.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <mutex>
#include <stdexcept>
#include <string>

#include "stb/image.hpp"

// STATIC: internal linkage for every stbi_write_* function (see image.cpp).
// NO_STDIO: we only encode to memory; files are written by detail::write_file.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace stb {
namespace {

// stb_image_write configures PNG compression level and TGA RLE through
// plain process-wide globals. Setting them and encoding happens under this
// lock, so concurrent encoders with different settings cannot see each
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

}  // namespace

// ------------------------------------------------------------------- encoder

std::vector<std::uint8_t> encoder::encode(const image& img) const {
    if (img.empty()) throw std::invalid_argument("cannot encode an empty image");
    // stb_image_write sizes everything with int.
    if (img.size_bytes() > static_cast<std::size_t>(INT_MAX))
        throw limit_error("image larger than 2 GiB cannot be encoded");
    return do_encode(img);
}

std::unique_ptr<encoder> encoder::for_path(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".png") return std::make_unique<png_encoder>();
    if (ext == ".jpg" || ext == ".jpeg") return std::make_unique<jpeg_encoder>();
    if (ext == ".bmp") return std::make_unique<bmp_encoder>();
    if (ext == ".tga") return std::make_unique<tga_encoder>();
    throw std::invalid_argument("unsupported image extension '" + ext +
                                "' (supported: .png .jpg .jpeg .bmp .tga)");
}

// ---------------------------------------------------------------------- png

png_encoder::png_encoder(int compression) : compression_(compression) {
    if (compression < 1 || compression > 9)
        throw std::invalid_argument("png compression must be in 1..9");
}

std::vector<std::uint8_t> png_encoder::do_encode(const image& img) const {
    std::lock_guard<std::mutex> lock(g_stb_globals);
    stbi_write_png_compression_level = compression_;
    return run("png", [&](void* ctx) {
        return stbi_write_png_to_func(sink, ctx, img.width(), img.height(), img.channels(), img.data(),
                                      static_cast<int>(img.stride()));
    });
}

// --------------------------------------------------------------------- jpeg

jpeg_encoder::jpeg_encoder(int quality) : quality_(quality) {
    if (quality < 1 || quality > 100) throw std::invalid_argument("jpeg quality must be in 1..100");
}

std::vector<std::uint8_t> jpeg_encoder::do_encode(const image& img) const {
    return run("jpeg", [&](void* ctx) {
        return stbi_write_jpg_to_func(sink, ctx, img.width(), img.height(), img.channels(), img.data(),
                                      quality_);
    });
}

// ---------------------------------------------------------------------- bmp

std::vector<std::uint8_t> bmp_encoder::do_encode(const image& img) const {
    return run("bmp", [&](void* ctx) {
        return stbi_write_bmp_to_func(sink, ctx, img.width(), img.height(), img.channels(), img.data());
    });
}

// ---------------------------------------------------------------------- tga

std::vector<std::uint8_t> tga_encoder::do_encode(const image& img) const {
    std::lock_guard<std::mutex> lock(g_stb_globals);
    stbi_write_tga_with_rle = rle_ ? 1 : 0;
    return run("tga", [&](void* ctx) {
        return stbi_write_tga_to_func(sink, ctx, img.width(), img.height(), img.channels(), img.data());
    });
}

}  // namespace stb
