#include "stb/image.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "file.hpp"

// STATIC: every stbi_* function gets internal linkage in this TU.
// NO_STDIO: we only decode from memory (files are read by detail::read_file),
// which removes fopen/FILE code from the attack surface and Windows path
// quirks. MAX_DIMENSIONS: hard cap on width/height; stb_image has been fuzzed
// into many bugs, so reject absurd headers before any decoding work happens.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS (1 << 16)
#include "stb_image.h"

// Same rules for the write side. NO_STDIO: we only encode to memory; files are
// written by detail::write_file.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

namespace stb {
namespace {

void check_input(const void* data, std::size_t size) {
    if (size == 0) throw decode_error("empty input");
    if (!data) throw std::invalid_argument("image data is null");
    if (size > static_cast<std::size_t>(INT_MAX)) throw limit_error("input larger than 2 GiB");
}

[[noreturn]] void fail_decode(const char* what) {
    const char* why = stbi_failure_reason();
    throw decode_error(std::string(what) + ": " + (why ? why : "unknown error"));
}

struct stb_free {
    void operator()(stbi_uc* p) const { stbi_image_free(p); }
};

std::size_t checked_bytes(int width, int height, int channels) {
    if (width < 1 || height < 1) throw std::invalid_argument("width and height must be >= 1");
    if (channels < 1 || channels > 4) throw std::invalid_argument("channels must be in 1..4");
    const std::uint64_t pixels = std::uint64_t(width) * std::uint64_t(height);  // <= 2^62
    if (pixels > std::uint64_t(std::numeric_limits<std::size_t>::max()) / std::uint64_t(channels))
        throw std::invalid_argument("image too large");
    return static_cast<std::size_t>(pixels * std::uint64_t(channels));
}

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

// ---------------------------------------------------------------- image_info

image_info image_info::read(const void* data, std::size_t size) {
    check_input(data, size);
    image_info i;
    if (!stbi_info_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &i.width,
                               &i.height, &i.channels))
        fail_decode("cannot read image header");
    return i;
}

image_info image_info::read_file(const std::filesystem::path& path) {
    const auto bytes = detail::read_file(path);
    return read(bytes.data(), bytes.size());
}

// --------------------------------------------------------------------- image

image::image(int width, int height, int channels) : width_(width), height_(height), channels_(channels) {
    const std::size_t n = checked_bytes(width, height, channels);
    data_ = std::shared_ptr<std::uint8_t>(new std::uint8_t[n](), std::default_delete<std::uint8_t[]>());
}

image::image(int width, int height, int channels, std::vector<std::uint8_t> pixels)
    : width_(width), height_(height), channels_(channels) {
    if (pixels.size() != checked_bytes(width, height, channels))
        throw std::invalid_argument("pixel buffer size does not match width * height * channels");
    auto holder = std::make_shared<std::vector<std::uint8_t>>(std::move(pixels));
    data_ = std::shared_ptr<std::uint8_t>(holder, holder->data());  // one block: vector + pointer
}

image image::wrap(int width, int height, int channels, std::uint8_t* data, std::shared_ptr<void> keep_alive) {
    checked_bytes(width, height, channels);  // validates the dimensions
    if (!data) throw std::invalid_argument("wrap needs a non-null pointer");
    image img;
    img.width_ = width;
    img.height_ = height;
    img.channels_ = channels;
    img.data_ = std::shared_ptr<std::uint8_t>(std::move(keep_alive), data);  // aliasing: keeps `keep_alive`
    return img;
}

// A moved-from image is empty (not just "valid but unspecified").
image::image(image&& o) noexcept
    : width_(std::exchange(o.width_, 0)),
      height_(std::exchange(o.height_, 0)),
      channels_(std::exchange(o.channels_, 0)),
      data_(std::move(o.data_)) {}

image& image::operator=(image&& o) noexcept {
    if (this != &o) {
        width_ = std::exchange(o.width_, 0);
        height_ = std::exchange(o.height_, 0);
        channels_ = std::exchange(o.channels_, 0);
        data_ = std::move(o.data_);
    }
    return *this;
}

image image::copy() const {
    if (empty()) return image();
    return image(width_, height_, channels_, std::vector<std::uint8_t>(begin(), end()));
}

image image::decode(const void* data, std::size_t size, const load_options& opt) {
    if (opt.channels < 0 || opt.channels > 4) throw std::invalid_argument("channels must be in 0..4");

    // Size check from the header alone: no pixel memory is allocated for a
    // hostile "60000x60000" file. width/height are <= 1<<16 and channels <= 4,
    // so the uint64 product cannot overflow.
    const image_info hdr = image_info::read(data, size);
    const int want = opt.channels ? opt.channels : hdr.channels;
    const std::uint64_t bytes = std::uint64_t(hdr.width) * std::uint64_t(hdr.height) * std::uint64_t(want);
    if (bytes > opt.max_bytes)
        throw limit_error("image too large: " + std::to_string(hdr.width) + "x" +
                          std::to_string(hdr.height) + "x" + std::to_string(want) +
                          " exceeds max_bytes");

    // The _thread variant: stb's plain setter is a process-wide global.
    stbi_set_flip_vertically_on_load_thread(opt.flip ? 1 : 0);

    int w = 0, h = 0, c = 0;
    std::unique_ptr<stbi_uc, stb_free> px(stbi_load_from_memory(
        static_cast<const stbi_uc*>(data), static_cast<int>(size), &w, &h, &c, opt.channels));
    if (!px) fail_decode("cannot decode image");

    // No copy: the image takes stb's buffer as is and frees it with stbi_image_free.
    // The shared_ptr owns the buffer from here on, so it is freed on any later throw too.
    stbi_uc* raw = px.get();
    std::shared_ptr<void> owner(px.release(), [](void* p) { stbi_image_free(p); });
    return image::wrap(w, h, want, raw, std::move(owner));
}

image image::open(const std::filesystem::path& path, const load_options& opt) {
    const auto bytes = detail::read_file(path);
    return decode(bytes.data(), bytes.size(), opt);
}

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

// ------------------------------------------------------------------- write

void image::write(const std::filesystem::path& path) const {
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
