#pragma once

// Internal helpers shared by the bind_*.cpp files.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <climits>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "libstb.h"

namespace nb = nanobind;

using image_out = nb::ndarray<nb::numpy, std::uint8_t, nb::ndim<3>>;
// c_contig: nanobind converts (copies) strided input, e.g. `arr[::-1]`.
using image_in = nb::ndarray<const std::uint8_t, nb::ndim<3>, nb::c_contig, nb::device::cpu>;

// The ndarray borrows the image's pixel memory; the capsule owns the image.
inline image_out to_array(libstb::image&& img) {
    auto* owned = new libstb::image(std::move(img));
    nb::capsule owner(owned, [](void* p) noexcept { delete static_cast<libstb::image*>(p); });
    const std::size_t shape[3] = {std::size_t(owned->height()), std::size_t(owned->width()),
                                  std::size_t(owned->channels())};
    return image_out(owned->data(), 3, shape, owner);
}

inline libstb::image to_image(const image_in& a) {
    const std::size_t h = a.shape(0), w = a.shape(1), c = a.shape(2);
    if (h > INT_MAX || w > INT_MAX) throw std::invalid_argument("image dimensions too large");
    // ponytail: one copy into the image's own vector; borrowing the numpy
    // buffer is the upgrade path if this ever shows up in a profile.
    return libstb::image(int(w), int(h), int(c),
                         std::vector<std::uint8_t>(a.data(), a.data() + h * w * c));
}

void bind_image(nb::module_& m);
void bind_encoder(nb::module_& m);
void bind_resizer(nb::module_& m);
void bind_font(nb::module_& m);
