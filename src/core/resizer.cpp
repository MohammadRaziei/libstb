#include "libstb/resizer.hpp"

#include <climits>
#include <cstdint>
#include <stdexcept>
#include <string>

// STATIC: internal linkage for every stbir_* function (see image.cpp).
#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

namespace libstb {
namespace {

stbir_filter to_stb(resize_filter f) {
    switch (f) {
        case resize_filter::automatic: return STBIR_FILTER_DEFAULT;
        case resize_filter::box: return STBIR_FILTER_BOX;
        case resize_filter::triangle: return STBIR_FILTER_TRIANGLE;
        case resize_filter::cubic_bspline: return STBIR_FILTER_CUBICBSPLINE;
        case resize_filter::catmull_rom: return STBIR_FILTER_CATMULLROM;
        case resize_filter::mitchell: return STBIR_FILTER_MITCHELL;
        case resize_filter::point: return STBIR_FILTER_POINT_SAMPLE;
    }
    throw std::invalid_argument("invalid resize_filter");
}

stbir_edge to_stb(resize_edge e) {
    switch (e) {
        case resize_edge::clamp: return STBIR_EDGE_CLAMP;
        case resize_edge::reflect: return STBIR_EDGE_REFLECT;
        case resize_edge::wrap: return STBIR_EDGE_WRAP;
        case resize_edge::zero: return STBIR_EDGE_ZERO;
    }
    throw std::invalid_argument("invalid resize_edge");
}

// Alpha layouts are the non-premultiplied ones: stb weights colour by alpha.
stbir_pixel_layout layout_for(int channels) {
    switch (channels) {
        case 1: return STBIR_1CHANNEL;
        case 2: return STBIR_RA;
        case 3: return STBIR_RGB;
        default: return STBIR_RGBA;
    }
}

}  // namespace

resizer::resizer(const resize_options& options) : options_(options) {
    (void)to_stb(options.filter);  // reject out-of-range enum values up front
    (void)to_stb(options.edge);
}

image resizer::resize(const image& src, int width, int height) const {
    if (src.empty()) throw std::invalid_argument("cannot resize an empty image");
    if (width < 1 || height < 1) throw std::invalid_argument("target width and height must be >= 1");

    const std::uint64_t out_stride = std::uint64_t(width) * std::uint64_t(src.channels());
    const std::uint64_t out_bytes = out_stride * std::uint64_t(height);  // <= 2^31 * 4 * 2^31 = 2^64: check below first
    if (out_stride > INT_MAX || src.stride() > std::size_t(INT_MAX) ||
        std::uint64_t(width) * std::uint64_t(height) > std::uint64_t(INT_MAX) ||
        out_bytes > std::uint64_t(INT_MAX) || out_bytes > options_.max_bytes)
        throw limit_error("resized image too large: " + std::to_string(width) + "x" +
                          std::to_string(height) + "x" + std::to_string(src.channels()));

    image out(width, height, src.channels());

    STBIR_RESIZE r;
    stbir_resize_init(&r, src.data(), src.width(), src.height(), static_cast<int>(src.stride()),
                      out.data(), width, height, static_cast<int>(out_stride),
                      layout_for(src.channels()),
                      options_.srgb ? STBIR_TYPE_UINT8_SRGB : STBIR_TYPE_UINT8);
    stbir_set_edgemodes(&r, to_stb(options_.edge), to_stb(options_.edge));
    stbir_set_filters(&r, to_stb(options_.filter), to_stb(options_.filter));
    if (!stbir_resize_extended(&r)) throw error("resize failed (out of memory?)");
    return out;
}

}  // namespace libstb
