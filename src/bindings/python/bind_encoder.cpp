#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>

#include "binding.hpp"

using namespace nb::literals;

// The C++ encoder class hierarchy, mirrored.
void bind_encoder(nb::module_& m) {
    nb::class_<stb::encoder>(m, "Encoder", "Abstract base class of all image encoders.")
        .def(
            "encode",
            [](const stb::encoder& self, const image_in& pixels) {
                const stb::image img = to_image(pixels);
                std::vector<std::uint8_t> out;
                {
                    nb::gil_scoped_release release;
                    out = self.encode(img);
                }
                return nb::bytes(reinterpret_cast<const char*>(out.data()), out.size());
            },
            "pixels"_a, "Encode a uint8 array of shape (H, W, C) to bytes.")
        .def_prop_ro("extension",
                     [](const stb::encoder& self) { return std::string(self.extension()); })
        .def_static(
            "for_path",
            [](const std::filesystem::path& path) { return stb::encoder::for_path(path); },
            "path"_a, "Default-configured encoder for a path's extension.");

    nb::class_<stb::png_encoder, stb::encoder>(m, "PngEncoder")
        .def(nb::init<int>(), "compression"_a = stb::png_encoder::default_compression)
        .def_prop_ro("compression", &stb::png_encoder::compression);

    nb::class_<stb::jpeg_encoder, stb::encoder>(m, "JpegEncoder")
        .def(nb::init<int>(), "quality"_a = stb::jpeg_encoder::default_quality)
        .def_prop_ro("quality", &stb::jpeg_encoder::quality);

    nb::class_<stb::bmp_encoder, stb::encoder>(m, "BmpEncoder").def(nb::init<>());

    nb::class_<stb::tga_encoder, stb::encoder>(m, "TgaEncoder")
        .def(nb::init<bool>(), "rle"_a = true)
        .def_prop_ro("rle", &stb::tga_encoder::rle);
}
