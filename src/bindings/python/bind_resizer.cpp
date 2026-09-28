#include <nanobind/stl/string.h>

#include "binding.hpp"

using namespace nb::literals;

void bind_resizer(nb::module_& m) {
    using libstb::resize_edge;
    using libstb::resize_filter;
    using libstb::resizer;

    nb::class_<resizer> cls(m, "Resizer", "Resizes uint8 images of 1..4 channels.");

    // Nested enums (libstb.Resizer.Filter / .Edge); they must exist before
    // __init__ uses them as default arguments.
    nb::enum_<resize_filter>(cls, "Filter")
        .value("DEFAULT", resize_filter::automatic)
        .value("BOX", resize_filter::box)
        .value("TRIANGLE", resize_filter::triangle)
        .value("CUBIC_BSPLINE", resize_filter::cubic_bspline)
        .value("CATMULL_ROM", resize_filter::catmull_rom)
        .value("MITCHELL", resize_filter::mitchell)
        .value("POINT", resize_filter::point);
    nb::enum_<resize_edge>(cls, "Edge")
        .value("CLAMP", resize_edge::clamp)
        .value("REFLECT", resize_edge::reflect)
        .value("WRAP", resize_edge::wrap)
        .value("ZERO", resize_edge::zero);

    cls.def(
           "__init__",
           [](resizer* self, resize_filter filter, resize_edge edge, bool srgb, std::size_t max_bytes) {
               libstb::resize_options o;
               o.filter = filter;
               o.edge = edge;
               o.srgb = srgb;
               o.max_bytes = max_bytes;
               new (self) resizer(o);
           },
           "filter"_a = resize_filter::automatic, "edge"_a = resize_edge::clamp, "srgb"_a = true,
           "max_bytes"_a = libstb::resize_options{}.max_bytes)
        .def_prop_ro("filter", [](const resizer& r) { return r.options().filter; })
        .def_prop_ro("edge", [](const resizer& r) { return r.options().edge; })
        .def_prop_ro("srgb", [](const resizer& r) { return r.options().srgb; })
        .def_prop_ro("max_bytes", [](const resizer& r) { return r.options().max_bytes; })
        .def(
            "resize",
            [](const resizer& self, const image_in& pixels, int width, int height) {
                const libstb::image src = to_image(pixels);
                libstb::image out;
                {
                    nb::gil_scoped_release release;
                    out = self.resize(src, width, height);
                }
                return to_array(std::move(out));
            },
            "pixels"_a, "width"_a, "height"_a,
            "Resize a (H, W, C) uint8 array; returns a new (height, width, C) array.");
}
