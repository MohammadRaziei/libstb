#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "test_common.hpp"

// stb::rgba_to_premultiplied_argb / premultiplied_argb_to_rgba. The expected values come from
// the plain reference formulas written out here (not from the library's own kernels), and every
// SIMD backend this machine can run is checked against them for every (colour, alpha) pair.

namespace {

using bytes = std::vector<std::uint8_t>;

// straight RGBA -> the premultiplied word 0xAARRGGBB
std::uint32_t reference_premultiply(int r, int g, int b, int a) {
    if (a == 0) return 0;
    if (a != 255) {
        r = r * a / 255;
        g = g * a / 255;
        b = b * a / 255;
    }
    return std::uint32_t(a) << 24 | std::uint32_t(r) << 16 | std::uint32_t(g) << 8 | std::uint32_t(b);
}

// the premultiplied word -> straight RGBA bytes (a byte store: wraps modulo 256)
std::array<std::uint8_t, 4> reference_unpremultiply(std::uint32_t w) {
    const int a = int(w >> 24);
    int r = int((w >> 16) & 255), g = int((w >> 8) & 255), b = int(w & 255);
    if (a == 0) return {0, 0, 0, 0};
    if (a != 255) {
        r = r * 255 / a;
        g = g * 255 / a;
        b = b * 255 / a;
    }
    return {std::uint8_t(r), std::uint8_t(g), std::uint8_t(b), std::uint8_t(a)};
}

std::uint32_t word_at(const std::uint8_t* p) {
    std::uint32_t w;
    std::memcpy(&w, p, 4);
    return w;
}

void put_word(std::uint8_t* p, std::uint32_t w) { std::memcpy(p, &w, 4); }

// The three colour channels that go with the colour value c, distinct so that a swapped
// channel shows up.
int green_of(int c) { return 255 - c; }
int blue_of(int c) { return (c * 7) & 255; }

constexpr std::uint8_t pad_byte = 0xAB;

// utest has no "<< message" on its checks: a failing check prints its context through report().
struct msg {
    std::ostringstream o;
    template <class T>
    msg& operator<<(const T& v) {
        o << v;
        return *this;
    }
};

bool report(bool ok, const msg& m) {
    if (!ok) {  // braces: UTEST_PRINTF expands to an unbraced if followed by a printf
        UTEST_PRINTF("%s\n", m.o.str().c_str());
    }
    return ok;
}

// A width x height block of 4-byte pixels with `pad` extra bytes after every row, filled with
// pad_byte, and the pixel (x, y) set by fill(x, y).
template <class Fill>
bytes block(int width, int height, std::size_t pad, Fill fill) {
    const std::size_t stride = 4 * std::size_t(width) + pad;
    bytes b(stride * std::size_t(height), pad_byte);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) fill(x, y, b.data() + std::size_t(y) * stride + 4 * std::size_t(x));
    return b;
}

bool padding_untouched(const bytes& b, int width, int height, std::size_t pad) {
    const std::size_t stride = 4 * std::size_t(width) + pad;
    for (int y = 0; y < height; ++y)
        for (std::size_t k = 4 * std::size_t(width); k < stride; ++k)
            if (b[std::size_t(y) * stride + k] != pad_byte) return false;
    return true;
}

// Runs fn once for every SIMD backend this machine can run.
template <class Fn>
void for_each_backend(Fn fn) {
    for (const std::string& name : stb::simd_backends()) {
        stb::simd_scope scope(name);
        fn(name);
    }
}

}  // namespace

// ------------------------------------------------------------ the formulas

UTEST(libstb_premultiply_tests, test_known_pixels) {
    // R 255, G 128, B 64 at alpha 128: 255 * 128 / 255 = 128, 128 * 128 / 255 = 64, 64 * 128 / 255 = 32
    const std::uint8_t rgba[4] = {255, 128, 64, 128};
    std::uint8_t out[4];
    stb::rgba_to_premultiplied_argb(out, 4, rgba, 4, 1, 1);
    ASSERT_EQ(0x80804020u, word_at(out));

    // and back: 128 * 255 / 128 = 255, 64 * 255 / 128 = 127 (truncated), 32 * 255 / 128 = 63
    std::uint8_t back[4];
    stb::premultiplied_argb_to_rgba(back, 4, out, 4, 1, 1);
    ASSERT_EQ(255, back[0]);
    ASSERT_EQ(127, back[1]);
    ASSERT_EQ(63, back[2]);
    ASSERT_EQ(128, back[3]);
}

