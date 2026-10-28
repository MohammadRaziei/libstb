// The decoding entry points of the codec module, on top of the format decoders in decode/. It also
// serves the public zlib API through the inflater.
//
// The zlib output buffer grows through realloc_with_ceiling, a hook that lets zlib_inflate put a
// ceiling on it when it serves the public zlib API.

#include <cstdlib>
#include <string>

#include "codec/codec.hpp"
#include "codec/decode/formats.hpp"
#include "stb/error.hpp"

namespace stb::codec {
namespace {

// The ceiling in force on this thread while zlib_inflate runs (0 = none: every other use).
thread_local std::size_t t_ceiling = 0;
thread_local bool t_refused = false;

[[noreturn]] void fail_decode(const char* what) {
    const char* why = detail::failure_reason();
    throw decode_error(std::string(what) + ": " + (why ? why : "unknown error"));
}

}  // namespace

void* realloc_with_ceiling(void* p, std::size_t new_size) {
    if (t_ceiling != 0 && new_size > t_ceiling) {
        t_refused = true;
        return nullptr;  // the decoder reports it as "out of memory" and frees the old block itself
    }
    return std::realloc(p, new_size);
}

void free_deleter::operator()(void* p) const noexcept { std::free(p); }

image_header read_header(const std::uint8_t* data, std::size_t size) {
    image_header h;
    if (!detail::info_from_memory(data, static_cast<int>(size), &h.width, &h.height, &h.channels))
        fail_decode("cannot read image header");
    return h;
}

decoded decode(const std::uint8_t* data, std::size_t size, int want_channels, bool flip) {
    // per thread: other threads decoding at the same time are not affected
    detail::set_flip_on_load(flip ? 1 : 0);

    decoded d;
    int file_channels = 0;
    d.pixels.reset(detail::load_from_memory(data, static_cast<int>(size), &d.width, &d.height, &file_channels,
                                         want_channels));
    if (!d.pixels) fail_decode("cannot decode image");
    d.channels = want_channels ? want_channels : file_channels;
    return d;
}

inflated zlib_inflate(const std::uint8_t* in, std::size_t size, std::size_t initial, std::size_t ceiling) {
    t_ceiling = ceiling;
    t_refused = false;
    int n = 0;
    char* out = detail::zlib_decode_malloc(reinterpret_cast<const char*>(in), static_cast<int>(size),
                                                             static_cast<int>(initial), &n, 1);
    const bool refused = t_refused;
    t_ceiling = 0;

    inflated r;
    if (out) {
        r.data.assign(reinterpret_cast<std::uint8_t*>(out), reinterpret_cast<std::uint8_t*>(out) + n);
        std::free(out);
        return r;
    }
    const char* why = detail::failure_reason();
    r.status = refused                                  ? inflate_status::too_large
               : (why && std::string(why) == "Out of memory") ? inflate_status::out_of_memory
                                                              : inflate_status::corrupt;
    return r;
}

}  // namespace stb::codec
