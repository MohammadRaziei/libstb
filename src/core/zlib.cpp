#include "stb/zlib.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include "stb/error.hpp"
#include "zlib_bridge.hpp"

namespace stb {
namespace {

constexpr std::size_t kIntMax = static_cast<std::size_t>(INT_MAX);

struct free_deleter {
    void operator()(unsigned char* p) const { std::free(p); }
};
using malloc_buffer = std::unique_ptr<unsigned char, free_deleter>;

std::uint32_t adler32(const unsigned char* p, std::size_t n) {
    std::uint32_t a = 1, b = 0;
    while (n > 0) {
        const std::size_t k = std::min<std::size_t>(n, 5552);  // largest run that cannot overflow
        for (std::size_t i = 0; i < k; ++i) {
            a += p[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
        p += k;
        n -= k;
    }
    return (b << 16) | a;
}

void check_args(const void* data, std::size_t size) {
    if (!data && size != 0) throw std::invalid_argument("data is null");
    if (size > kIntMax) throw limit_error("zlib input larger than 2 GiB");
}

}  // namespace

namespace {

// Level 0: zlib header, stored blocks of up to 65535 bytes, Adler-32.
std::vector<std::uint8_t> zlib_store(const unsigned char* data, std::size_t size) {
    std::vector<std::uint8_t> out;
    out.reserve(size + 6 + 5 * (size / 65535 + 1));
    out.push_back(0x78);
    out.push_back(0x01);  // deflate, 32 KiB window, "fastest" flag; (0x78 << 8 | 0x01) % 31 == 0
    std::size_t pos = 0;
    do {
        const std::size_t n = std::min<std::size_t>(size - pos, 65535);
        const bool last = pos + n == size;
        out.push_back(last ? 1 : 0);  // BFINAL, BTYPE = 00 (stored)
        out.push_back(std::uint8_t(n));
        out.push_back(std::uint8_t(n >> 8));
        out.push_back(std::uint8_t(~n));
        out.push_back(std::uint8_t(~n >> 8));
        out.insert(out.end(), data + pos, data + pos + n);
        pos += n;
    } while (pos < size);
    const std::uint32_t a = adler32(data, size);
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(std::uint8_t(a >> shift));
    return out;
}

}  // namespace

std::vector<std::uint8_t> zlib_compress(const void* data, std::size_t size, int level) {
    if (level < -1 || level > 9) throw std::invalid_argument("level must be in -1..9");
    if (level == -1) level = default_zlib_level;
    check_args(data, size);
    if (level == 0) return zlib_store(static_cast<const unsigned char*>(data), size);
    if (size == 0) return {0x78, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01};  // what zlib gives for ""

    // stb's effort table has 11 entries; 1..8 map to themselves and 9 to its strongest (10).
    const int quality = level == 9 ? 10 : level;
    int out_size = 0;
    malloc_buffer out(
        detail::stb_deflate(static_cast<const unsigned char*>(data), static_cast<int>(size), quality, &out_size));
    if (!out) throw encode_error("zlib compression failed (out of memory)");
    return std::vector<std::uint8_t>(out.get(), out.get() + out_size);
}

std::vector<std::uint8_t> zlib_compress(const std::vector<std::uint8_t>& data, int level) {
    return zlib_compress(data.data(), data.size(), level);
}

std::vector<std::uint8_t> zlib_decompress(const void* data, std::size_t size, std::size_t max_bytes) {
    check_args(data, size);
    if (size == 0) throw decode_error("zlib: empty input");
    const auto* in = static_cast<const unsigned char*>(data);
    const std::size_t limit = std::min(max_bytes, kIntMax);

    // stb inflates into a buffer that starts at `initial` and doubles. Choose `initial` so that
    // after k doublings the buffer lands on `limit` (rounded up), not up to twice past it: start
    // near 4x the input (a typical ratio for text), then walk down by halves until it divides
    // `limit` evenly enough. The buffer is then never allowed past that rounded-up size.
    const std::size_t guess = std::min(limit, std::max<std::size_t>(size * 4, std::size_t(1) << 16));
    unsigned k = 0;
    while (((limit + (std::size_t(1) << k) - 1) >> k) > guess) ++k;
    const std::size_t initial = std::max<std::size_t>((limit + (std::size_t(1) << k) - 1) >> k, 1);
    const std::size_t ceiling = initial << k;

    int n = 0;
    detail::inflate_status status{};
    malloc_buffer out(detail::stb_inflate(in, static_cast<int>(size), initial, ceiling, &n, &status));
    switch (status) {
        case detail::inflate_status::ok: break;
        case detail::inflate_status::too_large:
            throw limit_error("zlib output exceeds max_bytes (" + std::to_string(limit) + ")");
        case detail::inflate_status::out_of_memory: throw std::bad_alloc();
        case detail::inflate_status::corrupt: throw decode_error("zlib: the stream is corrupt, truncated or not zlib");
    }
    if (n < 0 || static_cast<std::size_t>(n) > limit)  // fit the rounded-up buffer, but not the limit
        throw limit_error("zlib output exceeds max_bytes (" + std::to_string(limit) + ")");

    // stb does not look at the Adler-32 trailer: check it, and with it that the input ends where
    // the stream ends (a truncated stream cannot match by accident).
    const std::uint32_t want = size >= 4 ? (std::uint32_t(in[size - 4]) << 24) | (std::uint32_t(in[size - 3]) << 16) |
                                               (std::uint32_t(in[size - 2]) << 8) | std::uint32_t(in[size - 1])
                                         : 0;
    if (size < 6 || want != adler32(out.get(), static_cast<std::size_t>(n)))
        throw decode_error("zlib: checksum mismatch, or the stream is truncated or followed by other data");
    return std::vector<std::uint8_t>(out.get(), out.get() + n);
}

std::vector<std::uint8_t> zlib_decompress(const std::vector<std::uint8_t>& data, std::size_t max_bytes) {
    return zlib_decompress(data.data(), data.size(), max_bytes);
}

}  // namespace stb
