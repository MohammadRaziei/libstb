#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "test_common.hpp"

// composite_row / composite_solid_row on premultiplied 0xAARRGGBB rows. The expected values come from the
// W3C formulas written out here in floating point (not from the library's integer code):
//   colour = Fa * (Cs (1 - ab) + as ab B(Cb / ab, Cs / as)) + Fb * Cb      alpha = Fa as + Fb ab
// for the 13 operators x 9 blend modes, with the coverage applied as dest + coverage * (result - dest).
// A second check compares with image::blend, the straight-alpha path.

namespace {

using stb::blend_mode;
using stb::composite_op;
using u32 = std::uint32_t;

const composite_op all_ops[] = {composite_op::clear,           composite_op::source,
                                composite_op::destination,     composite_op::source_over,
                                composite_op::destination_over, composite_op::source_in,
                                composite_op::destination_in,  composite_op::source_out,
                                composite_op::destination_out, composite_op::source_atop,
                                composite_op::destination_atop, composite_op::exclusive_or,
                                composite_op::plus_lighter};
const blend_mode all_modes[] = {blend_mode::normal,     blend_mode::multiply, blend_mode::screen,
                                blend_mode::overlay,    blend_mode::darken,   blend_mode::lighten,
                                blend_mode::add,        blend_mode::difference, blend_mode::exclusion};

double mix_channel(blend_mode m, double b, double s) {
    switch (m) {
        case blend_mode::normal: return s;
        case blend_mode::multiply: return b * s;
        case blend_mode::screen: return b + s - b * s;
        case blend_mode::overlay: return b <= 0.5 ? 2 * b * s : 1 - 2 * (1 - b) * (1 - s);
        case blend_mode::darken: return std::min(b, s);
        case blend_mode::lighten: return std::max(b, s);
        case blend_mode::add: return std::min(1.0, b + s);
        case blend_mode::difference: return std::fabs(b - s);
        case blend_mode::exclusion: return b + s - 2 * b * s;
    }
    return s;
}

void factors(composite_op op, double sa, double ab, double& fa, double& fb) {
    switch (op) {
        case composite_op::clear: fa = 0, fb = 0; return;
        case composite_op::source: fa = 1, fb = 0; return;
        case composite_op::destination: fa = 0, fb = 1; return;
        case composite_op::source_over: fa = 1, fb = 1 - sa; return;
        case composite_op::destination_over: fa = 1 - ab, fb = 1; return;
        case composite_op::source_in: fa = ab, fb = 0; return;
        case composite_op::destination_in: fa = 0, fb = sa; return;
        case composite_op::source_out: fa = 1 - ab, fb = 0; return;
        case composite_op::destination_out: fa = 0, fb = 1 - sa; return;
        case composite_op::source_atop: fa = ab, fb = 1 - sa; return;
        case composite_op::destination_atop: fa = 1 - ab, fb = sa; return;
        case composite_op::exclusive_or: fa = 1 - ab, fb = 1 - sa; return;
        case composite_op::plus_lighter: fa = 1, fb = 1; return;
    }
}

int channel(u32 p, int shift) { return int((p >> shift) & 255); }

// expected channels of the result, in the order B, G, R, A (shift 0, 8, 16, 24)
void reference(composite_op op, blend_mode mode, u32 d, u32 s, int ca, double out[4]) {
    const double sa = channel(s, 24) / 255.0, ab = channel(d, 24) / 255.0, c = ca / 255.0;
    double fa, fb;
    factors(op, sa, ab, fa, fb);
    for (int i = 0; i < 4; ++i) {
        const int shift = 8 * i;
        const double cs = channel(s, shift) / 255.0, cb = channel(d, shift) / 255.0;
        double full;
        if (i == 3) {
            full = fa * sa + fb * ab;
        } else {
            const double us = sa > 0 ? cs / sa : 0, ub = ab > 0 ? cb / ab : 0;
            const double blended = cs * (1 - ab) + sa * ab * mix_channel(mode, ub, us);
            full = fa * blended + fb * cb;
        }
        full = std::min(1.0, full);
        out[i] = 255.0 * (cb + c * (full - cb));
    }
}

// premultiplied pixels with a spread of alpha and colours (every channel <= alpha)
std::vector<u32> sample_pixels() {
    std::vector<u32> px;
    for (const int a : {0, 1, 40, 128, 200, 255}) {
        for (const int r : {0, a / 3, a})
            for (const int g : {0, a / 2, a})
                for (const int b : {0, (2 * a) / 3, a}) px.push_back(u32(a) << 24 | u32(r) << 16 | u32(g) << 8 | u32(b));
    }
    return px;
}

}  // namespace

