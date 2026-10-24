// The two simple formats: BMP (uncompressed) and TGA (uncompressed or run-length encoded).
// Derived from stb_image_write.h v1.16 (public domain / MIT, Sean Barrett and contributors).
//
// Headers go through byte_writer; the pixels are written straight into a buffer sized up front
// (these formats are mostly a copy, so a bounds-checked push per byte would dominate).

#include <cstring>

#include "codec/codec.hpp"

namespace stb::codec {
namespace {

// Writes `count` pixels in the order BMP and TGA store them: blue, green, red (or the single gray
// value), with the alpha byte after it when `with_alpha`. `gray_as_rgb` repeats a gray value three
// times (BMP has no gray format; TGA does). Returns where the next byte goes.
std::uint8_t* write_pixels(std::uint8_t* d, const std::uint8_t* s, int count, int channels, bool with_alpha,
                           bool gray_as_rgb) {
    const std::size_t n = std::size_t(count);
    switch (channels) {
        case 1:
            if (gray_as_rgb) {
                for (std::size_t i = 0; i < n; ++i, d += 3) d[0] = d[1] = d[2] = s[i];
            } else {
                std::memcpy(d, s, n);
                d += n;
            }
            break;
        case 2:
            for (std::size_t i = 0; i < n; ++i, s += 2) {
                if (gray_as_rgb) {
                    d[0] = d[1] = d[2] = s[0];
                    d += 3;
                } else {
                    *d++ = s[0];
                }
                if (with_alpha) *d++ = s[1];
            }
            break;
        case 3:
            for (std::size_t i = 0; i < n; ++i, s += 3, d += 3) d[0] = s[2], d[1] = s[1], d[2] = s[0];
            break;
        default:  // 4
            for (std::size_t i = 0; i < n; ++i, s += 4) {
                d[0] = s[2], d[1] = s[1], d[2] = s[0];
                d += 3;
                if (with_alpha) *d++ = s[3];
            }
            break;
    }
    return d;
}

// Row y (0 = top) of a raster.
const std::uint8_t* row_of(const raster& r, int y) { return r.pixels + std::size_t(y) * r.row_bytes(); }

}  // namespace

bytes encode_bmp(const raster& r) {
    const int w = r.width, h = r.height, n = r.channels;
    const bool rgba = n == 4;
    // RGBA needs a v4 header: BI_BITFIELDS with 32 bits and an alpha mask (a plain BI_RGB with
    // an alpha mask does not work in most readers). Everything else is a 24-bit bitmap whose
    // rows are padded to a multiple of 4 bytes.
    const std::size_t pad = rgba ? 0 : std::size_t((-w * 3) & 3);
    const std::uint32_t info_size = rgba ? 108 : 40;
    const std::size_t row_size = std::size_t(w) * (rgba ? 4 : 3) + pad;
    const std::size_t pixel_bytes = row_size * std::size_t(h);

    bytes out;
    byte_writer wr(out);
    wr.u8('B'), wr.u8('M');
    wr.u32_le(std::uint32_t(14 + info_size + pixel_bytes));
    wr.u16_le(0), wr.u16_le(0);
    wr.u32_le(14 + info_size);
    wr.u32_le(info_size);
    wr.u32_le(std::uint32_t(w)), wr.u32_le(std::uint32_t(h));
    wr.u16_le(1), wr.u16_le(rgba ? 32 : 24);  // planes, bits per pixel
    wr.u32_le(rgba ? 3 : 0);                  // compression: BI_BITFIELDS or BI_RGB
    for (int i = 0; i < 5; ++i) wr.u32_le(0);  // image size, resolution x / y, palette sizes
    if (rgba) {
        wr.u32_le(0xff0000), wr.u32_le(0xff00), wr.u32_le(0xff), wr.u32_le(0xff000000u);  // channel masks
        for (int i = 0; i < 13; ++i) wr.u32_le(0);  // colour space type, endpoints, gamma
    }

    const std::size_t header_size = out.size();
    out.resize(header_size + pixel_bytes);  // zero filled, which is the row padding too
    std::uint8_t* d = out.data() + header_size;
    for (int y = h - 1; y >= 0; --y) {  // bottom row first
        d = write_pixels(d, row_of(r, y), w, n, rgba, true);
        d += pad;
    }
    return out;
}

namespace {

// One TGA run-length encoded row: packets of up to 128 pixels, each either raw (a count and that
// many pixels) or a run (a count and one pixel, repeated). Returns where the next byte goes.
std::uint8_t* write_rle_row(std::uint8_t* d, const std::uint8_t* row, int width, int n, bool with_alpha) {
    auto same = [&](const std::uint8_t* a, const std::uint8_t* b) { return std::memcmp(a, b, std::size_t(n)) == 0; };
    for (int i = 0, len = 1; i < width; i += len) {
        const std::uint8_t* begin = row + std::size_t(i) * n;
        bool raw = true;
        len = 1;
        if (i < width - 1) {
            ++len;
            raw = !same(begin, row + std::size_t(i + 1) * n);
            if (raw) {  // pixels differ: extend until two equal ones in a row
                const std::uint8_t* prev = begin;
                for (int k = i + 2; k < width && len < 128; ++k) {
                    if (!same(prev, row + std::size_t(k) * n)) {
                        prev += n;
                        ++len;
                    } else {
                        --len;
                        break;
                    }
                }
            } else {  // pixels equal: extend the run
                for (int k = i + 2; k < width && len < 128; ++k) {
                    if (!same(begin, row + std::size_t(k) * n)) break;
                    ++len;
                }
            }
        }
        if (raw) {
            *d++ = std::uint8_t(len - 1);
            d = write_pixels(d, begin, len, n, with_alpha, false);
        } else {
            *d++ = std::uint8_t(len - 129);
            d = write_pixels(d, begin, 1, n, with_alpha, false);
        }
    }
    return d;
}

}  // namespace

bytes encode_tga(const raster& r, bool rle) {
    const int w = r.width, h = r.height, n = r.channels;
    const bool with_alpha = n == 2 || n == 4;
    const int color_bytes = with_alpha ? n - 1 : n;
    const int image_type = (color_bytes < 2 ? 3 : 2) + (rle ? 8 : 0);  // 3 = gray, 2 = true colour

    bytes out;
    byte_writer wr(out);
    wr.u8(0);  // id length
    wr.u8(0);  // no colour map
    wr.u8(unsigned(image_type));
    wr.u16_le(0), wr.u16_le(0), wr.u8(0);  // colour map: first entry, length, entry size
    wr.u16_le(0), wr.u16_le(0);            // origin x, y
    wr.u16_le(unsigned(w)), wr.u16_le(unsigned(h));
    wr.u8(unsigned(n * 8));        // bits per pixel (colour bytes + alpha = the channel count)
    wr.u8(with_alpha ? 8 : 0);     // alpha bits

    // A pixel takes n bytes; in the worst case every RLE packet holds one pixel and adds a byte.
    const std::size_t header_size = out.size();
    out.resize(header_size + std::size_t(w) * std::size_t(h) * std::size_t(n + (rle ? 1 : 0)));
    std::uint8_t* d = out.data() + header_size;
    for (int y = h - 1; y >= 0; --y) {  // bottom row first
        d = rle ? write_rle_row(d, row_of(r, y), w, n, with_alpha)
                : write_pixels(d, row_of(r, y), w, n, with_alpha, false);
    }
    out.resize(std::size_t(d - out.data()));
    return out;
}

}  // namespace stb::codec