UTEST(libstb_premultiply_tests, test_opaque_pixels_only_change_their_order_and_transparent_ones_become_zero) {
    const std::uint8_t opaque[4] = {10, 20, 30, 255}, clear[4] = {10, 20, 30, 0};
    std::uint8_t out[4];
    stb::rgba_to_premultiplied_argb(out, 4, opaque, 4, 1, 1);
    ASSERT_EQ(0xFF0A141Eu, word_at(out));
    stb::premultiplied_argb_to_rgba(out, 4, out, 4, 1, 1);  // in place
    ASSERT_EQ(10, out[0]);
    ASSERT_EQ(20, out[1]);
    ASSERT_EQ(30, out[2]);
    ASSERT_EQ(255, out[3]);

    stb::rgba_to_premultiplied_argb(out, 4, clear, 4, 1, 1);
    ASSERT_EQ(0u, word_at(out));
}

UTEST(libstb_premultiply_tests, test_a_colour_above_its_alpha_wraps_like_a_byte_store) {
    // Not a valid premultiplied pixel (200 > 100): 200 * 255 / 100 = 510, stored as 510 - 256 = 254.
    std::uint8_t in[4];
    put_word(in, 100u << 24 | 200u << 16 | 100u << 8 | 50u);
    std::uint8_t out[4];
    stb::premultiplied_argb_to_rgba(out, 4, in, 4, 1, 1);
    ASSERT_EQ(254, out[0]);
    ASSERT_EQ(255, out[1]);  // 100 * 255 / 100
    ASSERT_EQ(127, out[2]);  // 50 * 255 / 100 = 127
    ASSERT_EQ(100, out[3]);
}

// ----------------------------------------------- every (colour, alpha) pair, every backend

UTEST(libstb_premultiply_tests, test_every_colour_and_alpha_pair_on_every_backend) {
    // x is the colour value, y the alpha: 256 x 256 pixels, rows padded (stride > 4 * width).
    constexpr int n = 256;
    constexpr std::size_t pad = 12;
    const bytes straight = block(n, n, pad, [](int x, int y, std::uint8_t* p) {
        p[0] = std::uint8_t(x);
        p[1] = std::uint8_t(green_of(x));
        p[2] = std::uint8_t(blue_of(x));
        p[3] = std::uint8_t(y);
    });
    const bytes premultiplied = block(n, n, pad, [](int x, int y, std::uint8_t* p) {
        put_word(p, std::uint32_t(y) << 24 | std::uint32_t(x) << 16 | std::uint32_t(green_of(x)) << 8 | std::uint32_t(blue_of(x)));
    });
    const std::size_t stride = 4 * n + pad;

    for_each_backend([&](const std::string& backend) {
        // out of place, straight -> premultiplied
        bytes out(straight.size(), pad_byte);
        stb::rgba_to_premultiplied_argb(out.data(), stride, straight.data(), stride, n, n);
        bool ok = true;
        for (int y = 0; y < n && ok; ++y)
            for (int x = 0; x < n; ++x) {
                const std::uint32_t want = reference_premultiply(x, green_of(x), blue_of(x), y);
                const std::uint32_t got = word_at(out.data() + std::size_t(y) * stride + 4 * std::size_t(x));
                if (got != want) {
                    UTEST_PRINTF("%s: colour %d alpha %d: got %08x, want %08x\n", backend.c_str(), x, y, got, want);
                    ok = false;
                    break;
                }
            }
        EXPECT_TRUE(report(ok, msg() << backend << ": premultiply differs from the reference"));
        EXPECT_TRUE(report(padding_untouched(out, n, n, pad), msg() << backend));

        // in place, same result
        bytes in_place = straight;
        stb::rgba_to_premultiplied_argb(in_place.data(), stride, in_place.data(), stride, n, n);
        EXPECT_TRUE(report(in_place == out, msg() << backend << ": in place differs from out of place"));

        // out of place, premultiplied -> straight (includes every colour above its alpha)
        bytes back(premultiplied.size(), pad_byte);
        stb::premultiplied_argb_to_rgba(back.data(), stride, premultiplied.data(), stride, n, n);
        ok = true;
        for (int y = 0; y < n && ok; ++y)
            for (int x = 0; x < n; ++x) {
                const std::uint32_t w = word_at(premultiplied.data() + std::size_t(y) * stride + 4 * std::size_t(x));
                const std::array<std::uint8_t, 4> want = reference_unpremultiply(w);
                const std::uint8_t* got = back.data() + std::size_t(y) * stride + 4 * std::size_t(x);
                if (std::memcmp(got, want.data(), 4) != 0) {
                    UTEST_PRINTF("%s: word %08x: got %d %d %d %d, want %d %d %d %d\n", backend.c_str(), w, got[0], got[1], got[2],
                                 got[3], want[0], want[1], want[2], want[3]);
                    ok = false;
                    break;
                }
            }
        EXPECT_TRUE(report(ok, msg() << backend << ": unpremultiply differs from the reference"));
        EXPECT_TRUE(report(padding_untouched(back, n, n, pad), msg() << backend));

        bytes back_in_place = premultiplied;
        stb::premultiplied_argb_to_rgba(back_in_place.data(), stride, back_in_place.data(), stride, n, n);
        EXPECT_TRUE(report(back_in_place == back, msg() << backend << ": in place differs from out of place"));
    });
}