UTEST(libstb_composite_row_tests, test_every_operator_and_mode_matches_the_float_formulas) {
    const std::vector<u32> px = sample_pixels();
    // every pair (source, destination): the source row against the destination row shifted by a prime
    std::vector<u32> src, dst;
    for (std::size_t i = 0; i < px.size(); ++i)
        for (std::size_t j = 0; j < px.size(); j += 3) {
            src.push_back(px[i]);
            dst.push_back(px[j]);
        }
    double worst = 0;
    for (const composite_op op : all_ops) {
        for (const blend_mode mode : all_modes) {
            for (const int ca : {255, 140, 0}) {
                std::vector<u32> out = dst;
                stb::composite_row(out.data(), src.data(), out.size(), op, mode, std::uint8_t(ca));
                for (std::size_t i = 0; i < out.size(); ++i) {
                    double want[4];
                    reference(op, mode, dst[i], src[i], ca, want);
                    for (int c = 0; c < 4; ++c) {
                        const double err = std::fabs(channel(out[i], 8 * c) - want[c]);
                        worst = std::max(worst, err);
                        if (err > 2.0) {
                            UTEST_PRINTF("op %s mode %s ca %d src %08x dst %08x channel %d: got %d want %.2f\n",
                                         std::string(stb::composite_op_name(op)).c_str(),
                                         std::string(stb::blend_mode_name(mode)).c_str(), ca, unsigned(src[i]),
                                         unsigned(dst[i]), c, channel(out[i], 8 * c), want[c]);
                        }
                        ASSERT_TRUE(err <= 2.0);
                    }
                    // a premultiplied pixel stays valid: no colour above its alpha
                    ASSERT_TRUE(channel(out[i], 0) <= channel(out[i], 24));
                    ASSERT_TRUE(channel(out[i], 8) <= channel(out[i], 24));
                    ASSERT_TRUE(channel(out[i], 16) <= channel(out[i], 24));
                }
            }
        }
    }
    UTEST_PRINTF("largest error against the float formulas: %.2f levels\n", worst);
}

UTEST(libstb_composite_row_tests, test_the_plain_operators_are_within_one_level) {
    const std::vector<u32> px = sample_pixels();
    for (const composite_op op : all_ops) {
        for (const int ca : {255, 100}) {
            for (const u32 s : px) {
                std::vector<u32> out = px;
                stb::composite_solid_row(out.data(), s, out.size(), op, blend_mode::normal, std::uint8_t(ca));
                for (std::size_t i = 0; i < out.size(); ++i) {
                    double want[4];
                    reference(op, blend_mode::normal, px[i], s, ca, want);
                    for (int c = 0; c < 4; ++c) {
                        if (std::fabs(channel(out[i], 8 * c) - want[c]) > 1.5) {
                            UTEST_PRINTF("op %s ca %d src %08x dst %08x channel %d: got %d want %.2f\n",
                                         std::string(stb::composite_op_name(op)).c_str(), ca, unsigned(s),
                                         unsigned(px[i]), c, channel(out[i], 8 * c), want[c]);
                        }
                        ASSERT_TRUE(std::fabs(channel(out[i], 8 * c) - want[c]) <= 1.5);
                    }
                }
            }
        }
    }
}

UTEST(libstb_composite_row_tests, test_a_solid_row_is_the_same_as_a_row_of_that_colour) {
    const std::vector<u32> px = sample_pixels();
    for (const composite_op op : all_ops) {
        for (const blend_mode mode : {blend_mode::normal, blend_mode::multiply, blend_mode::overlay}) {
            for (const int ca : {255, 77}) {
                const u32 color = 0x80402010u;
                std::vector<u32> a = px, b = px;
                const std::vector<u32> same(px.size(), color);
                stb::composite_solid_row(a.data(), color, a.size(), op, mode, std::uint8_t(ca));
                stb::composite_row(b.data(), same.data(), b.size(), op, mode, std::uint8_t(ca));
                ASSERT_TRUE(a == b);
            }
        }
    }
}

UTEST(libstb_composite_row_tests, test_exact_cases) {
    const std::vector<u32> below = {0x00000000u, 0x40102030u, 0xff112233u};
    {  // an opaque source replaces what is below; a transparent one changes nothing; clear empties
        std::vector<u32> d = below;
        stb::composite_solid_row(d.data(), 0xffaabbccu, 3, composite_op::source_over, blend_mode::normal, 255);
        for (const u32 p : d) ASSERT_EQ(0xffaabbccu, p);
        d = below;
        stb::composite_solid_row(d.data(), 0u, 3, composite_op::source_over, blend_mode::normal, 255);
        ASSERT_TRUE(d == below);
        stb::composite_solid_row(d.data(), 0xffffffffu, 3, composite_op::clear, blend_mode::multiply, 255);
        for (const u32 p : d) ASSERT_EQ(0u, p);
    }
    {  // destination never changes, and coverage 0 changes nothing for any operator or mode
        for (const composite_op op : all_ops)
            for (const blend_mode mode : all_modes) {
                std::vector<u32> d = below;
                stb::composite_solid_row(d.data(), 0x80402010u, 3, op, mode, 0);
                ASSERT_TRUE(d == below);
                if (op == composite_op::destination) {
                    stb::composite_solid_row(d.data(), 0x80402010u, 3, op, mode, 255);
                    ASSERT_TRUE(d == below);
                }
            }
    }
    {  // plus_lighter stops at 255 in every channel
        std::vector<u32> d = {0xf0f0f0f0u};
        stb::composite_solid_row(d.data(), 0x300a1420u, 1, composite_op::plus_lighter, blend_mode::normal, 255);
        ASSERT_EQ(255, channel(d[0], 24));
        ASSERT_EQ(0xfa, channel(d[0], 16));
        ASSERT_EQ(255, channel(d[0], 8));
        ASSERT_EQ(255, channel(d[0], 0));
    }
    {  // every opaque colour multiplied by white stays, by black goes black
        std::vector<u32> d = {0xff336699u};
        stb::composite_solid_row(d.data(), 0xffffffffu, 1, composite_op::source_over, blend_mode::multiply, 255);
        ASSERT_EQ(0xff336699u, d[0]);
        stb::composite_solid_row(d.data(), 0xff000000u, 1, composite_op::source_over, blend_mode::multiply, 255);
        ASSERT_EQ(0xff000000u, d[0]);
    }
}

