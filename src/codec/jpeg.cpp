// The baseline JPEG encoder: 8x8 DCT, quantisation, standard Huffman tables, 4:2:0 chroma up to
// quality 90 and 4:4:4 above. Derived from stb_image_write.h v1.16, which is in turn based on
// Jon Olick's jo_jpeg.cpp (public domain / MIT).
//
// Speed: the DCT runs eight columns at a time on a transposed copy of the block (compilers turn
// the loop into SIMD by themselves), the colour conversion has an SSE2 path, and the entropy
// coder keeps a 64-bit bit accumulator. All of it uses the same arithmetic, in the same order,
// as the scalar code, so the bytes written do not depend on which path runs.

#include <algorithm>
#include <array>
#include <cstring>

#include "codec/codec.hpp"

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define LIBSTB_JPEG_SSE2 1
#include <emmintrin.h>
#endif

namespace stb::codec {
namespace {

constexpr std::uint8_t zigzag[64] = {0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13, 16, 26, 29, 42,
                                     3,  8,  12, 17, 25, 30, 41, 43, 9,  11, 18, 24, 31, 40, 44, 53,
                                     10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
                                     21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63};

// Standard quantisation tables (ITU T.81 Annex K), before scaling by quality.
constexpr int luma_quant[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr int chroma_quant[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                  24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                  99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

// Scale factors that fold the DCT's output normalisation into the quantiser.
constexpr float aan_scale[8] = {1.0f * 2.828427125f,      1.387039845f * 2.828427125f, 1.306562965f * 2.828427125f,
                                1.175875602f * 2.828427125f, 1.0f * 2.828427125f,      0.785694958f * 2.828427125f,
                                0.541196100f * 2.828427125f, 0.275899379f * 2.828427125f};

// A standard Huffman table as the file stores it: how many codes of each length 1..16, then the
// symbols in code order (ITU T.81 Annex K.3).
struct huffman_spec {
    std::array<std::uint8_t, 16> counts;
    const std::uint8_t* symbols;
    std::size_t symbol_count;
};

constexpr std::uint8_t dc_luma_symbols[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::uint8_t ac_luma_symbols[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3,
    0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
constexpr std::uint8_t ac_chroma_symbols[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1,
    0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

const huffman_spec dc_luma_spec{{0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, dc_luma_symbols, 12};
const huffman_spec ac_luma_spec{{0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d}, ac_luma_symbols, 162};
const huffman_spec dc_chroma_spec{{0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0}, dc_luma_symbols, 12};
const huffman_spec ac_chroma_spec{{0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77}, ac_chroma_symbols, 162};

// symbol -> {code, length}, assigned in code order as the file's counts imply.
class huffman_table {
public:
    explicit huffman_table(const huffman_spec& spec) : spec_(spec) {
        unsigned code = 0;
        std::size_t k = 0;
        for (int length = 1; length <= 16; ++length) {
            for (int i = 0; i < spec.counts[length - 1]; ++i, ++k, ++code) {
                entries_[spec.symbols[k]] = {static_cast<std::uint16_t>(code), static_cast<std::uint16_t>(length)};
            }
            code <<= 1;
        }
    }

    // {code, length}; length 0 for a symbol that has no code
    const std::array<std::uint16_t, 2>& operator[](std::size_t symbol) const { return entries_[symbol]; }
    const huffman_spec& spec() const { return spec_; }

private:
    huffman_spec spec_;
    std::array<std::array<std::uint16_t, 2>, 256> entries_{};
};

// Entropy-coded data goes out through a 64-bit accumulator and a 4 KB buffer: four bytes at a
// time unless one of them is 0xFF, which must be followed by a stuffed 0x00.
class bit_writer {
public:
    explicit bit_writer(bytes& out) : out_(out) {}

    // Appends the low n bits of `bits` (n <= 27, bits above n must be zero).
    void put(unsigned bits, int n) {
        acc_ = (acc_ << n) | bits;
        nbits_ += n;
        if (nbits_ >= 32) {
            nbits_ -= 32;
            put32(static_cast<unsigned>(acc_ >> nbits_));
        }
    }

    // A Huffman code followed by the amplitude bits of `value` (nb of them), in one put.
    void put_value(const std::array<std::uint16_t, 2>& code, int value, int nb) {
        const unsigned amplitude = static_cast<unsigned>(value < 0 ? value - 1 : value) & ((1u << nb) - 1);
        put((unsigned(code[0]) << nb) | amplitude, code[1] + nb);
    }

    void put_code(const std::array<std::uint16_t, 2>& code) { put(code[0], code[1]); }

    // Pads the last byte with 1 bits and writes everything out.
    void finish() {
        const int pad = (8 - (nbits_ & 7)) & 7;
        if (pad) {
            acc_ = (acc_ << pad) | ((1u << pad) - 1);
            nbits_ += pad;
        }
        while (nbits_ >= 8) {
            nbits_ -= 8;
            const auto c = static_cast<std::uint8_t>(acc_ >> nbits_);
            buf_[len_++] = c;
            if (c == 0xFF) buf_[len_++] = 0;
        }
        flush();
    }

private:
    void put32(unsigned v) {
        const unsigned t = ~v;
        if (((t - 0x01010101u) & ~t & 0x80808080u) == 0) {  // none of the four bytes is 0xFF
            std::uint8_t* o = buf_ + len_;
            o[0] = std::uint8_t(v >> 24);
            o[1] = std::uint8_t(v >> 16);
            o[2] = std::uint8_t(v >> 8);
            o[3] = std::uint8_t(v);
            len_ += 4;
        } else {
            for (int i = 24; i >= 0; i -= 8) {
                const auto c = std::uint8_t(v >> i);
                buf_[len_++] = c;
                if (c == 0xFF) buf_[len_++] = 0;
            }
        }
        if (len_ >= 4096) flush();
    }

    void flush() {
        out_.insert(out_.end(), buf_, buf_ + len_);
        len_ = 0;
    }

    bytes& out_;
    unsigned long long acc_ = 0;
    int nbits_ = 0;
    int len_ = 0;
    std::uint8_t buf_[4096 + 16];
};

// AAN DCT on 8 columns of a packed 8x8 block at once: lane i works on p[i], p[i+8], ... p[i+56].
// The 8 iterations are independent, so compilers vectorise the loop.
void dct_columns(float* p) {
    for (int i = 0; i < 8; ++i) {
        float d0 = p[i], d1 = p[i + 8], d2 = p[i + 16], d3 = p[i + 24];
        float d4 = p[i + 32], d5 = p[i + 40], d6 = p[i + 48], d7 = p[i + 56];
        float z1, z2, z3, z4, z5, z11, z13;
        float tmp0 = d0 + d7;
        float tmp7 = d0 - d7;
        float tmp1 = d1 + d6;
        float tmp6 = d1 - d6;
        float tmp2 = d2 + d5;
        float tmp5 = d2 - d5;
        float tmp3 = d3 + d4;
        float tmp4 = d3 - d4;
        float tmp10 = tmp0 + tmp3;
        float tmp13 = tmp0 - tmp3;
        float tmp11 = tmp1 + tmp2;
        float tmp12 = tmp1 - tmp2;
        d0 = tmp10 + tmp11;
        d4 = tmp10 - tmp11;
        z1 = (tmp12 + tmp13) * 0.707106781f;
        d2 = tmp13 + z1;
        d6 = tmp13 - z1;
        tmp10 = tmp4 + tmp5;
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;
        z5 = (tmp10 - tmp12) * 0.382683433f;
        z2 = tmp10 * 0.541196100f + z5;
        z4 = tmp12 * 1.306562965f + z5;
        z3 = tmp11 * 0.707106781f;
        z11 = tmp7 + z3;
        z13 = tmp7 - z3;
        p[i + 40] = z13 + z2;
        p[i + 24] = z13 - z2;
        p[i + 8] = z11 + z4;
        p[i + 56] = z11 - z4;
        p[i] = d0;
        p[i + 16] = d2;
        p[i + 32] = d4;
        p[i + 48] = d6;
    }
}

// dst[x*8+y] = src[y*stride+x] for an 8x8 block (a pure data move, no arithmetic).
void transpose8(float* dst, const float* src, int stride) {
#ifdef LIBSTB_JPEG_SSE2
    for (int by = 0; by < 8; by += 4) {
        for (int bx = 0; bx < 8; bx += 4) {
            const float* s0 = src + by * stride + bx;
            __m128 r0 = _mm_loadu_ps(s0), r1 = _mm_loadu_ps(s0 + stride);
            __m128 r2 = _mm_loadu_ps(s0 + 2 * stride), r3 = _mm_loadu_ps(s0 + 3 * stride);
            _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
            _mm_storeu_ps(dst + bx * 8 + by, r0);
            _mm_storeu_ps(dst + (bx + 1) * 8 + by, r1);
            _mm_storeu_ps(dst + (bx + 2) * 8 + by, r2);
            _mm_storeu_ps(dst + (bx + 3) * 8 + by, r3);
        }
    }
#else
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) dst[x * 8 + y] = src[y * stride + x];
#endif
}

// number of bits needed for a > 0
int bit_length(unsigned a) {
#if defined(__GNUC__) || defined(__clang__)
    return 32 - __builtin_clz(a);
#else
    int n = 1;
    while (a >>= 1) ++n;
    return n;
#endif
}

int count_trailing_zeros(unsigned long long x) {  // x != 0
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#else
    int n = 0;
    while (!(x & 1)) {
        x >>= 1;
        ++n;
    }
    return n;
#endif
}

// RGB -> YCbCr for a run of n pixels inside the image (n is 8 or 16). The arithmetic is the
// per-pixel float expression of convert_pixel, in the same order, so every rounding is the same.
void convert_run(float* luma, float* cb, float* cr, const std::uint8_t* src, int ofs_g, int ofs_b, int comp, int n) {
    int i = 0;
#ifdef LIBSTB_JPEG_SSE2
    if (comp >= 3) {  // interleaved R,G,B[,A]: four pixels per step
        const __m128 luma_r = _mm_set1_ps(0.29900f), luma_g = _mm_set1_ps(0.58700f), luma_b = _mm_set1_ps(0.11400f),
                     c128 = _mm_set1_ps(128);
        const __m128 cb_r = _mm_set1_ps(-0.16874f), cb_g = _mm_set1_ps(-0.33126f), cb_b = _mm_set1_ps(0.50000f);
        const __m128 cr_r = _mm_set1_ps(0.50000f), cr_g = _mm_set1_ps(-0.41869f), cr_b = _mm_set1_ps(-0.08131f);
        for (; i < n; i += 4) {
            __m128 r, g, b;
            if (comp == 4) {
                __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i * 4)), m = _mm_set1_epi32(255);
                r = _mm_cvtepi32_ps(_mm_and_si128(px, m));
                g = _mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(px, 8), m));
                b = _mm_cvtepi32_ps(_mm_and_si128(_mm_srli_epi32(px, 16), m));
            } else {
                unsigned w0, w1, w2;  // r0 g0 b0 r1 | g1 b1 r2 g2 | b2 r3 g3 b3
                std::memcpy(&w0, src + i * 3, 4);
                std::memcpy(&w1, src + i * 3 + 4, 4);
                std::memcpy(&w2, src + i * 3 + 8, 4);
                r = _mm_cvtepi32_ps(_mm_setr_epi32(int(w0 & 255), int(w0 >> 24), int((w1 >> 16) & 255), int((w2 >> 8) & 255)));
                g = _mm_cvtepi32_ps(_mm_setr_epi32(int((w0 >> 8) & 255), int(w1 & 255), int(w1 >> 24), int((w2 >> 16) & 255)));
                b = _mm_cvtepi32_ps(_mm_setr_epi32(int((w0 >> 16) & 255), int((w1 >> 8) & 255), int(w2 & 255), int(w2 >> 24)));
            }
            _mm_storeu_ps(luma + i, _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(luma_r, r), _mm_mul_ps(luma_g, g)), _mm_mul_ps(luma_b, b)), c128));
            _mm_storeu_ps(cb + i, _mm_add_ps(_mm_add_ps(_mm_mul_ps(cb_r, r), _mm_mul_ps(cb_g, g)), _mm_mul_ps(cb_b, b)));
            _mm_storeu_ps(cr + i, _mm_add_ps(_mm_add_ps(_mm_mul_ps(cr_r, r), _mm_mul_ps(cr_g, g)), _mm_mul_ps(cr_b, b)));
        }
        return;
    }
#endif
    for (; i < n; ++i) {
        const float r = src[i * comp], g = src[i * comp + ofs_g], b = src[i * comp + ofs_b];
        luma[i] = +0.29900f * r + 0.58700f * g + 0.11400f * b - 128;
        cb[i] = -0.16874f * r - 0.33126f * g + 0.50000f * b;
        cr[i] = +0.50000f * r - 0.41869f * g - 0.08131f * b;
    }
}

