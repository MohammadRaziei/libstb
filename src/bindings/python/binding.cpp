#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/tuple.h>

#include <cstdint>
#include <tuple>
#include <utility>

#include "libstb/image.hpp"

namespace nb = nanobind;
using namespace nb::literals;

using image_array = nb::ndarray<nb::numpy, std::uint8_t, nb::ndim<3>>;

NB_MODULE(libstb_py, m) {
    m.doc() = "libstb native module (stb single-header libraries)";

    // ponytail: plain tuples, no ImageInfo class here; the Python package
    // wraps it in a namedtuple.
    m.def(
        "info_bytes",
        [](nb::bytes data) {
            const char* p = data.c_str();
            const std::size_t n = data.size();
            libstb::image_info i;
            {
                nb::gil_scoped_release release;  // decode without holding the GIL
                i = libstb::info(p, n);
            }
            return std::make_tuple(i.width, i.height, i.channels);
        },
        "data"_a);

    m.def(
        "load_bytes",
        [](nb::bytes data, int channels, bool flip, std::size_t max_bytes) {
            libstb::load_options opt;
            opt.channels = channels;
            opt.flip = flip;
            opt.max_bytes = max_bytes;

            const char* p = data.c_str();
            const std::size_t n = data.size();
            libstb::image img;
            {
                nb::gil_scoped_release release;
                img = libstb::load(p, n, opt);
            }

            // The ndarray borrows the vector's memory; the capsule owns it.
            auto* owned = new libstb::image(std::move(img));
            nb::capsule owner(owned, [](void* q) noexcept { delete static_cast<libstb::image*>(q); });
            const std::size_t shape[3] = {std::size_t(owned->height), std::size_t(owned->width),
                                          std::size_t(owned->channels)};
            return image_array(owned->data.data(), 3, shape, owner);
        },
        "data"_a, "channels"_a = 0, "flip"_a = false,
        "max_bytes"_a = libstb::load_options{}.max_bytes);
}
