#pragma once

// Internal. What every format decoder shares: byte types, the input stream, error reporting and
// checked allocation, pixel conversion, and the entry points of each format (one namespace per
// format, defined in its own .cpp). Derived from stb_image.h v2.30 (public domain / MIT, Sean
// Barrett and contributors), restructured into one file per format; the algorithms are unchanged.
//
//   formats.hpp        this header
//   stream.hpp-like    the stream class below
//   dispatch.cpp       tries the formats in order and post-processes the pixels (flip, 8 bit)
//   convert.cpp        channel-count conversion (gray, gray + alpha, RGB, RGBA)
//   inflate.*          the zlib decoder; jpeg.cpp, png.cpp, gif.cpp, bmp.cpp, tga.cpp, psd.cpp,
//                      pic.cpp, pnm.cpp, hdr.cpp: one format each

#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace stb::codec {
// Grows a zlib output buffer, refusing to pass the ceiling set by zlib_inflate (decode.cpp).
void* realloc_with_ceiling(void* p, std::size_t new_size);
}  // namespace stb::codec

namespace stb::codec::detail {

using byte = std::uint8_t;

// A hard cap on width and height: absurd headers are rejected before anything is allocated.
constexpr int max_dimensions = 1 << 16;

// The number of channels a caller can ask for (0 = the file's own).
enum channels_wanted { ch_default = 0, ch_grey = 1, ch_grey_alpha = 2, ch_rgb = 3, ch_rgb_alpha = 4 };

// A block of memory with a read cursor: the one input of every format decoder. Reads past the
// end give 0 (get8 and friends) or fail (getn), so a truncated file never reads outside it.
class stream {
public:
    // what the decoders learn about the image as they parse its header
    std::uint32_t img_x = 0, img_y = 0;
    int img_n = 0, img_out_n = 0;

    stream(const byte* data, int length) : begin_(data), cur_(data), end_(data + length) {}

    byte get8() { return cur_ < end_ ? *cur_++ : 0; }
    int at_eof() const { return cur_ >= end_; }

    void skip(int n) {
        if (n == 0) return;
        if (n < 0) {
            cur_ = end_;
            return;
        }
        cur_ += n;
    }

    // Copies the next n bytes; 0 (and nothing copied) if fewer than n are left.
    int getn(byte* out, int n) {
        if (cur_ + n <= end_) {
            std::memcpy(out, cur_, std::size_t(n));
            cur_ += n;
            return 1;
        }
        return 0;
    }

    int get16be() {
        int z = get8();
        return (z << 8) + get8();
    }
    std::uint32_t get32be() {
        std::uint32_t z = std::uint32_t(get16be());
        return (z << 16) + std::uint32_t(get16be());
    }
    int get16le() {
        int z = get8();
        return z + (get8() << 8);
    }
    std::uint32_t get32le() {
        std::uint32_t z = std::uint32_t(get16le());
        z += std::uint32_t(get16le()) << 16;
        return z;
    }

