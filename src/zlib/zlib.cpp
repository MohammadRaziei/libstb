#include "stb/zlib.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

#include "stb/error.hpp"
#include "codec/codec.hpp"

namespace stb {
namespace {

constexpr std::size_t int_max = static_cast<std::size_t>(INT_MAX);

void check_args(const void* data, std::size_t size) {
    if (!data && size != 0) throw std::invalid_argument("data is null");
    if (size > int_max) throw limit_error("zlib input larger than 2 GiB");
}

}  // namespace

std::vector<std::uint8_t> zlib_compress(const void* data, std::size_t size, int level) {
    if (level < -1 || level > 9) throw std::invalid_argument("level must be in -1..9");
    if (level == -1) level = default_zlib_level;
    check_args(data, size);
    if (level == 0) return codec::zlib_store(static_cast<const std::uint8_t*>(data), size);
    if (size == 0) return {0x78, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01};  // what zlib gives for ""

    // The compressor's effort table has 11 entries: 1..8 map to themselves and 9 to its strongest (10).
    const int effort = level == 9 ? 10 : level;
    return codec::zlib_deflate(static_cast<const std::uint8_t*>(data), size, effort);
}

std::vector<std::uint8_t> zlib_compress(const std::vector<std::uint8_t>& data, int level) {
    return zlib_compress(data.data(), data.size(), level);
}

std::vector<std::uint8_t> zlib_decompress(const void* data, std::size_t size, std::size_t max_bytes) {
    check_args(data, size);
    if (size == 0) throw decode_error("zlib: empty input");
    const auto* in = static_cast<const unsigned char*>(data);
    const std::size_t limit = std::min(max_bytes, int_max);

    // stb inflates into a buffer that starts at `initial` and doubles. Choose `initial` so that
    // after k doublings the buffer lands on `limit` (rounded up), not up to twice past it: start
    // near 4x the input (a typical ratio for text), then walk down by halves until it divides
    // `limit` evenly enough. The buffer is then never allowed past that rounded-up size.
    const std::size_t guess = std::min(limit, std::max<std::size_t>(size * 4, std::size_t(1) << 16));
    unsigned k = 0;
    while (((limit + (std::size_t(1) << k) - 1) >> k) > guess) ++k;
    const std::size_t initial = std::max<std::size_t>((limit + (std::size_t(1) << k) - 1) >> k, 1);
    const std::size_t ceiling = initial << k;

    codec::inflated out = codec::zlib_inflate(in, size, initial, ceiling);
    switch (out.status) {
        case codec::inflate_status::ok: break;
        case codec::inflate_status::too_large:
            throw limit_error("zlib output exceeds max_bytes (" + std::to_string(limit) + ")");
        case codec::inflate_status::out_of_memory: throw std::bad_alloc();
        case codec::inflate_status::corrupt: throw decode_error("zlib: the stream is corrupt, truncated or not zlib");
    }
    if (out.data.size() > limit)  // fit the rounded-up buffer, but not the limit
        throw limit_error("zlib output exceeds max_bytes (" + std::to_string(limit) + ")");

    // stb does not look at the Adler-32 trailer: check it, and with it that the input ends where
    // the stream ends (a truncated stream cannot match by accident).
    const std::uint32_t want = size >= 4 ? (std::uint32_t(in[size - 4]) << 24) | (std::uint32_t(in[size - 3]) << 16) |
                                               (std::uint32_t(in[size - 2]) << 8) | std::uint32_t(in[size - 1])
                                         : 0;
    if (size < 6 || want != codec::adler32(out.data.data(), out.data.size()))
        throw decode_error("zlib: checksum mismatch, or the stream is truncated or followed by other data");
    return std::move(out.data);
}

std::vector<std::uint8_t> zlib_decompress(const std::vector<std::uint8_t>& data, std::size_t max_bytes) {
    return zlib_decompress(data.data(), data.size(), max_bytes);
}

}  // namespace stb
