#include "stb/image.hpp"

#include <climits>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "file.hpp"
#include "stb/encoder.hpp"

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

image::image(int width, int height, int channels)
    : width_(width), height_(height), channels_(channels), pixels_(checked_bytes(width, height, channels)) {}

image::image(int width, int height, int channels, std::vector<std::uint8_t> pixels)
    : width_(width), height_(height), channels_(channels), pixels_(std::move(pixels)) {
    if (pixels_.size() != checked_bytes(width, height, channels))
        throw std::invalid_argument("pixel buffer size does not match width * height * channels");
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

    // ponytail: one memcpy into a vector so the image owns plain C++ memory
    // and stb never leaks out. Zero-copy (custom deleter) is the upgrade path
    // if profiling ever shows this copy matters.
    const std::size_t n = std::size_t(w) * std::size_t(h) * std::size_t(want);
    return image(w, h, want, std::vector<std::uint8_t>(px.get(), px.get() + n));
}

image image::open(const std::filesystem::path& path, const load_options& opt) {
    const auto bytes = detail::read_file(path);
    return decode(bytes.data(), bytes.size(), opt);
}

std::vector<std::uint8_t> image::encode(const encoder& enc) const { return enc.encode(*this); }

void image::save(const std::filesystem::path& path, const encoder& enc) const {
    detail::write_file(path, encode(enc));  // encode first: a failure leaves no half-written file
}

void image::save(const std::filesystem::path& path) const { save(path, *encoder::for_path(path)); }

}  // namespace stb
