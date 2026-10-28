#pragma once

// Internal. Every format libstb reads or writes, behind plain functions that know nothing about
// stb::image: the rest of the library hands them bytes and gets bytes back.
//
//   decode.cpp    the entry points: read_header, decode, zlib_inflate, on top of decode/ (one file per
//                 format, the stateful ones as classes: jpeg_decoder, png_decoder, gif_decoder, inflater)
//   deflate.cpp   the zlib compressor (LZ77 + Huffman), shared by PNG and the public zlib API
//   png.cpp, jpeg.cpp, bmp_tga.cpp   the encoders
//
// The encoders and the compressor are libstb's own code (derived from stb_image_write.h, which
// is no longer vendored); the decoders are stb_image.h v2.30 restructured into decode/, same algorithms.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace stb::codec {

using bytes = std::vector<std::uint8_t>;

// Appends integers and raw bytes to a byte vector, in the byte orders the formats use.
class byte_writer {
public:
    explicit byte_writer(bytes& out) : out_(out) {}

    void u8(unsigned v) { out_.push_back(static_cast<std::uint8_t>(v)); }
    void u16_le(unsigned v) { u8(v), u8(v >> 8); }
    void u32_le(std::uint32_t v) { u16_le(v & 0xffff), u16_le(v >> 16); }
    void u32_be(std::uint32_t v) { u8(v >> 24), u8(v >> 16), u8(v >> 8), u8(v); }
    void raw(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        out_.insert(out_.end(), b, b + n);
    }
    template <std::size_t N>
    void raw(const std::uint8_t (&a)[N]) { raw(a, N); }
    void zeros(std::size_t n) { out_.insert(out_.end(), n, std::uint8_t(0)); }

private:
    bytes& out_;
};

// 8-bit interleaved pixels (1 = gray, 2 = gray + alpha, 3 = RGB, 4 = RGBA), row after row with no
// padding. Borrowed: the raster never owns the memory.
struct raster {
    const std::uint8_t* pixels;
    int width;
    int height;
    int channels;

    std::size_t row_bytes() const { return std::size_t(width) * std::size_t(channels); }
};

// ---- encoders. The caller has checked the arguments (non-empty raster, channels 1..4, size
// below 2 GiB, levels in range). They throw std::bad_alloc when memory runs out.

bytes encode_png(const raster& image, int compression);  // compression 1..9, higher = smaller and slower
bytes encode_jpeg(const raster& image, int quality);     // quality 1..100 (4:2:0 up to 90, 4:4:4 above)
bytes encode_bmp(const raster& image);                   // 24-bit BGR, or 32-bit BGRA for 4 channels
bytes encode_tga(const raster& image, bool rle);

// ---- zlib

std::uint32_t adler32(const std::uint8_t* data, std::size_t size);

// Level 0: a zlib stream of stored blocks.
bytes zlib_store(const std::uint8_t* data, std::size_t size);

// A zlib stream compressed with the given effort: 0 (fastest) .. 10 (smallest). PNG uses 1..9;
// the public zlib API maps its level 9 to 10.
bytes zlib_deflate(const std::uint8_t* data, std::size_t size, int effort);

enum class inflate_status { ok, corrupt, too_large, out_of_memory };

struct inflated {
    bytes data;
    inflate_status status = inflate_status::ok;
};

// Inflates one zlib stream. The output buffer starts at `initial` bytes and doubles as needed; it
// is never allowed past `ceiling` bytes (too_large instead). The Adler-32 trailer is NOT checked:
// the caller does.
inflated zlib_inflate(const std::uint8_t* in, std::size_t size, std::size_t initial, std::size_t ceiling);

// ---- decoders

struct image_header {
    int width = 0;
    int height = 0;
    int channels = 0;  // channels in the file
};

// Throws decode_error ("cannot read image header: <why>").
image_header read_header(const std::uint8_t* data, std::size_t size);

struct free_deleter {
    void operator()(void* p) const noexcept;  // std::free
};

struct decoded {
    std::unique_ptr<std::uint8_t, free_deleter> pixels;  // width * height * channels bytes
    int width = 0;
    int height = 0;
    int channels = 0;  // what was asked for, or the file's when the request was 0
};

// Decodes to `want_channels` (0 = the file's own). `flip` flips vertically while decoding, per
// call. Throws decode_error ("cannot decode image: <why>").
decoded decode(const std::uint8_t* data, std::size_t size, int want_channels, bool flip);

}  // namespace stb::codec