// ------------------------------------------------------- shapes, alignment, edges

UTEST(libstb_premultiply_tests, test_widths_around_a_vector_length_with_and_without_padding_and_at_an_odd_address) {
    for_each_backend([&](const std::string& backend) {
        for (int w : {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 100})
            for (int h : {1, 2, 3})
                for (std::size_t pad : {std::size_t(0), std::size_t(4), std::size_t(13)}) {
                    const std::size_t stride = 4 * std::size_t(w) + pad;
                    // A varied block that starts one byte into the allocation (an unaligned address).
                    bytes src_store(1 + stride * std::size_t(h), pad_byte), dst_store(1 + stride * std::size_t(h), pad_byte);
                    std::uint8_t* src = src_store.data() + 1;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) {
                            std::uint8_t* p = src + std::size_t(y) * stride + 4 * std::size_t(x);
                            const int i = x * 37 + y * 101;
                            p[0] = std::uint8_t(i), p[1] = std::uint8_t(i * 3 + 1), p[2] = std::uint8_t(i * 5 + 2), p[3] = std::uint8_t(i * 11);
                        }
                    stb::rgba_to_premultiplied_argb(dst_store.data() + 1, stride, src, stride, w, h);
                    bool ok = true;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) {
                            const std::uint8_t* p = src + std::size_t(y) * stride + 4 * std::size_t(x);
                            const std::uint32_t want = reference_premultiply(p[0], p[1], p[2], p[3]);
                            ok = ok && word_at(dst_store.data() + 1 + std::size_t(y) * stride + 4 * std::size_t(x)) == want;
                        }
                    EXPECT_TRUE(report(ok, msg() << backend << " " << w << "x" << h << " pad " << pad));
                    EXPECT_EQ(pad_byte, dst_store[0]);  // nothing written before the block
                    for (int y = 0; y < h; ++y)
                        for (std::size_t k = 4 * std::size_t(w); k < stride; ++k)
                            EXPECT_TRUE(report(dst_store[1 + std::size_t(y) * stride + k] == pad_byte, msg() << backend << " padding"));

                    // and the inverse on the result, in place
                    bytes round = dst_store;
                    stb::premultiplied_argb_to_rgba(round.data() + 1, stride, round.data() + 1, stride, w, h);
                    ok = true;
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) {
                            const std::size_t at = 1 + std::size_t(y) * stride + 4 * std::size_t(x);
                            const std::array<std::uint8_t, 4> want = reference_unpremultiply(word_at(dst_store.data() + at));
                            ok = ok && std::memcmp(round.data() + at, want.data(), 4) == 0;
                        }
                    EXPECT_TRUE(report(ok, msg() << backend << " inverse " << w << "x" << h << " pad " << pad));
                }
    });
}

