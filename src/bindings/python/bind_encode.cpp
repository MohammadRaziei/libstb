#include "binding.hpp"

using namespace nb::literals;

namespace {

// Encodes with the GIL released; `fn` is the image member to call.
template <class Fn>
nb::bytes encode_with(const image_in& pixels, Fn&& fn) {
    const stb::image img = to_image(pixels);
    std::vector<std::uint8_t> out;
    {
        nb::gil_scoped_release release;
        out = fn(img);
    }
    return nb::bytes(reinterpret_cast<const char*>(out.data()), out.size());
}

}  // namespace

// image::to_png / to_jpg / to_bmp / to_tga as module functions on a uint8
// array; the Python Image class wraps them (files are written in Python).
void bind_encode(nb::module_& m) {
    m.attr("DEFAULT_PNG_COMPRESSION") = stb::default_png_compression;
    m.attr("DEFAULT_JPG_QUALITY") = stb::default_jpg_quality;

    m.def(
        "to_png",
        [](const image_in& pixels, int compression) {
            return encode_with(pixels, [&](const stb::image& img) { return img.to_png(compression); });
        },
        "pixels"_a, "compression"_a = stb::default_png_compression,
        "Encode a uint8 array of shape (H, W, C) as PNG bytes.");
    m.def(
        "to_jpg",
        [](const image_in& pixels, int quality) {
            return encode_with(pixels, [&](const stb::image& img) { return img.to_jpg(quality); });
        },
        "pixels"_a, "quality"_a = stb::default_jpg_quality,
        "Encode a uint8 array of shape (H, W, C) as JPEG bytes (alpha is dropped).");
    m.def(
        "to_bmp",
        [](const image_in& pixels) {
            return encode_with(pixels, [](const stb::image& img) { return img.to_bmp(); });
        },
        "pixels"_a, "Encode a uint8 array of shape (H, W, C) as BMP bytes.");
    m.def(
        "to_tga",
        [](const image_in& pixels, bool rle) {
            return encode_with(pixels, [&](const stb::image& img) { return img.to_tga(rle); });
        },
        "pixels"_a, "rle"_a = true, "Encode a uint8 array of shape (H, W, C) as TGA bytes.");
}