    // Back to the start. (Used after a format's "test", which looks at no more than ~92 bytes.)
    void rewind() { cur_ = begin_; }
    // How many bytes were read so far.
    int tell() const { return int(cur_ - begin_); }

private:
    const byte* begin_;
    const byte* cur_;
    const byte* end_;
};

enum channel_order { order_rgb, order_bgr };

// What a decoder reports besides the pixels.
struct result_info {
    int bits_per_channel;
    int num_channels;
    int channel_order;
};

// Which part of a file a decoder reads: everything, just the type check, or the header.
enum scan_mode { scan_load = 0, scan_type, scan_header };

// ---- errors: the reason of the last failure, per thread, as a message a user can read

inline thread_local const char* failure_message;

inline const char* failure_reason() { return failure_message; }

// Records a failure and returns 0. `code` is the short internal name of the problem, `message` what
// the caller sees.
inline int fail(const char* /*code*/, const char* message) {
    failure_message = message;
    return 0;
}
// The same, for decoders that return a pointer.
inline byte* fail_bytes(const char* code, const char* message) {
    fail(code, message);
    return nullptr;
}
inline float* fail_float(const char* code, const char* message) {
    fail(code, message);
    return nullptr;
}

// ---- checked allocation. stb_image uses ints pervasively, including for offset calculations, so the
// largest decoded image is INT_MAX bytes; these helpers make sure the size arithmetic cannot
// overflow (negative terms are invalid too).

inline void* alloc(std::size_t size) { return std::malloc(size); }
inline void* realloc_sized(void* p, std::size_t /*old_size*/, std::size_t new_size) {
    return ::stb::codec::realloc_with_ceiling(p, new_size);
}
inline byte truncate_to_byte(int x) { return static_cast<byte>(x & 255); }

// true if a + b fits in an int
inline int addsizes_valid(int a, int b) {
    if (b < 0) return 0;
    return a <= INT_MAX - b;  // a + b <= INT_MAX, written so it cannot overflow
}
// true if a * b fits in an int
inline int mul2sizes_valid(int a, int b) {
    if (a < 0 || b < 0) return 0;
    if (b == 0) return 1;
    return a <= INT_MAX / b;
}
// true if a * b + add fits
inline int mad2sizes_valid(int a, int b, int add) { return mul2sizes_valid(a, b) && addsizes_valid(a * b, add); }
inline int mad3sizes_valid(int a, int b, int c, int add) {
    return mul2sizes_valid(a, b) && mul2sizes_valid(a * b, c) && addsizes_valid(a * b * c, add);
}
inline int mad4sizes_valid(int a, int b, int c, int d, int add) {
    return mul2sizes_valid(a, b) && mul2sizes_valid(a * b, c) && mul2sizes_valid(a * b * c, d) &&
           addsizes_valid(a * b * c * d, add);
}
inline void* alloc_mad2(int a, int b, int add) {
    if (!mad2sizes_valid(a, b, add)) return nullptr;
    return alloc(std::size_t(a) * std::size_t(b) + std::size_t(add));
}
inline void* alloc_mad3(int a, int b, int c, int add) {
    if (!mad3sizes_valid(a, b, c, add)) return nullptr;
    return alloc(std::size_t(a) * std::size_t(b) * std::size_t(c) + std::size_t(add));
}
inline void* alloc_mad4(int a, int b, int c, int d, int add) {
    if (!mad4sizes_valid(a, b, c, d, add)) return nullptr;
    return alloc(std::size_t(a) * std::size_t(b) * std::size_t(c) * std::size_t(d) + std::size_t(add));
}
// true if a + b of two signed ints does not overflow
inline int addints_valid(int a, int b) {
    if ((a >= 0) != (b >= 0)) return 1;  // different signs: no overflow
    if (a < 0 && b < 0) return a >= INT_MIN - b;
    return a <= INT_MAX - b;
}
// true if a * b fits in a signed short
inline int mul2shorts_valid(int a, int b) {
    if (b == 0 || b == -1) return 1;  // by 0 is always 0; -1 is checked so SHRT_MIN / b cannot overflow
    if ((a >= 0) == (b >= 0)) return a <= SHRT_MAX / b;
    if (b < 0) return a <= SHRT_MIN / b;
    return a >= SHRT_MIN / b;
}

// ---- pixel conversion (convert.cpp)

byte compute_y(int r, int g, int b);
byte* convert_format(byte* data, int img_n, int req_comp, unsigned int x, unsigned int y);
std::uint16_t* convert_format16(std::uint16_t* data, int img_n, int req_comp, unsigned int x, unsigned int y);
byte* hdr_to_ldr(float* data, int x, int y, int comp);  // hdr.cpp

// ---- the whole-image entry points (dispatch.cpp)

void set_flip_on_load(int flip);  // per thread: flip the next decodes vertically
byte* load_from_memory(const byte* buffer, int len, int* x, int* y, int* comp, int req_comp);
int info_from_memory(const byte* buffer, int len, int* x, int* y, int* comp);
// zlib (inflate.cpp); the caller frees the result with std::free
char* zlib_decode_malloc(const char* buffer, int len, int initial_size, int* outlen, int parse_header);

// ---- the entry points of each format, one namespace each

namespace jpeg {
  void * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri);
  int test(stream *s);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace png {
  void * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri);
  int test(stream *s);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace bmp {
  int test(stream *s);
  void * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace gif {
  int test(stream *s);
  void * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace psd {
  int test(stream *s);
  void * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri, int bpc);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace pic {
  void * load(stream *s, int *px, int *py, int *comp, int req_comp, result_info *ri);
  int test(stream *s);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace pnm {
  int test(stream *s);
  void * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace hdr {
  int test(stream* s);
  float * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri);
  int info(stream *s, int *x, int *y, int *comp);
}

namespace tga {
  int info(stream *s, int *x, int *y, int *comp);
  int test(stream *s);
  void * load(stream *s, int *x, int *y, int *comp, int req_comp, result_info *ri);
}

}  // namespace stb::codec::detail
