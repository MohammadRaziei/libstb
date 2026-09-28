#pragma once

// stb headers are never included here: they are compiled into src/core/*.cpp
// only, with STB_*_STATIC, so no stbi_* symbol leaks out of libstb_core (safe
// to link next to your own copy of stb).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "libstb/error.hpp"

namespace libstb {

class encoder;

// Header-only facts about an encoded image (no pixel decoding).
struct image_info {
    int width = 0;
    int height = 0;
    int channels = 0;  // channels in the source file

    // Throws: invalid_argument (null data), decode_error, limit_error.
    static image_info read(const void* data, std::size_t size);
    // Also throws io_error.
    static image_info read_file(const std::filesystem::path& path);
};

struct load_options {
    int channels = 0;   // 0 = keep the source's, 1..4 = convert to that many
    bool flip = false;  // flip vertically while decoding (per call, thread-safe)
    // Refuse to decode anything whose output would exceed this many bytes.
    // Checked from the header BEFORE any pixel memory is allocated.
    std::size_t max_bytes = std::size_t(1) << 29;  // 512 MiB
};

// An 8-bit image: row-major, interleaved channels, no padding, so
// size_bytes() == width * height * channels. A value type: copy, move and
// destroy as usual (rule of zero). A default-constructed image is empty.
//
// Thread-safety: like std::vector - const members are safe to call
// concurrently, mutation needs external synchronisation. The static
// factories and encoders are safe to call from any number of threads.
class image {
public:
    image() = default;

    // Zero-filled. Throws invalid_argument unless width, height >= 1 and
    // channels in 1..4.
    image(int width, int height, int channels);
    // Takes ownership of `pixels`; throws invalid_argument on a size mismatch.
    image(int width, int height, int channels, std::vector<std::uint8_t> pixels);

    // --- decoding (throws invalid_argument, decode_error, limit_error) ---
    static image decode(const void* data, std::size_t size, const load_options& opt = {});
    // Also throws io_error.
    static image open(const std::filesystem::path& path, const load_options& opt = {});

    // --- encoding ---
    // Throws invalid_argument (empty image), encode_error, limit_error.
    std::vector<std::uint8_t> encode(const encoder& enc) const;
    // Also throws io_error. The file is written only after encoding succeeded.
    void save(const std::filesystem::path& path, const encoder& enc) const;
    // Encoder chosen from the extension: .png .jpg/.jpeg .bmp .tga
    // (invalid_argument for anything else).
    void save(const std::filesystem::path& path) const;

    // --- accessors ---
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    int channels() const noexcept { return channels_; }
    bool empty() const noexcept { return pixels_.empty(); }
    std::size_t stride() const noexcept { return std::size_t(width_) * std::size_t(channels_); }
    std::size_t size_bytes() const noexcept { return pixels_.size(); }

    std::uint8_t* data() noexcept { return pixels_.data(); }
    const std::uint8_t* data() const noexcept { return pixels_.data(); }
    const std::vector<std::uint8_t>& pixels() const noexcept { return pixels_; }

    // Unchecked element access (x < width, y < height, c < channels).
    std::uint8_t& operator()(int x, int y, int c = 0) noexcept { return pixels_[index(x, y, c)]; }
    const std::uint8_t& operator()(int x, int y, int c = 0) const noexcept {
        return pixels_[index(x, y, c)];
    }

private:
    std::size_t index(int x, int y, int c) const noexcept {
        return std::size_t(y) * stride() + std::size_t(x) * std::size_t(channels_) + std::size_t(c);
    }

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    std::vector<std::uint8_t> pixels_;
};

}  // namespace libstb
