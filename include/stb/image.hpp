#pragma once

// stb headers are never included here: they are compiled into src/core/*.cpp
// only, with STB_*_STATIC, so no stbi_* symbol leaks out of libstb_core (safe
// to link next to your own copy of stb).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "stb/error.hpp"

namespace stb {

class resizer;
enum class resize_filter;  // opaque declaration; defined in resizer.hpp

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

// Defaults of to_png / write_png and to_jpg / write_jpg.
inline constexpr int default_png_compression = 8;  // 1..9, higher = smaller and slower
inline constexpr int default_jpg_quality = 90;     // 1..100

// An 8-bit image: row-major, interleaved channels, no padding, so
// size_bytes() == width * height * channels. A default-constructed image is
// empty.
//
// Move-only: an image never copies its pixels behind your back. Say so with
// copy(). The pixels may be memory the image owns (constructors, decode) or
// memory someone else owns (wrap), which is how a numpy array becomes an
// image without a copy.
//
// Thread-safety: like std::vector - const members are safe to call
// concurrently, mutation needs external synchronisation. The static
// factories are safe to call from any number of threads.
class image {
public:
    image() = default;

    // Zero-filled. Throws invalid_argument unless width, height >= 1 and
    // channels in 1..4.
    image(int width, int height, int channels);
    // Takes ownership of `pixels`; throws invalid_argument on a size mismatch.
    image(int width, int height, int channels, std::vector<std::uint8_t> pixels);

    // Views memory that someone else owns, without copying it. `data` must
    // hold width * height * channels bytes and stay valid while the image
    // (or anything moved from it) lives; `keep_alive` is released when that
    // image dies, so put whatever owns or frees the memory in it (a deleter,
    // a Python reference, ...). Throws invalid_argument on a null `data` or
    // bad dimensions.
    static image wrap(int width, int height, int channels, std::uint8_t* data,
                      std::shared_ptr<void> keep_alive = {});

    image(image&& other) noexcept;
    image& operator=(image&& other) noexcept;
    image(const image&) = delete;             // copies are explicit:
    image& operator=(const image&) = delete;  // see copy()
    ~image() = default;

    // A deep copy that owns its own pixels (also when this image is a wrap).
    image copy() const;

    // --- decoding (throws invalid_argument, decode_error, limit_error) ---
    static image decode(const void* data, std::size_t size, const load_options& opt = {});
    // Also throws io_error.
    static image open(const std::filesystem::path& path, const load_options& opt = {});

    // --- encoding ---
    // to_*: the bytes of a whole file in that format. Throw invalid_argument
    // (empty image, or an argument out of range), encode_error, limit_error.
    // Each format takes its own settings, all defaulted.
    std::vector<std::uint8_t> to_png(int compression = default_png_compression) const;  // 1..9
    std::vector<std::uint8_t> to_jpg(int quality = default_jpg_quality) const;          // 1..100; alpha is dropped
    std::vector<std::uint8_t> to_bmp() const;
    std::vector<std::uint8_t> to_tga(bool rle = true) const;

    // write_*: the matching to_* plus writing the file (also throws io_error).
    // The file is written only after encoding succeeded.
    void write_png(const std::filesystem::path& path, int compression = default_png_compression) const;
    void write_jpg(const std::filesystem::path& path, int quality = default_jpg_quality) const;
    void write_bmp(const std::filesystem::path& path) const;
    void write_tga(const std::filesystem::path& path, bool rle = true) const;

    // The format comes from the extension (.png .jpg .jpeg .bmp .tga,
    // case-insensitive; invalid_argument for anything else), with default
    // settings. For other settings call write_* (or to_*) directly.
    void write(const std::filesystem::path& path) const;

    // --- resizing (implemented in resizer.cpp; include stb/resizer.hpp to pass a resizer) ---
    // A new image of the given size; this one is not modified. `r` == nullptr
    // means a default-constructed resizer. Throws invalid_argument (empty
    // image, width/height < 1), limit_error, error - see resizer::resize.
    image resize(int width, int height, const resizer* r = nullptr) const;
    // Default options with just this filter, by enum or by name
    // ("cubic", "linear", "nearest", ... see resize_filter_from_name).
    image resize(int width, int height, resize_filter filter) const;
    image resize(int width, int height, std::string_view filter_name) const;

    // --- accessors ---
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    int channels() const noexcept { return channels_; }
    bool empty() const noexcept { return !data_; }
    std::size_t stride() const noexcept { return std::size_t(width_) * std::size_t(channels_); }
    std::size_t size_bytes() const noexcept { return stride() * std::size_t(height_); }

    std::uint8_t* data() noexcept { return data_.get(); }
    const std::uint8_t* data() const noexcept { return data_.get(); }
    // All bytes, for range-for and <algorithm> (empty range for an empty image).
    std::uint8_t* begin() noexcept { return data(); }
    std::uint8_t* end() noexcept { return data() + size_bytes(); }
    const std::uint8_t* begin() const noexcept { return data(); }
    const std::uint8_t* end() const noexcept { return data() + size_bytes(); }

    // Unchecked element access (x < width, y < height, c < channels).
    std::uint8_t& operator()(int x, int y, int c = 0) noexcept { return data_.get()[index(x, y, c)]; }
    const std::uint8_t& operator()(int x, int y, int c = 0) const noexcept {
        return data_.get()[index(x, y, c)];
    }

private:
    std::size_t index(int x, int y, int c) const noexcept {
        return std::size_t(y) * stride() + std::size_t(x) * std::size_t(channels_) + std::size_t(c);
    }

    int width_ = 0;
    int height_ = 0;
    int channels_ = 0;
    // Owner + pointer in one: the deleter (or aliased owner) is what frees, or
    // keeps alive, the memory. Null for an empty image.
    std::shared_ptr<std::uint8_t> data_;
};

}  // namespace stb
