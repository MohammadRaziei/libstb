// The PNG encoder: per-row filter choice, then one zlib stream in a single IDAT chunk.
// Derived from stb_image_write.h v1.16 (public domain / MIT, Sean Barrett and contributors).

#include <array>
#include <cstdlib>
#include <cstring>

#include "codec/codec.hpp"

namespace stb::codec {
namespace {

constexpr std::uint32_t crc32_polynomial = 0xEDB88320u;

// CRC-32 of the chunk type and data, as PNG stores it.
class crc32 {
public:
    static std::uint32_t of(const std::uint8_t* p, std::size_t n) {
        static const table_t t = make_table();
        std::uint32_t crc = ~0u;
        for (std::size_t i = 0; i < n; ++i) crc = (crc >> 8) ^ t[(crc ^ p[i]) & 0xff];
        return ~crc;
    }

private:
    using table_t = std::array<std::uint32_t, 256>;

    static table_t make_table() {
        table_t t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ crc32_polynomial : c >> 1;
            t[i] = c;
        }
        return t;
    }
};

std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return std::uint8_t(a);
    if (pb <= pc) return std::uint8_t(b);
    return std::uint8_t(c);
}

// The five PNG row filters. The first row has no row above it, so its Up, Average and Paeth
// filters are computed with "above = 0" (the variants first_row_*).
enum class filter : int { none = 0, left = 1, up = 2, average = 3, paeth = 4 };
constexpr int filter_count = 5;

class row_filter {
public:
    row_filter(const std::uint8_t* pixels, int width, int channels, std::size_t stride)
        : pixels_(pixels), row_bytes_(std::size_t(width) * std::size_t(channels)), channels_(std::size_t(channels)),
          stride_(stride) {}

    // Filters row `y` into `out` (row_bytes bytes).
    void apply(filter f, int y, signed char* out) const {
        const std::uint8_t* z = pixels_ + stride_ * std::size_t(y);
        const std::uint8_t* above = y != 0 ? z - stride_ : nullptr;
        const std::size_t n = channels_, end = row_bytes_;
        auto up = [&](std::size_t i) { return above ? int(above[i]) : 0; };
        auto put = [&](std::size_t i, int v) { out[i] = static_cast<signed char>(v); };

        if (f == filter::none || (f == filter::up && !above)) {
            std::memcpy(out, z, end);
            return;
        }
        // the first pixel has no left neighbour
        for (std::size_t i = 0; i < n; ++i) {
            switch (f) {
                case filter::left: put(i, z[i]); break;
                case filter::up: put(i, z[i] - up(i)); break;
                case filter::average: put(i, above ? z[i] - (above[i] >> 1) : z[i]); break;
                case filter::paeth: put(i, z[i] - paeth(0, up(i), 0)); break;
                case filter::none: break;
            }
        }
        for (std::size_t i = n; i < end; ++i) {
            switch (f) {
                case filter::left: put(i, z[i] - z[i - n]); break;
                case filter::up: put(i, z[i] - above[i]); break;
                case filter::average: put(i, z[i] - ((z[i - n] + up(i)) >> 1)); break;
                case filter::paeth:
                    put(i, above ? z[i] - paeth(z[i - n], above[i], above[i - n]) : z[i] - paeth(z[i - n], 0, 0));
                    break;
                case filter::none: break;
            }
        }
    }

private:
    const std::uint8_t* pixels_;
    std::size_t row_bytes_, channels_, stride_;
};

// Appends one chunk: length, type, data, CRC of type + data.
void write_chunk(bytes& out, const char (&type)[5], const std::uint8_t* data, std::size_t size) {
    byte_writer w(out);
    w.u32_be(std::uint32_t(size));
    const std::size_t crc_from = out.size();
    w.raw(type, 4);
    w.raw(data, size);
    w.u32_be(crc32::of(out.data() + crc_from, out.size() - crc_from));
}

}  // namespace

bytes encode_png(const raster& image, int compression) {
    const int w = image.width, h = image.height, n = image.channels;
    const std::size_t row = image.row_bytes();
    static const int color_type[5] = {-1, 0, 4, 2, 6};  // gray, gray + alpha, RGB, RGBA

    // Every row with its filter byte in front, each row filtered the way that leaves the
    // smallest sum of absolute values (the usual estimate of what compresses best).
    const row_filter filters(image.pixels, w, n, row);
    bytes filtered((row + 1) * std::size_t(h));
    std::vector<signed char> best_line(row), candidate(row);
    for (int y = 0; y < h; ++y) {
        int best = 0, best_cost = 0x7fffffff;
        for (int f = 0; f < filter_count; ++f) {
            filters.apply(filter(f), y, candidate.data());
            int cost = 0;
            for (std::size_t i = 0; i < row; ++i) cost += std::abs(int(candidate[i]));
            if (cost < best_cost) {
                best_cost = cost;
                best = f;
                best_line.swap(candidate);  // keep the best row, no second pass to recompute it
            }
        }
        std::uint8_t* dst = filtered.data() + std::size_t(y) * (row + 1);
        dst[0] = std::uint8_t(best);
        std::memcpy(dst + 1, best_line.data(), row);
    }
    const bytes zlib = zlib_deflate(filtered.data(), filtered.size(), compression);

    bytes out;
    out.reserve(8 + 12 + 13 + 12 + zlib.size() + 12);
    byte_writer wr(out);
    static const std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    wr.raw(signature);

    bytes ihdr;
    byte_writer hw(ihdr);
    hw.u32_be(std::uint32_t(w));
    hw.u32_be(std::uint32_t(h));
    hw.u8(8);  // bit depth
    hw.u8(unsigned(color_type[n]));
    hw.u8(0);  // deflate
    hw.u8(0);  // adaptive filtering
    hw.u8(0);  // no interlace
    write_chunk(out, "IHDR", ihdr.data(), ihdr.size());
    write_chunk(out, "IDAT", zlib.data(), zlib.size());
    write_chunk(out, "IEND", nullptr, 0);
    return out;
}

}  // namespace stb::codec
