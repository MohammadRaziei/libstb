// Decoding: the one translation unit that compiles stb_image.h, so no stbi_* symbol leaves the
// library (it is safe to link next to your own copy of stb). It also serves the public zlib API
// through stb's inflater.
//
// STB_IMAGE_STATIC gives every stbi_* function internal linkage here. STBI_NO_STDIO: only memory
// is decoded (files are read by detail::read_file), which removes fopen/FILE code from the
// attack surface and Windows path quirks. STBI_MAX_DIMENSIONS: a hard cap on width and height;
// stb_image has been fuzzed into many bugs, so absurd headers are rejected before any decoding.
//
// stb_image grows its zlib output buffer through STBI_REALLOC_SIZED, one of stb's documented
// customisation points (all three allocator macros go together). It is routed through a hook so
// zlib_inflate can put a ceiling on that buffer when it serves the public zlib API.

#include <cstdlib>
#include <string>

#include "codec/codec.hpp"
#include "stb/error.hpp"

namespace stb::codec {
void* realloc_with_ceiling(void* p, std::size_t new_size);
}

#define STBI_MALLOC(sz) std::malloc(sz)
#define STBI_REALLOC_SIZED(p, oldsz, newsz) ::stb::codec::realloc_with_ceiling(p, newsz)
#define STBI_FREE(p) std::free(p)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS (1 << 16)
#include "stb_image.h"

namespace stb::codec {
namespace {

// The ceiling in force on this thread while zlib_inflate runs (0 = none: every other use).
thread_local std::size_t t_ceiling = 0;
thread_local bool t_refused = false;

[[noreturn]] void fail_decode(const char* what) {
    const char* why = stbi_failure_reason();
    throw decode_error(std::string(what) + ": " + (why ? why : "unknown error"));
}

const stbi_uc* as_stb(const std::uint8_t* p) { return reinterpret_cast<const stbi_uc*>(p); }

}  // namespace

void* realloc_with_ceiling(void* p, std::size_t new_size) {
    if (t_ceiling != 0 && new_size > t_ceiling) {
        t_refused = true;
        return nullptr;  // stb reports it as "out of memory" and frees the old block itself
    }
    return std::realloc(p, new_size);
}

void free_deleter::operator()(void* p) const noexcept { std::free(p); }

image_header read_header(const std::uint8_t* data, std::size_t size) {
    image_header h;
    if (!stbi_info_from_memory(as_stb(data), static_cast<int>(size), &h.width, &h.height, &h.channels))
        fail_decode("cannot read image header");
    return h;
}

decoded decode(const std::uint8_t* data, std::size_t size, int want_channels, bool flip) {
    // The _thread variant: stb's plain setter is a process-wide global.
    stbi_set_flip_vertically_on_load_thread(flip ? 1 : 0);

    decoded d;
    int file_channels = 0;
    d.pixels.reset(stbi_load_from_memory(as_stb(data), static_cast<int>(size), &d.width, &d.height, &file_channels,
                                         want_channels));
    if (!d.pixels) fail_decode("cannot decode image");
    d.channels = want_channels ? want_channels : file_channels;
    return d;
}

inflated zlib_inflate(const std::uint8_t* in, std::size_t size, std::size_t initial, std::size_t ceiling) {
    t_ceiling = ceiling;
    t_refused = false;
    int n = 0;
    char* out = stbi_zlib_decode_malloc_guesssize_headerflag(reinterpret_cast<const char*>(in), static_cast<int>(size),
                                                             static_cast<int>(initial), &n, 1);
    const bool refused = t_refused;
    t_ceiling = 0;

    inflated r;
    if (out) {
        r.data.assign(reinterpret_cast<std::uint8_t*>(out), reinterpret_cast<std::uint8_t*>(out) + n);
        std::free(out);
        return r;
    }
    const char* why = stbi_failure_reason();
    r.status = refused                                  ? inflate_status::too_large
               : (why && std::string(why) == "Out of memory") ? inflate_status::out_of_memory
                                                              : inflate_status::corrupt;
    return r;
}

}  // namespace stb::codec
