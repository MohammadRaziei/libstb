#include <cstddef>
#include <cstdint>
#include <vector>

#include "test_common.hpp"

using testutil::gradient;
using testutil::same;

namespace {

using bytes = std::vector<std::uint8_t>;

void put16(bytes& b, unsigned v, bool big) {
    if (big) b.push_back(std::uint8_t(v >> 8)), b.push_back(std::uint8_t(v));
    else b.push_back(std::uint8_t(v)), b.push_back(std::uint8_t(v >> 8));
}

void put32(bytes& b, std::uint32_t v, bool big) {
    if (big) for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
    else for (int s = 0; s <= 24; s += 8) b.push_back(std::uint8_t(v >> s));
}

// A TIFF structure (the payload of an EXIF block) whose first IFD holds `extra` unrelated
// entries followed by the Orientation entry.
bytes tiff(unsigned orientation, bool big, unsigned type = 3, std::uint32_t count = 1, int extra = 0) {
    bytes t = {std::uint8_t(big ? 'M' : 'I'), std::uint8_t(big ? 'M' : 'I')};
    put16(t, 42, big);
    put32(t, 8, big);
    put16(t, unsigned(extra) + 1, big);
    for (int i = 0; i < extra; ++i) {
        put16(t, 0x0100 + unsigned(i), big);  // some other tag
        put16(t, 3, big);
        put32(t, 1, big);
        put32(t, 0, big);
    }
    put16(t, 0x0112, big);
    put16(t, type, big);
    put32(t, count, big);
    put16(t, orientation, big);  // a lone SHORT sits in the first two bytes of the value field
    put16(t, 0, big);
    put32(t, 0, big);            // no next IFD
    return t;
}

// SOI + a JFIF-like APP0 + an Exif APP1 + EOI: enough for the orientation reader.
bytes jpeg_with(const bytes& tiff_data) {
    bytes j = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    const unsigned len = unsigned(2 + 6 + tiff_data.size());
    j.insert(j.end(), {0xFF, 0xE1, std::uint8_t(len >> 8), std::uint8_t(len), 'E', 'x', 'i', 'f', 0, 0});
    j.insert(j.end(), tiff_data.begin(), tiff_data.end());
    j.insert(j.end(), {0xFF, 0xD9});
    return j;
}

// A real, decodable PNG with an eXIf chunk after IHDR.
bytes png_with(const stb::image& img, const bytes& tiff_data) {
    bytes p = img.to_png();
    bytes chunk;
    put32(chunk, std::uint32_t(tiff_data.size()), true);
    chunk.insert(chunk.end(), {'e', 'X', 'I', 'f'});
    chunk.insert(chunk.end(), tiff_data.begin(), tiff_data.end());
    put32(chunk, 0, true);  // CRC: neither stb nor the reader checks it
    p.insert(p.begin() + 8 + 25, chunk.begin(), chunk.end());  // signature + IHDR chunk
    return p;
}

// A real, decodable JPEG with an Exif APP1 segment right after SOI.
bytes real_jpeg_with(const stb::image& img, const bytes& tiff_data) {
    bytes j = img.to_jpg();
    const unsigned len = unsigned(2 + 6 + tiff_data.size());
    bytes seg = {0xFF, 0xE1, std::uint8_t(len >> 8), std::uint8_t(len), 'E', 'x', 'i', 'f', 0, 0};
    seg.insert(seg.end(), tiff_data.begin(), tiff_data.end());
    j.insert(j.begin() + 2, seg.begin(), seg.end());
    return j;
}

int orientation_of(const bytes& b) { return stb::exif_orientation(b.data(), b.size()); }

}  // namespace

UTEST(libstb_exif_tests, test_reads_every_orientation_from_a_jpeg_in_both_byte_orders) {
    for (unsigned o = 1; o <= 8; ++o) {
        ASSERT_EQ(int(o), orientation_of(jpeg_with(tiff(o, false))));
        ASSERT_EQ(int(o), orientation_of(jpeg_with(tiff(o, true))));
    }
}

UTEST(libstb_exif_tests, test_reads_every_orientation_from_a_png) {
    const stb::image img = gradient(4, 3, 3);
    for (unsigned o = 1; o <= 8; ++o) {
        ASSERT_EQ(int(o), orientation_of(png_with(img, tiff(o, false))));
        ASSERT_EQ(int(o), orientation_of(png_with(img, tiff(o, true))));
    }
}

UTEST(libstb_exif_tests, test_finds_the_tag_after_other_entries) {
    ASSERT_EQ(6, orientation_of(jpeg_with(tiff(6, false, 3, 1, 5))));
    ASSERT_EQ(8, orientation_of(jpeg_with(tiff(8, true, 3, 1, 5))));
}

