#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>

#include "binding.hpp"

using namespace nb::literals;

// The C++ encoder class hierarchy, mirrored.
void bind_encoder(nb::module_& m) {
    nb::class_<libstb::encoder>(m, "Encoder", "Abstract base class of all image encoders.")
        .def(
            "encode",
            [](const libstb::encoder& self, const image_in& pixels) {
                const libstb::image img = to_image(pixels);
                std::vector<std::uint8_t> out;
                {
                    nb::gil_scoped_release release;
                    out = self.encode(img);
                }
                return nb::bytes(reinterpret_cast<const char*>(out.data()), out.size());
            },
            "pixels"_a, "Encode a uint8 array of shape (H, W, C) to bytes.")
        .def_prop_ro("extension",
                     [](const libstb::encoder& self) { return std::string(self.extension()); })
        .def_static(
            "for_path",
            [](const std::filesystem::path& path) { return libstb::encoder::for_path(path); },
            "path"_a, "Default-configured encoder for a path's extension.");

    nb::class_<libstb::png_encoder, libstb::encoder>(m, "PngEncoder")
        .def(nb::init<int>(), "compression"_a = libstb::png_encoder::default_compression)
        .def_prop_ro("compression", &libstb::png_encoder::compression);

    nb::class_<libstb::jpeg_encoder, libstb::encoder>(m, "JpegEncoder")
        .def(nb::init<int>(), "quality"_a = libstb::jpeg_encoder::default_quality)
        .def_prop_ro("quality", &libstb::jpeg_encoder::quality);

    nb::class_<libstb::bmp_encoder, libstb::encoder>(m, "BmpEncoder").def(nb::init<>());

    nb::class_<libstb::tga_encoder, libstb::encoder>(m, "TgaEncoder")
        .def(nb::init<bool>(), "rle"_a = true)
        .def_prop_ro("rle", &libstb::tga_encoder::rle);
}