UTEST(libstb_premultiply_tests, test_opaque_runs_and_the_one_pixel_in_them_that_is_not) {
    // The kernels take blocks of pixels whose alphas are all 255 on a shortcut (only R and B swap):
    // a block with a single other pixel, wherever it sits, must still be converted by the formula.
    for_each_backend([&](const std::string& backend) {
        for (int w : {31, 32, 33, 64, 65, 96})
            for (int odd : {-1, 0, 1, 15, 31, 32, 63, 95}) {
                if (odd >= w) continue;
                for (int other_alpha : {0, 1, 200, 254}) {
                    bytes straight(4 * std::size_t(w)), premultiplied(4 * std::size_t(w));
                    for (int x = 0; x < w; ++x) {
                        const int a = x == odd ? other_alpha : 255;
                        std::uint8_t* p = straight.data() + 4 * std::size_t(x);
                        p[0] = std::uint8_t(x * 29 + 3), p[1] = std::uint8_t(x * 53 + 7), p[2] = std::uint8_t(x * 71 + 11), p[3] = std::uint8_t(a);
                        put_word(premultiplied.data() + 4 * std::size_t(x), std::uint32_t(a) << 24 | std::uint32_t(p[0]) << 16 | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]));
                    }
                    bytes out(straight.size()), back(straight.size());
                    stb::rgba_to_premultiplied_argb(out.data(), 4 * std::size_t(w), straight.data(), 4 * std::size_t(w), w, 1);
                    stb::premultiplied_argb_to_rgba(back.data(), 4 * std::size_t(w), premultiplied.data(), 4 * std::size_t(w), w, 1);
                    bytes out_in_place = straight, back_in_place = premultiplied;
                    stb::rgba_to_premultiplied_argb(out_in_place.data(), 4 * std::size_t(w), out_in_place.data(), 4 * std::size_t(w), w, 1);
                    stb::premultiplied_argb_to_rgba(back_in_place.data(), 4 * std::size_t(w), back_in_place.data(), 4 * std::size_t(w), w, 1);
                    bool ok = out == out_in_place && back == back_in_place;
                    for (int x = 0; x < w; ++x) {
                        const std::uint8_t* p = straight.data() + 4 * std::size_t(x);
                        ok = ok && word_at(out.data() + 4 * std::size_t(x)) == reference_premultiply(p[0], p[1], p[2], p[3]);
                        const std::array<std::uint8_t, 4> want = reference_unpremultiply(word_at(premultiplied.data() + 4 * std::size_t(x)));
                        ok = ok && std::memcmp(back.data() + 4 * std::size_t(x), want.data(), 4) == 0;
                    }
                    EXPECT_TRUE(report(ok, msg() << backend << " width " << w << ", pixel " << odd << " has alpha " << other_alpha));
                }
            }
    });
}

UTEST(libstb_premultiply_tests, test_tightly_packed_rows_give_the_same_result_as_padded_ones) {
    const int w = 37, h = 5;
    const bytes packed = block(w, h, 0, [](int x, int y, std::uint8_t* p) {
        p[0] = std::uint8_t(x * 7), p[1] = std::uint8_t(y * 40), p[2] = std::uint8_t(x + y), p[3] = std::uint8_t(x * 9 + y);
    });
    for_each_backend([&](const std::string& backend) {
        bytes a(packed.size());
        stb::rgba_to_premultiplied_argb(a.data(), 4 * w, packed.data(), 4 * w, w, h);
        // the same pixels row by row (height 1 never takes the one-call path)
        bytes b(packed.size());
        for (int y = 0; y < h; ++y)
            stb::rgba_to_premultiplied_argb(b.data() + std::size_t(y) * 4 * w, 4 * w, packed.data() + std::size_t(y) * 4 * w, 4 * w, w, 1);
        EXPECT_TRUE(report(a == b, msg() << backend));
    });
}

UTEST(libstb_premultiply_tests, test_dimensions_of_zero_or_less_do_nothing) {
    std::uint8_t src[4] = {1, 2, 3, 4}, dst[4] = {9, 9, 9, 9};
    stb::rgba_to_premultiplied_argb(dst, 4, src, 4, 0, 1);
    stb::rgba_to_premultiplied_argb(dst, 4, src, 4, 1, 0);
    stb::premultiplied_argb_to_rgba(dst, 4, src, 4, -3, 1);
    stb::premultiplied_argb_to_rgba(dst, 4, src, 4, 1, -3);
    for (std::uint8_t v : dst) ASSERT_EQ(9, v);
}

