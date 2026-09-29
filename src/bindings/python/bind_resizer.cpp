#include <nanobind/stl/string.h>

#include "binding.hpp"

using namespace nb::literals;

void bind_resizer(nb::module_& m) {
    using stb::resize_edge;
    using stb::resize_filter;
    using stb::resizer;

    nb::class_<resizer> cls(
        m, "Resizer",
        "Resizes uint8 images of 1..4 channels.\n\n"
        "Resizer(filter, edge, srgb, max_bytes): `filter` is a Resizer.Filter or a name such as\n"
        "\"nearest\", \"linear\", \"cubic\", \"bspline\", \"mitchell\", \"box\", \"auto\".");

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
               stb::resize_options o;
               o.filter = filter;
               o.edge = edge;
               o.srgb = srgb;
               o.max_bytes = max_bytes;
               new (self) resizer(o);
           },
           "filter"_a = resize_filter::automatic, "edge"_a = resize_edge::clamp, "srgb"_a = true,
           "max_bytes"_a = stb::resize_options{}.max_bytes)
        // Resizer("cubic", ...): the filter by name (see resize_filter_from_name).
        // An unknown name raises ValueError listing the valid ones.
        .def(
            "__init__",
            [](resizer* self, const std::string& filter, resize_edge edge, bool srgb, std::size_t max_bytes) {
                stb::resize_options o;
                o.filter = stb::resize_filter_from_name(filter);
                o.edge = edge;
                o.srgb = srgb;
                o.max_bytes = max_bytes;
                new (self) resizer(o);
            },
            "filter"_a, "edge"_a = resize_edge::clamp, "srgb"_a = true,
            "max_bytes"_a = stb::resize_options{}.max_bytes)
        .def_prop_ro("filter", [](const resizer& r) { return r.options().filter; })
        .def_prop_ro("edge", [](const resizer& r) { return r.options().edge; })
        .def_prop_ro("srgb", [](const resizer& r) { return r.options().srgb; })
        .def_prop_ro("max_bytes", [](const resizer& r) { return r.options().max_bytes; })
        .def("resize", &resizer::resize, "image"_a, "width"_a, "height"_a,
             nb::call_guard<nb::gil_scoped_release>(),
             "A new Image of the given size (same as image.resize(width, height, resizer)).");
}
