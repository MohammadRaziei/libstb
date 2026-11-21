#include "stb/image.hpp"
#include "stb/util.hpp"

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
#include "util/log.hpp"

namespace stb {
namespace {

void check_input(const void* data, std::size_t size) {
    if (size == 0) throw decode_error("empty input");
    if (!data) throw std::invalid_argument("image data is null");
    if (size > static_cast<std::size_t>(INT_MAX)) throw limit_error("input larger than 2 GiB");
}

codec::raster view_of(const image& img) {
    return codec::raster{img.data(), img.width(), img.height(), img.channels()};
}

// Runs an encoder; running out of memory is an encode_error, like any other encoding failure.
template <class Encode>
std::vector<std::uint8_t> encode(const char* format, const image& img, Encode&& run) {
    try {
        std::vector<std::uint8_t> out = run();
        detail::loggers::encode().debug("encoded %s: %dx%d, %d channel(s) -> %zu bytes", format, img.width(),
                                        img.height(), img.channels(), out.size());
        return out;
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

namespace {

// The header facts without the orientation: the decode path needs only these (and scans the EXIF block only
// when it was asked to apply it).
image_info header_of(const void* data, std::size_t size) {
    check_input(data, size);
    const codec::image_header h = codec::read_header(static_cast<const std::uint8_t*>(data), size);
    return image_info{h.width, h.height, pixel_format_from_channels(h.channels), 1};
}

}  // namespace

image_info image_info::read_bytes(const void* data, std::size_t size) {
    image_info info = header_of(data, size);
    info.orientation = detail::exif_orientation(data, size);
    return info;
}

image_info image_info::read_file(const std::filesystem::path& path) {
    const auto bytes = detail::read_file(path);
    return read_bytes(bytes.data(), bytes.size());
}

// --------------------------------------------------------------------- image

pixel_format pixel_format_from_name(std::string_view name) {
    for (const pixel_format f : {pixel_format::gray, pixel_format::gray_alpha, pixel_format::rgb, pixel_format::rgba})
        if (name == pixel_format_name(f)) return f;
    throw std::invalid_argument("unknown pixel format \"" + std::string(name) +
                                "\": use gray, gray_alpha, rgb or rgba");
}

namespace {

// The decode of image::decode and loader::run.
image decode_with(const void* data, std::size_t size, std::optional<pixel_format> format, bool flip, bool exif_orient, std::size_t max_bytes) {
    // Size check from the header alone: no pixel memory is allocated for a
    // hostile "60000x60000" file. width/height are <= 1<<16 and channels <= 4,
    // so the uint64 product cannot overflow.
    const image_info hdr = header_of(data, size);
    const int want = format ? channel_count(*format) : hdr.channels();
    const std::uint64_t bytes = std::uint64_t(hdr.width) * std::uint64_t(hdr.height) * std::uint64_t(want);
    if (bytes > max_bytes)
        throw limit_error(sformat("image too large: %dx%dx%d exceeds max_bytes", hdr.width, hdr.height, want));

    // With exif_orient, a flip is done after the orientation (a vertical flip commutes with
    // nothing else): stb flips only when no orientation has to be applied.
    const int orientation = exif_orient ? detail::exif_orientation(data, size) : 1;
    const bool stb_flips = flip && orientation == 1;

    codec::decoded d = codec::decode(static_cast<const std::uint8_t*>(data), size, format ? channel_count(*format) : 0, stb_flips);

    // No copy: the image takes the decoder's buffer as is. The shared_ptr owns it from here on, so
    // it is freed on any later throw too.
    if (orientation == 1)
        detail::loggers::decode().debug("decoded %dx%d, %d channel(s), from %zu bytes", d.width, d.height, d.channels, size);
    else
        detail::loggers::decode().debug("decoded %dx%d, %d channel(s), from %zu bytes (EXIF orientation %d applied)", d.width,
                                        d.height, d.channels, size, orientation);
    std::uint8_t* raw = d.pixels.release();
    image img = image::wrap(d.width, d.height, pixel_format_from_channels(d.channels), raw, std::shared_ptr<void>(raw, codec::free_deleter{}));
    if (orientation == 1) return img;
    img = img.orient(orientation);
    return flip ? img.flip_vertical() : std::move(img);
}

}  // namespace

image image::decode(const void* data, std::size_t size) { return decode_with(data, size, std::nullopt, false, false, default_max_decode_bytes); }

image image::open(const std::filesystem::path& path) {
    const auto bytes = detail::read_file(path);
    return decode(bytes.data(), bytes.size());
}

loader::loader(const void* data, std::size_t size) : data_(data), size_(size) {}

loader::loader(std::filesystem::path path) : path_(std::move(path)), from_file_(true) {}

loader& loader::format(pixel_format f) & {
    (void)pixel_format_from_channels(channel_count(f));  // throws for an enum value that is not one of the four formats
    format_ = f;
    return *this;
}

loader& loader::flip(bool on) & {
    flip_ = on;
    return *this;
}

loader& loader::exif_orient(bool on) & {
    exif_orient_ = on;
    return *this;
}

loader& loader::max_bytes(std::size_t bytes) & {
    max_bytes_ = bytes;
    return *this;
}

image loader::run() const {
    if (from_file_) {
        const auto bytes = detail::read_file(path_);
        return decode_with(bytes.data(), bytes.size(), format_, flip_, exif_orient_, max_bytes_);
    }
    return decode_with(data_, size_, format_, flip_, exif_orient_, max_bytes_);
}

// ------------------------------------------------------------------ to_*

std::vector<std::uint8_t> image::to_png(int compression) const {
    if (compression < 1 || compression > 9) throw std::invalid_argument("png compression must be in 1..9");
    check_encodable(*this);
    return encode("png", *this, [&] { return codec::encode_png(view_of(*this), compression); });
}

std::vector<std::uint8_t> image::to_jpg(int quality) const {
    if (quality < 1 || quality > 100) throw std::invalid_argument("jpg quality must be in 1..100");
    check_encodable(*this);
    return encode("jpg", *this, [&] { return codec::encode_jpeg(view_of(*this), quality); });
}

std::vector<std::uint8_t> image::to_bmp() const {
    check_encodable(*this);
    return encode("bmp", *this, [&] { return codec::encode_bmp(view_of(*this)); });
}

std::vector<std::uint8_t> image::to_tga(bool rle) const {
    check_encodable(*this);
    return encode("tga", *this, [&] { return codec::encode_tga(view_of(*this), rle); });
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