UTEST(libstb_premultiply_tests, test_works_on_an_image_buffer) {
    // The use it is made for: straight RGBA from an image, premultiplied into a rasterizer's buffer, and back.
    stb::image img = testutil::gradient(9, 4, 4);
    bytes argb(std::size_t(img.height()) * img.stride());
    stb::rgba_to_premultiplied_argb(argb.data(), img.stride(), img.data(), img.stride(), img.width(), img.height());
    stb::image back(img.width(), img.height(), stb::pixel_format::rgba);
    stb::premultiplied_argb_to_rgba(back.data(), back.stride(), argb.data(), img.stride(), img.width(), img.height());
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            // premultiplying loses colour precision, never alpha
            ASSERT_EQ(int(img(x, y, 3)), int(back(x, y, 3)));
            for (int k = 0; k < 3; ++k) ASSERT_TRUE(back(x, y, k) <= img(x, y, k));
        }
}

// ------------------------------------------------- image::premultiplied / unpremultiplied
//
// The same reference formulas, but the premultiplied side keeps the bytes in the order R, G, B, A.

UTEST(libstb_premultiply_tests, test_image_methods_match_the_reference_on_every_backend) {
    constexpr int n = 256;  // x is the colour value, y the alpha
    stb::image straight(n, n, stb::pixel_format::rgba);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            std::uint8_t* p = straight.data() + std::size_t(y) * straight.stride() + 4 * std::size_t(x);
            p[0] = std::uint8_t(x);
            p[1] = std::uint8_t(green_of(x));
            p[2] = std::uint8_t(blue_of(x));
            p[3] = std::uint8_t(y);
        }
    for_each_backend([&](const std::string& backend) {
        const stb::image pre = straight.premultiplied();
        ASSERT_EQ(n, pre.width());
        ASSERT_EQ(n, pre.height());
        ASSERT_EQ(4, stb::channel_count(pre.format()));
        bool ok = true;
        for (int y = 0; y < n && ok; ++y)
            for (int x = 0; x < n; ++x) {
                const std::uint32_t w = reference_premultiply(x, green_of(x), blue_of(x), y);  // 0xAARRGGBB
                const int want[4] = {int((w >> 16) & 255), int((w >> 8) & 255), int(w & 255), int(w >> 24)};
                for (int k = 0; k < 4; ++k)
                    if (int(pre(x, y, k)) != want[k]) {
                        UTEST_PRINTF("%s: colour %d alpha %d channel %d: got %d, want %d\n", backend.c_str(), x, y, k,
                                     int(pre(x, y, k)), want[k]);
                        ok = false;
                        break;
                    }
            }
        EXPECT_TRUE(report(ok, msg() << backend << ": premultiplied() differs from the reference"));

        // The inverse, on every (colour, alpha) pair including colours above their alpha (wraps like a byte store).
        const stb::image back = straight.unpremultiplied();
        ok = true;
        for (int y = 0; y < n && ok; ++y)
            for (int x = 0; x < n; ++x) {
                // `straight` read as premultiplied RGBA is the ARGB word with R and B exchanged
                const std::uint32_t argb = std::uint32_t(y) << 24 | std::uint32_t(x) << 16 | std::uint32_t(green_of(x)) << 8 | std::uint32_t(blue_of(x));
                const std::array<std::uint8_t, 4> want = reference_unpremultiply(argb);
                for (int k = 0; k < 4; ++k)
                    if (back(x, y, k) != want[std::size_t(k)]) {
                        UTEST_PRINTF("%s: colour %d alpha %d channel %d: got %d, want %d\n", backend.c_str(), x, y, k,
                                     int(back(x, y, k)), int(want[std::size_t(k)]));
                        ok = false;
                        break;
                    }
            }
        EXPECT_TRUE(report(ok, msg() << backend << ": unpremultiplied() differs from the reference"));
    });
}

UTEST(libstb_premultiply_tests, test_image_methods_agree_with_the_argb_functions) {
    const stb::image img = testutil::gradient(37, 11, 4);
    bytes argb(std::size_t(img.height()) * img.stride());
    stb::rgba_to_premultiplied_argb(argb.data(), img.stride(), img.data(), img.stride(), img.width(), img.height());
    const stb::image pre = img.premultiplied();
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const std::uint32_t w = word_at(argb.data() + std::size_t(y) * img.stride() + 4 * std::size_t(x));
            ASSERT_EQ(int((w >> 16) & 255), int(pre(x, y, 0)));
            ASSERT_EQ(int((w >> 8) & 255), int(pre(x, y, 1)));
            ASSERT_EQ(int(w & 255), int(pre(x, y, 2)));
            ASSERT_EQ(int(w >> 24), int(pre(x, y, 3)));
        }
}

