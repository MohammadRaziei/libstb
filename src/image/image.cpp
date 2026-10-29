#include "stb/image.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

#include "util/file.hpp"
#include "codec/codec.hpp"

namespace stb {
namespace {

void check_input(const void* data, std::size_t size) {
    if (size == 0) throw decode_error("empty input");
    if (!data) throw std::invalid_argument("image data is null");
    if (size > static_cast<std::size_t>(INT_MAX)) throw limit_error("input larger than 2 GiB");
}

std::size_t checked_bytes(int width, int height, int channels) {
    if (width < 1 || height < 1) throw std::invalid_argument("width and height must be >= 1");
    if (channels < 1 || channels > 4) throw std::invalid_argument("channels must be in 1..4");
    const std::uint64_t pixels = std::uint64_t(width) * std::uint64_t(height);  // <= 2^62
    if (pixels > std::uint64_t(std::numeric_limits<std::size_t>::max()) / std::uint64_t(channels))
        throw std::invalid_argument("image too large");
    return static_cast<std::size_t>(pixels * std::uint64_t(channels));
}

codec::raster view_of(const image& img) {
    return codec::raster{img.data(), img.width(), img.height(), img.channels()};
}

// Runs an encoder; running out of memory is an encode_error, like any other encoding failure.
template <class Encode>
std::vector<std::uint8_t> encode(const char* format, Encode&& run) {
    try {
        return run();
    } catch (const std::bad_alloc&) {
        throw encode_error(std::string(format) + " encoding failed");
    }
}

// The checks common to every format.
void check_encodable(const image& img) {
    if (img.empty()) throw std::invalid_argument("cannot encode an empty image");
    // The encoders size everything with int.
    if (img.size_bytes() > static_cast<std::size_t>(INT_MAX))
        throw limit_error("image larger than 2 GiB cannot be encoded");
}

}  // namespace

// ---------------------------------------------------------------- image_info

image_info image_info::read(const void* data, std::size_t size) {
    check_input(data, size);
    const codec::image_header h = codec::read_header(static_cast<const std::uint8_t*>(data), size);
    return image_info{h.width, h.height, h.channels};
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
    img.data_ = std::shared_ptr<std::uint8_t>(keep_alive, data);  // aliasing: keeps `keep_alive` (C++17 has no moving form)
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

    // With orient, a flip is done after the orientation (a vertical flip commutes with
    // nothing else): stb flips only when no orientation has to be applied.
    const int orientation = opt.orient ? exif_orientation(data, size) : 1;
    const bool stb_flips = opt.flip && orientation == 1;

    codec::decoded d = codec::decode(static_cast<const std::uint8_t*>(data), size, opt.channels, stb_flips);

    // No copy: the image takes the decoder's buffer as is. The shared_ptr owns it from here on, so
    // it is freed on any later throw too.
    std::uint8_t* raw = d.pixels.release();
    image img = image::wrap(d.width, d.height, d.channels, raw, std::shared_ptr<void>(raw, codec::free_deleter{}));
    if (orientation == 1) return img;
    img = img.orient(orientation);
    return opt.flip ? img.flip_vertical() : std::move(img);
}

image image::open(const std::filesystem::path& path, const load_options& opt) {
    const auto bytes = detail::read_file(path);
    return decode(bytes.data(), bytes.size(), opt);
}

// ------------------------------------------------------------------ to_*

std::vector<std::uint8_t> image::to_png(int compression) const {
    if (compression < 1 || compression > 9) throw std::invalid_argument("png compression must be in 1..9");
    check_encodable(*this);
    return encode("png", [&] { return codec::encode_png(view_of(*this), compression); });
}

std::vector<std::uint8_t> image::to_jpg(int quality) const {
    if (quality < 1 || quality > 100) throw std::invalid_argument("jpg quality must be in 1..100");
    check_encodable(*this);
    return encode("jpg", [&] { return codec::encode_jpeg(view_of(*this), quality); });
}

std::vector<std::uint8_t> image::to_bmp() const {
    check_encodable(*this);
    return encode("bmp", [&] { return codec::encode_bmp(view_of(*this)); });
}

std::vector<std::uint8_t> image::to_tga(bool rle) const {
    check_encodable(*this);
    return encode("tga", [&] { return codec::encode_tga(view_of(*this), rle); });
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