class jpeg_encoder {
public:
    jpeg_encoder(const raster& image, int quality)
        : image_(image), dc_luma_(dc_luma_spec), ac_luma_(ac_luma_spec), dc_chroma_(dc_chroma_spec), ac_chroma_(ac_chroma_spec) {
        quality = quality ? quality : 90;
        subsample_ = quality <= 90;
        quality = std::clamp(quality, 1, 100);
        const int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
        for (int i = 0; i < 64; ++i) {
            luma_table_[zigzag[i]] = scaled(luma_quant[i], scale);
            chroma_table_[zigzag[i]] = scaled(chroma_quant[i], scale);
        }
        for (int row = 0, k = 0; row < 8; ++row) {
            for (int col = 0; col < 8; ++col, ++k) {
                luma_factor_[k] = 1 / (luma_table_[zigzag[k]] * aan_scale[row] * aan_scale[col]);
                chroma_factor_[k] = 1 / (chroma_table_[zigzag[k]] * aan_scale[row] * aan_scale[col]);
            }
        }
    }

    bytes encode() {
        bytes out;
        write_headers(out);
        bit_writer bits(out);
        if (subsample_) {
            encode_420(bits);
        } else {
            encode_444(bits);
        }
        bits.finish();
        byte_writer end(out);
        end.u8(0xFF);
        end.u8(0xD9);  // EOI
        return out;
    }

private:
    static std::uint8_t scaled(int base, int scale) {
        return static_cast<std::uint8_t>(std::clamp((base * scale + 50) / 100, 1, 255));
    }