UTEST(libstb_composite_row_tests, test_a_row_can_be_composited_onto_itself_and_odd_lengths_work) {
    std::vector<u32> row;
    for (int i = 0; i < 37; ++i) row.push_back(u32(i * 6) << 24 | u32(i) << 16 | u32(i * 2) << 8 | u32(i * 3));
    std::vector<u32> separate = row, copy = row;
    stb::composite_row(row.data(), row.data(), row.size(), composite_op::source_over, blend_mode::screen, 200);
    stb::composite_row(separate.data(), copy.data(), separate.size(), composite_op::source_over, blend_mode::screen, 200);
    ASSERT_TRUE(row == separate);
}

UTEST(libstb_composite_row_tests, test_bad_arguments) {
    std::vector<u32> d(4, 0xff000000u), s(4, 0x80000000u);
    ASSERT_THROWS(stb::composite_row(nullptr, s.data(), 4, composite_op::source_over, blend_mode::normal, 255), std::invalid_argument);
    ASSERT_THROWS(stb::composite_row(d.data(), nullptr, 4, composite_op::source_over, blend_mode::normal, 255), std::invalid_argument);
    ASSERT_THROWS(stb::composite_solid_row(nullptr, 0u, 4, composite_op::source_over, blend_mode::normal, 255), std::invalid_argument);
    ASSERT_THROWS(stb::composite_row(d.data(), s.data(), 4, composite_op(13), blend_mode::normal, 255), std::invalid_argument);
    ASSERT_THROWS(stb::composite_row(d.data(), s.data(), 4, composite_op::source_over, blend_mode(9), 255), std::invalid_argument);
    // an empty row needs no pointers
    stb::composite_row(nullptr, nullptr, 0, composite_op::source_over, blend_mode::normal, 255);
    stb::composite_solid_row(nullptr, 0u, 0, composite_op::source_over, blend_mode::normal, 255);
}

UTEST(libstb_composite_row_tests, test_agrees_with_the_straight_alpha_path) {
    // image::blend works on straight alpha in floating point; the premultiplied row must give the same
    // picture (compared as premultiplied pixels) to within the precision of 8-bit premultiplied storage.
    const std::vector<u32> px = sample_pixels();
    const int n = int(px.size());
    std::vector<u32> src, dst;
    for (int i = 0; i < n; ++i) {
        src.push_back(px[std::size_t(i)]);
        dst.push_back(px[std::size_t((i * 7 + 3) % n)]);
    }
    const stb::premultiplied_image dst_pre = [&] {
        stb::premultiplied_image img(n, 1);
        std::memcpy(img.data(), dst.data(), dst.size() * 4);
        return img;
    }();
    const stb::premultiplied_image src_pre = [&] {
        stb::premultiplied_image img(n, 1);
        std::memcpy(img.data(), src.data(), src.size() * 4);
        return img;
    }();
    for (const composite_op op : all_ops) {
        for (const blend_mode mode : all_modes) {
            stb::image straight = dst_pre.to_image();
            straight.blend(src_pre.to_image(), 0, 0, mode, 1.0f, op);
            const stb::premultiplied_image want = stb::premultiplied_image::from_image(straight);
            std::vector<u32> out = dst;
            stb::composite_row(out.data(), src.data(), out.size(), op, mode, 255);
            for (int i = 0; i < n; ++i) {
                u32 w;
                std::memcpy(&w, want.data() + 4 * i, 4);
                // pixels with little alpha lose colour precision in the straight round trip: skip those
                if (channel(w, 24) < 64 || channel(dst[std::size_t(i)], 24) < 64 || channel(src[std::size_t(i)], 24) < 64) continue;
                for (int c = 0; c < 4; ++c) ASSERT_TRUE(std::abs(channel(out[std::size_t(i)], 8 * c) - channel(w, 8 * c)) <= 4);
            }
        }
    }
}