UTEST(libstb_exif_tests, test_no_exif_or_nothing_usable_means_upright) {
    const stb::image img = gradient(4, 3, 3);
    ASSERT_EQ(1, orientation_of(img.to_png()));
    ASSERT_EQ(1, orientation_of(img.to_jpg()));
    ASSERT_EQ(1, orientation_of(img.to_bmp()));
    ASSERT_EQ(1, orientation_of(bytes{}));
    ASSERT_EQ(1, orientation_of(bytes{1, 2, 3, 4, 5, 6, 7, 8, 9}));
    ASSERT_EQ(1, stb::exif_orientation(nullptr, 100));
    ASSERT_EQ(1, orientation_of(jpeg_with(tiff(0, false))));  // values outside 1..8
    ASSERT_EQ(1, orientation_of(jpeg_with(tiff(9, false))));
    ASSERT_EQ(1, orientation_of(jpeg_with(tiff(6, false, 4))));     // not a SHORT
    ASSERT_EQ(1, orientation_of(jpeg_with(tiff(6, false, 3, 2))));  // not a single value
}

UTEST(libstb_exif_tests, test_truncated_and_corrupted_input_never_reads_out_of_bounds) {
    const bytes full_j = jpeg_with(tiff(6, false, 3, 1, 3));
    const bytes full_p = png_with(gradient(4, 3, 3), tiff(6, true, 3, 1, 3));
    for (const bytes* src : {&full_j, &full_p})
        for (std::size_t n = 0; n <= src->size(); ++n) {
            bytes cut(src->begin(), src->begin() + std::ptrdiff_t(n));
            const int o = orientation_of(cut);  // run under ASan/valgrind to see an overread
            ASSERT_TRUE(o >= 1 && o <= 8);
            if (n < 20) ASSERT_EQ(1, o);
        }
    std::uint32_t rng = 12345;
    for (int round = 0; round < 4000; ++round)
        for (const bytes* src : {&full_j, &full_p}) {
            bytes m = *src;
            for (int k = 0; k < 4; ++k) {
                rng = rng * 1664525u + 1013904223u;
                m[(rng >> 8) % m.size()] = std::uint8_t(rng >> 24);
            }
            const int o = orientation_of(m);
            ASSERT_TRUE(o >= 1 && o <= 8);
        }
}

UTEST(libstb_exif_tests, test_decode_orient_makes_a_png_upright) {
    const stb::image src = gradient(5, 3, 3);
    const bytes file = png_with(src, tiff(6, false));
    stb::image raw = stb::image::decode(file.data(), file.size());
    ASSERT_TRUE(same(src, raw));  // off by default: the stored pixels

    stb::load_options o;
    o.orient = true;
    stb::image up = stb::image::decode(file.data(), file.size(), o);
    ASSERT_EQ(3, up.width());
    ASSERT_EQ(5, up.height());
    ASSERT_TRUE(same(src.orient(6), up));
}

UTEST(libstb_exif_tests, test_decode_orient_applies_every_orientation_to_a_png_and_a_jpeg) {
    const stb::image src = gradient(6, 4, 3);
    stb::load_options o;
    o.orient = true;
    for (unsigned k = 1; k <= 8; ++k) {
        const bytes p = png_with(src, tiff(k, k % 2 == 0));
        ASSERT_TRUE(same(src.orient(int(k)), stb::image::decode(p.data(), p.size(), o)));

        const bytes j = real_jpeg_with(src, tiff(k, false));
        const stb::image plain = stb::image::decode(j.data(), j.size());
        ASSERT_TRUE(same(plain.orient(int(k)), stb::image::decode(j.data(), j.size(), o)));
    }
}

UTEST(libstb_exif_tests, test_decode_orient_then_flip_is_flip_of_the_oriented_image) {
    const stb::image src = gradient(5, 3, 3);
    const bytes file = png_with(src, tiff(6, false));
    stb::load_options o;
    o.orient = true;
    const stb::image oriented = stb::image::decode(file.data(), file.size(), o);
    o.flip = true;
    ASSERT_TRUE(same(oriented.flip_vertical(), stb::image::decode(file.data(), file.size(), o)));

    // Orientation 1 with flip: stb's own flip, unchanged.
    const bytes upright = png_with(src, tiff(1, false));
    ASSERT_TRUE(same(src.flip_vertical(), stb::image::decode(upright.data(), upright.size(), o)));
}

UTEST(libstb_exif_tests, test_decode_orient_keeps_the_channel_conversion_and_size_limit) {
    const stb::image src = gradient(5, 3, 3);
    const bytes file = png_with(src, tiff(8, false));
    stb::load_options o;
    o.orient = true;
    o.channels = 4;
    stb::image r = stb::image::decode(file.data(), file.size(), o);
    ASSERT_EQ(4, r.channels());
    ASSERT_EQ(3, r.width());
    o.max_bytes = 10;
    ASSERT_THROWS(stb::image::decode(file.data(), file.size(), o), stb::limit_error);
}