    void write_headers(bytes& out) const {
        static const std::uint8_t soi_to_dqt[] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1,
                                                  0,    0,    1,    0,    1, 0,    0,   0xFF, 0xDB, 0, 0x84, 0};
        static const std::uint8_t start_of_scan[] = {0xFF, 0xDA, 0, 0xC, 3, 1, 0, 2, 0x11, 3, 0x11, 0, 0x3F, 0};
        const int w = image_.width, h = image_.height;
        const std::uint8_t start_of_frame[] = {0xFF, 0xC0, 0, 0x11, 8, std::uint8_t(h >> 8), std::uint8_t(h),
                                               std::uint8_t(w >> 8), std::uint8_t(w), 3, 1,
                                               std::uint8_t(subsample_ ? 0x22 : 0x11), 0, 2, 0x11, 1, 3, 0x11, 1,
                                               0xFF, 0xC4, 0x01, 0xA2, 0};  // ... and the DHT marker
        byte_writer wr(out);
        wr.raw(soi_to_dqt);
        wr.raw(luma_table_.data(), 64);
        wr.u8(1);
        wr.raw(chroma_table_.data(), 64);
        wr.raw(start_of_frame);
        write_huffman(wr, dc_luma_);
        wr.u8(0x10);
        write_huffman(wr, ac_luma_);
        wr.u8(1);
        write_huffman(wr, dc_chroma_);
        wr.u8(0x11);
        write_huffman(wr, ac_chroma_);
        wr.raw(start_of_scan);
    }

    static void write_huffman(byte_writer& wr, const huffman_table& t) {
        wr.raw(t.spec().counts.data(), 16);
        wr.raw(t.spec().symbols, t.spec().symbol_count);
    }

    // Level shift, DCT, quantise and entropy-code one 8x8 block; returns its DC value (the next
    // block's predictor).
    int encode_block(bit_writer& bits, const float* block, int stride, const float* factor, int prev_dc,
                     const huffman_table& dc, const huffman_table& ac) const {
        int du[64];
        {
            float a[64], b[64], q[64];
            int qi[64];
            transpose8(a, block, stride);  // a[x*8+y] = block[y*stride+x]
            dct_columns(a);                // a[u*8+y]: horizontal frequency u of row y
            transpose8(b, a, 8);           // b[y*8+x] = a[x*8+y]
            dct_columns(b);                // b[v*8+u]: vertical frequency v, horizontal u
            for (int k = 0; k < 64; ++k) q[k] = b[k] * factor[k];
            for (int k = 0; k < 64; ++k) qi[k] = static_cast<int>(q[k] < 0 ? q[k] - 0.5f : q[k] + 0.5f);
            for (int k = 0; k < 64; ++k) du[zigzag[k]] = qi[k];
        }

        const int diff = du[0] - prev_dc;
        if (diff == 0) {
            bits.put_code(dc[0]);
        } else {
            const int nb = bit_length(static_cast<unsigned>(diff < 0 ? -diff : diff));
            bits.put_value(dc[nb], diff, nb);
        }

        // AC coefficients: walk only the non-zero ones (bit k of nz is set when du[k] != 0)
        unsigned long long nz = 0;
        for (int i = 1; i < 64; ++i) nz |= static_cast<unsigned long long>(du[i] != 0) << i;
        int last = 0;  // index of the previous coefficient that was written (0 = the DC)
        while (nz) {
            const int k = count_trailing_zeros(nz), val = du[k];
            int zeroes = k - last - 1;
            const int nb = bit_length(static_cast<unsigned>(val < 0 ? -val : val));
            for (; zeroes >= 16; zeroes -= 16) bits.put_code(ac[0xF0]);
            bits.put_value(ac[(zeroes << 4) + nb], val, nb);
            last = k;
            nz &= nz - 1;
        }
        if (last != 63) bits.put_code(ac[0x00]);  // end of block
        return du[0];
    }

    // YCbCr of the size x size block whose top left pixel is (x, y); rows and columns past the
    // image repeat its last row and column.
    void load_block(int x, int y, int size, float* luma, float* cb, float* cr) const {
        const int w = image_.width, h = image_.height, comp = image_.channels;
        const int ofs_g = comp > 2 ? 1 : 0, ofs_b = comp > 2 ? 2 : 0;  // comp 2 is gray + alpha: alpha is ignored
        const std::uint8_t* data = image_.pixels;
        int pos = 0;
        for (int row = y; row < y + size; ++row) {
            const int clamped_row = row < h ? row : h - 1;
            const std::size_t base = std::size_t(clamped_row) * std::size_t(w) * std::size_t(comp);
            if (x + size <= w) {  // a run inside the image needs no per-pixel column clamp
                convert_run(luma + pos, cb + pos, cr + pos, data + base + std::size_t(x) * comp, ofs_g, ofs_b, comp, size);
                pos += size;
            } else {
                for (int col = x; col < x + size; ++col, ++pos) {
                    const std::size_t p = base + std::size_t(col < w ? col : w - 1) * comp;
                    const float r = data[p], g = data[p + ofs_g], b = data[p + ofs_b];
                    luma[pos] = +0.29900f * r + 0.58700f * g + 0.11400f * b - 128;
                    cb[pos] = -0.16874f * r - 0.33126f * g + 0.50000f * b;
                    cr[pos] = +0.50000f * r - 0.41869f * g - 0.08131f * b;
                }
            }
        }
    }

    // 4:2:0: a 16x16 pixel block is four luma blocks and one (averaged) block each of cb and cr.
    void encode_420(bit_writer& bits) const {
        int dc_y = 0, dc_u = 0, dc_v = 0;
        for (int y = 0; y < image_.height; y += 16) {
            for (int x = 0; x < image_.width; x += 16) {
                float luma[256], cb[256], cr[256];
                load_block(x, y, 16, luma, cb, cr);
                dc_y = encode_block(bits, luma + 0, 16, luma_factor_.data(), dc_y, dc_luma_, ac_luma_);
                dc_y = encode_block(bits, luma + 8, 16, luma_factor_.data(), dc_y, dc_luma_, ac_luma_);
                dc_y = encode_block(bits, luma + 128, 16, luma_factor_.data(), dc_y, dc_luma_, ac_luma_);
                dc_y = encode_block(bits, luma + 136, 16, luma_factor_.data(), dc_y, dc_luma_, ac_luma_);
                float sub_u[64], sub_v[64];
                for (int yy = 0, pos = 0; yy < 8; ++yy) {
                    for (int xx = 0; xx < 8; ++xx, ++pos) {
                        const int j = yy * 32 + xx * 2;
                        sub_u[pos] = (cb[j + 0] + cb[j + 1] + cb[j + 16] + cb[j + 17]) * 0.25f;
                        sub_v[pos] = (cr[j + 0] + cr[j + 1] + cr[j + 16] + cr[j + 17]) * 0.25f;
                    }
                }
                dc_u = encode_block(bits, sub_u, 8, chroma_factor_.data(), dc_u, dc_chroma_, ac_chroma_);
                dc_v = encode_block(bits, sub_v, 8, chroma_factor_.data(), dc_v, dc_chroma_, ac_chroma_);
            }
        }
    }

    // 4:4:4: one block each of luma, cb and cr per 8x8 pixels.
    void encode_444(bit_writer& bits) const {
        int dc_y = 0, dc_u = 0, dc_v = 0;
        for (int y = 0; y < image_.height; y += 8) {
            for (int x = 0; x < image_.width; x += 8) {
                float luma[64], cb[64], cr[64];
                load_block(x, y, 8, luma, cb, cr);
                dc_y = encode_block(bits, luma, 8, luma_factor_.data(), dc_y, dc_luma_, ac_luma_);
                dc_u = encode_block(bits, cb, 8, chroma_factor_.data(), dc_u, dc_chroma_, ac_chroma_);
                dc_v = encode_block(bits, cr, 8, chroma_factor_.data(), dc_v, dc_chroma_, ac_chroma_);
            }
        }
    }

    raster image_;
    bool subsample_ = true;
    std::array<std::uint8_t, 64> luma_table_{}, chroma_table_{};   // quantisers, in zigzag order as the file stores them
    std::array<float, 64> luma_factor_{}, chroma_factor_{};         // their reciprocals with the DCT scale folded in
    huffman_table dc_luma_, ac_luma_, dc_chroma_, ac_chroma_;
};

}  // namespace

bytes encode_jpeg(const raster& image, int quality) { return jpeg_encoder(image, quality).encode(); }

}  // namespace stb::codec
