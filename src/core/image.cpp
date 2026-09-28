#include "libstb/image.hpp"

#include <climits>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

// STATIC: every stbi_* function gets internal linkage in this TU.
// NO_STDIO: we only decode from memory (callers read files themselves), which
// removes fopen/FILE code from the attack surface and Windows path quirks.
// MAX_DIMENSIONS: hard cap on width/height; stb_image has been fuzzed into
// many bugs, so reject absurd headers before any decoding work happens.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS (1 << 16)
#include "stb_image.h"

namespace libstb {
namespace {

int checked_size(const void* data, std::size_t size) {
    if (!data) throw std::invalid_argument("image data is null");
    if (size == 0 || size > static_cast<std::size_t>(INT_MAX))
        throw std::invalid_argument("image data size must be in 1..INT_MAX");
    return static_cast<int>(size);
}

[[noreturn]] void fail(const char* what) {
    const char* why = stbi_failure_reason();
    throw std::runtime_error(std::string(what) + ": " + (why ? why : "unknown error"));
}

struct stb_free {
    void operator()(stbi_uc* p) const { stbi_image_free(p); }
};

}  // namespace

image_info info(const void* data, std::size_t size) {
    const int n = checked_size(data, size);
    image_info i;
    if (!stbi_info_from_memory(static_cast<const stbi_uc*>(data), n, &i.width, &i.height,
                               &i.channels))
        fail("cannot read image header");
    return i;
}

image load(const void* data, std::size_t size, const load_options& opt) {
    if (opt.channels < 0 || opt.channels > 4)
        throw std::invalid_argument("channels must be in 0..4");

    // Size check from the header alone: no pixel memory is allocated for a
    // hostile "60000x60000" file. width/height are <= 1<<16 and channels <= 4,
    // so the uint64 product cannot overflow.
    const image_info hdr = info(data, size);
    const int want = opt.channels ? opt.channels : hdr.channels;
    const std::uint64_t bytes = std::uint64_t(hdr.width) * std::uint64_t(hdr.height) * std::uint64_t(want);
    if (bytes > opt.max_bytes)
        throw std::runtime_error("image too large: " + std::to_string(hdr.width) + "x" +
                                 std::to_string(hdr.height) + "x" + std::to_string(want) +
                                 " exceeds max_bytes");

    // The _thread variant: stb's plain setter is a process-wide global.
    stbi_set_flip_vertically_on_load_thread(opt.flip ? 1 : 0);

    int w = 0, h = 0, c = 0;
    std::unique_ptr<stbi_uc, stb_free> px(stbi_load_from_memory(
        static_cast<const stbi_uc*>(data), checked_size(data, size), &w, &h, &c, opt.channels));
    if (!px) fail("cannot decode image");

    image out;
    out.width = w;
    out.height = h;
    out.channels = want;
    // ponytail: one memcpy into a vector so callers own plain C++ memory and
    // stb never leaks out. Zero-copy (custom deleter) is the upgrade path if
    // profiling ever shows this copy matters.
    out.data.assign(px.get(), px.get() + std::size_t(w) * std::size_t(h) * std::size_t(want));
    return out;
}

}  // namespace libstb