UTEST(libstb_premultiply_tests, test_image_methods_leave_the_original_and_round_trip_opaque_and_clear) {
    stb::image img(3, 1, stb::pixel_format::rgba, {10, 20, 30, 255, 200, 100, 50, 0, 90, 80, 70, 128});
    const stb::image before = img.copy();
    const stb::image pre = img.premultiplied();
    EXPECT_TRUE(report(std::memcmp(img.data(), before.data(), 12) == 0, msg() << "the original changed"));
    // opaque pixels do not change; alpha 0 gives zeros; 128 halves (rounding down)
    EXPECT_EQ(10, int(pre(0, 0, 0)));
    EXPECT_EQ(30, int(pre(0, 0, 2)));
    for (int k = 0; k < 4; ++k) EXPECT_EQ(0, int(pre(1, 0, k)));
    EXPECT_EQ(45, int(pre(2, 0, 0)));
    EXPECT_EQ(128, int(pre(2, 0, 3)));
    const stb::image back = pre.unpremultiplied();
    for (int k = 0; k < 4; ++k) EXPECT_EQ(int(img(0, 0, k)), int(back(0, 0, k)));  // opaque: exact
    for (int k = 0; k < 4; ++k) EXPECT_EQ(0, int(back(1, 0, k)));                   // clear: all zeros
    for (int k = 0; k < 3; ++k) EXPECT_TRUE(back(2, 0, k) <= img(2, 0, k));         // loses precision, never gains
}

UTEST(libstb_premultiply_tests, test_image_methods_on_gray_alpha_and_formats_without_alpha) {
    stb::image ga(4, 1, stb::pixel_format::gray_alpha, {200, 255, 200, 0, 200, 128, 7, 1});
    const stb::image pre = ga.premultiplied();
    ASSERT_EQ(2, stb::channel_count(pre.format()));
    EXPECT_EQ(200, int(pre(0, 0, 0)));
    EXPECT_EQ(0, int(pre(1, 0, 0)));
    EXPECT_EQ(100, int(pre(2, 0, 0)));  // 200 * 128 / 255
    EXPECT_EQ(0, int(pre(3, 0, 0)));    // 7 * 1 / 255
    for (int x = 0; x < 4; ++x) EXPECT_EQ(int(ga(x, 0, 1)), int(pre(x, 0, 1)));
    const stb::image back = pre.unpremultiplied();
    EXPECT_EQ(200, int(back(0, 0, 0)));
    EXPECT_EQ(0, int(back(1, 0, 0)));
    EXPECT_EQ(199, int(back(2, 0, 0)));  // 100 * 255 / 128, truncated
    EXPECT_EQ(0, int(back(3, 0, 0)));    // alpha 1 with colour 0

    const stb::image rgb = testutil::gradient(5, 3, 3);
    const stb::image rgb_pre = rgb.premultiplied();
    ASSERT_EQ(3, stb::channel_count(rgb_pre.format()));
    EXPECT_TRUE(report(std::memcmp(rgb.data(), rgb_pre.data(), std::size_t(rgb.height()) * rgb.stride()) == 0, msg() << "rgb"));
    const stb::image gray = testutil::gradient(5, 3, 1);
    EXPECT_TRUE(report(std::memcmp(gray.data(), gray.unpremultiplied().data(), std::size_t(gray.height()) * gray.stride()) == 0, msg() << "gray"));
}

UTEST(libstb_premultiply_tests, test_image_methods_work_on_wrapped_memory_without_touching_it) {
    bytes mem = {10, 20, 30, 128, 40, 50, 60, 255};
    const bytes keep = mem;
    stb::image view = stb::image::wrap(2, 1, stb::pixel_format::rgba, mem.data());
    const stb::image pre = view.premultiplied();
    EXPECT_TRUE(report(mem == keep, msg() << "the wrapped memory changed"));
    EXPECT_EQ(5, int(pre(0, 0, 0)));
    EXPECT_EQ(40, int(pre(1, 0, 0)));
}
