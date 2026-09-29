#include <nanobind/stl/tuple.h>

#include <tuple>

#include "binding.hpp"

using namespace nb::literals;

void bind_image(nb::module_& m) {
    // ponytail: plain tuple; the Python package wraps it in ImageInfo.
    m.def(
        "info_bytes",
        [](nb::bytes data) {
            const char* p = data.c_str();
            const std::size_t n = data.size();
            stb::image_info i;
            {
                nb::gil_scoped_release release;  // decode without holding the GIL
                i = stb::image_info::read(p, n);
            }
            return std::make_tuple(i.width, i.height, i.channels);
        },
        "data"_a);

    m.def(
        "load_bytes",
        [](nb::bytes data, int channels, bool flip, std::size_t max_bytes) {
            stb::load_options opt;
            opt.channels = channels;
            opt.flip = flip;
            opt.max_bytes = max_bytes;

            const char* p = data.c_str();
            const std::size_t n = data.size();
            stb::image img;
            {
                nb::gil_scoped_release release;
                img = stb::image::decode(p, n, opt);
            }
            return to_array(std::move(img));
        },
        "data"_a, "channels"_a = 0, "flip"_a = false,
        "max_bytes"_a = stb::load_options{}.max_bytes);
}
