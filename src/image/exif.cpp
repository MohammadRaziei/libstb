#include <cstddef>
#include <cstdint>
#include <cstring>

#include "stb/image.hpp"

// Reads only the Orientation tag (0x0112) of the EXIF block in a JPEG (APP1 segment)
// or a PNG (eXIf chunk). Written for untrusted input: every offset is checked against
// the buffer before it is used, nothing is allocated, and any oddity ends in "no
// orientation" (1), never in a read past the end.

namespace stb {
namespace {

struct bytes {
    const std::uint8_t* p;
    std::size_t n;

    bool has(std::size_t off, std::size_t len) const { return off <= n && len <= n - off; }

    bool u16(std::size_t off, bool little, unsigned& out) const {
        if (!has(off, 2)) return false;
        out = little ? unsigned(p[off]) | unsigned(p[off + 1]) << 8
                     : unsigned(p[off]) << 8 | unsigned(p[off + 1]);
        return true;
    }

    bool u32(std::size_t off, bool little, std::uint32_t& out) const {
        if (!has(off, 4)) return false;
        const std::uint32_t a = p[off], b = p[off + 1], c = p[off + 2], d = p[off + 3];
        out = little ? a | b << 8 | c << 16 | d << 24 : a << 24 | b << 16 | c << 8 | d;
        return true;
    }
};

// `t` is a TIFF structure (the EXIF payload). Returns 1..8, or 0 when there is no
// valid orientation in its first IFD.
int tiff_orientation(const bytes& t) {
    if (!t.has(0, 8)) return 0;
    bool little;
    if (t.p[0] == 'I' && t.p[1] == 'I')
        little = true;
    else if (t.p[0] == 'M' && t.p[1] == 'M')
        little = false;
    else
        return 0;

    unsigned magic = 0;
    if (!t.u16(2, little, magic) || magic != 42) return 0;
    std::uint32_t ifd = 0;
    if (!t.u32(4, little, ifd)) return 0;

    unsigned count = 0;
    if (!t.u16(ifd, little, count)) return 0;
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t e = std::size_t(ifd) + 2 + std::size_t(i) * 12;
        if (!t.has(e, 12)) return 0;
        unsigned tag = 0, type = 0, value = 0;
        std::uint32_t n = 0;
        t.u16(e, little, tag);
        if (tag != 0x0112) continue;
        t.u16(e + 2, little, type);
        t.u32(e + 4, little, n);
        if (type != 3 || n != 1) return 0;  // Orientation is one SHORT
        t.u16(e + 8, little, value);        // a lone SHORT sits in the first two value bytes
        return value >= 1 && value <= 8 ? int(value) : 0;
    }
    return 0;
}

int jpeg_orientation(const bytes& d) {
    if (!d.has(0, 4) || d.p[0] != 0xFF || d.p[1] != 0xD8) return 0;
    std::size_t i = 2;
    while (d.has(i, 2)) {
        if (d.p[i] != 0xFF) return 0;  // lost sync with the marker stream
        const unsigned marker = d.p[i + 1];
        if (marker == 0xFF) {  // fill byte
            ++i;
            continue;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) {  // markers without a length
            i += 2;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) return 0;  // end of image / start of pixel data

        unsigned len = 0;
        if (!d.u16(i + 2, false, len) || len < 2 || !d.has(i + 2, len)) return 0;
        const std::size_t seg = i + 4, seg_len = len - 2;
        if (marker == 0xE1 && seg_len >= 14 && std::memcmp(d.p + seg, "Exif\0\0", 6) == 0) {
            const int o = tiff_orientation({d.p + seg + 6, seg_len - 6});
            if (o) return o;
        }
        i += 2 + std::size_t(len);
    }
    return 0;
}

int png_orientation(const bytes& d) {
    static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!d.has(0, 8) || std::memcmp(d.p, sig, 8) != 0) return 0;
    std::size_t i = 8;
    while (d.has(i, 12)) {  // length + type + CRC at least
        std::uint32_t len = 0;
        d.u32(i, false, len);
        if (!d.has(i + 8, std::size_t(len) + 4)) return 0;
        if (std::memcmp(d.p + i + 4, "eXIf", 4) == 0) {
            const int o = tiff_orientation({d.p + i + 8, len});
            if (o) return o;
        }
        if (std::memcmp(d.p + i + 4, "IEND", 4) == 0) return 0;
        i += 12 + std::size_t(len);
    }
    return 0;
}

}  // namespace

int exif_orientation(const void* data, std::size_t size) noexcept {
    if (!data) return 1;
    const bytes d{static_cast<const std::uint8_t*>(data), size};
    int o = jpeg_orientation(d);
    if (!o) o = png_orientation(d);
    return o ? o : 1;
}

}  // namespace stb
