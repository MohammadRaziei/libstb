#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>

#include "binding.hpp"
#include "stb.h"

// Filters (gaussian_blur, box_blur, convolve, sobel), the 8-bit <-> float conversions and superpixels.
// Every filter takes an Image (8 bits) or a float32 numpy array of shape (H, W) or (H, W, C), and returns
// the same kind; they are module functions and, for the Image, methods too.

namespace nb = nanobind;
using namespace nb::literals;

namespace {

using f32_array = nb::ndarray<const float, nb::c_contig, nb::device::cpu>;
using f32_result = nb::ndarray<nb::numpy, float>;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// An enum member, or its lower-case name.
template <class E>
E to_enum(nb::handle h, std::initializer_list<std::pair<const char*, E>> names, const char* what) {
    if (nb::isinstance<E>(h)) return nb::cast<E>(h);
    if (nb::isinstance<nb::str>(h)) {
        const std::string s = lower(nb::cast<std::string>(h));
        for (const auto& n : names)
            if (s == n.first) return n.second;
    }
    std::string list;
    for (const auto& n : names) list += std::string(list.empty() ? "" : ", ") + "\"" + n.first + "\"";
    throw std::invalid_argument(std::string(what) + " must be one of " + list);
}

stb::edge_mode edge_of(nb::handle h) {
    using stb::edge_mode;
    return to_enum<edge_mode>(h,
                              {{"clamp", edge_mode::clamp},
                               {"reflect", edge_mode::reflect},
                               {"mirror", edge_mode::mirror},
                               {"wrap", edge_mode::wrap},
                               {"zero", edge_mode::zero}},
                              "edge");
}

stb::superpixel_method method_of(nb::handle h) {
    using stb::superpixel_method;
    return to_enum<superpixel_method>(
        h, {{"slic", superpixel_method::slic}, {"slico", superpixel_method::slico}, {"snic", superpixel_method::snic}},
        "method");
}

// A float32 array of shape (H, W) or (H, W, C) seen as a mat32f, without a copy. The mat is only valid during the
// call, which is all the (synchronous) filters need.
stb::mat32f view_of(const f32_array& a) {
    if (a.ndim() != 2 && a.ndim() != 3) throw std::invalid_argument("expected an array of shape (H, W) or (H, W, C)");
    const std::size_t h = a.shape(0), w = a.shape(1), c = a.ndim() == 3 ? a.shape(2) : 1;
    if (h == 0 || w == 0 || c == 0) throw std::invalid_argument("the array must not be empty");
    return stb::mat32f::wrap(int(w), int(h), int(c), const_cast<float*>(a.data()));
}

// A mat32f as a numpy array that owns it: (H, W, C), or (H, W) if `with_channels` is false.
f32_result to_array(stb::mat32f&& m, bool with_channels) {
    auto* owned = new stb::mat32f(std::move(m));
    nb::capsule owner(owned, [](void* p) noexcept { delete static_cast<stb::mat32f*>(p); });
    const std::size_t shape[3] = {std::size_t(owned->height()), std::size_t(owned->width()), std::size_t(owned->channels())};
    return f32_result(owned->data(), with_channels ? 3 : 2, shape, owner);
}

// A kernel from a 1-D or 2-D array (any dtype numpy can convert to float32) or a (nested) list of numbers. Copied:
// kernels are small. A 1-D kernel is a row.
stb::mat32f kernel_of(nb::handle h) {
    f32_array a;
    if (nb::try_cast(h, a, /*convert=*/true)) {
        if (a.ndim() != 1 && a.ndim() != 2) throw std::invalid_argument("a kernel must be a 1-D or 2-D array");
        const std::size_t rows = a.ndim() == 2 ? a.shape(0) : 1, cols = a.ndim() == 2 ? a.shape(1) : a.shape(0);
        if (rows == 0 || cols == 0) throw std::invalid_argument("a kernel must not be empty");
        return stb::mat32f(int(cols), int(rows), 1, std::vector<float>(a.data(), a.data() + rows * cols));
    }
    if (nb::isinstance<nb::sequence>(h) && !nb::isinstance<nb::str>(h)) {
        std::vector<float> values;
        std::size_t rows = 0, cols = 0;
        for (nb::handle item : h) {
            if (nb::isinstance<nb::sequence>(item) && !nb::isinstance<nb::str>(item)) {
                std::size_t n = 0;
                for (nb::handle v : item) {
                    values.push_back(nb::cast<float>(v));
                    ++n;
                }
                if (rows > 0 && n != cols) throw std::invalid_argument("all rows of a kernel must have the same length");
                cols = n;
                ++rows;
            } else {
                if (rows > 1 || (rows == 1 && cols != 0 && !values.empty() && cols != values.size()))
                    throw std::invalid_argument("a kernel is a list of numbers or a list of lists");
                values.push_back(nb::cast<float>(item));
                rows = 1;
                cols = values.size();
            }
        }
        if (rows == 0 || cols == 0) throw std::invalid_argument("a kernel must not be empty");
        return stb::mat32f(int(cols), int(rows), 1, std::move(values));
    }
    throw std::invalid_argument("a kernel must be a 1-D or 2-D array, or a (nested) list of numbers");
}

// Runs fn(mat32f view of a) without the GIL and returns the result as an array shaped like `a`.
template <class Fn>
f32_result run_f32(const f32_array& a, Fn&& fn) {
    const bool with_channels = a.ndim() == 3;
    const stb::mat32f in = view_of(a);
    stb::mat32f out;
    {
        nb::gil_scoped_release release;
        out = fn(in);
    }
    return to_array(std::move(out), with_channels);
}

nb::tuple gradients_tuple(stb::gradients&& g, bool with_channels) {
    return nb::make_tuple(nb::cast(to_array(std::move(g.dx), with_channels)), nb::cast(to_array(std::move(g.dy), with_channels)));
}

// ---- the region map as a Python object
nb::object labels_view(nb::handle self) {
    const stb::superpixel_map& m = nb::cast<const stb::superpixel_map&>(self);
    if (m.empty()) throw std::invalid_argument("the map is empty");
    const std::size_t shape[2] = {std::size_t(m.height()), std::size_t(m.width())};
    return nb::cast(nb::ndarray<nb::numpy, const std::int32_t, nb::ndim<2>>(m.labels().data(), 2, shape, self));  // read-only view; keeps the map alive
}

constexpr const char* edge_doc =
    "`edge` says what lies outside the picture: \"clamp\" (the default: the nearest edge value repeats), \"reflect\",\n"
    "\"mirror\", \"wrap\" or \"zero\" (or an EdgeMode).";

}  // namespace

void bind_filter(nb::module_& m) {
    nb::enum_<stb::edge_mode>(m, "EdgeMode",
                              "What a filter reads where its window leaves the picture. Functions that take an EdgeMode also take\n"
                              "its lower-case name, e.g. \"mirror\".")
        .value("CLAMP", stb::edge_mode::clamp)
        .value("REFLECT", stb::edge_mode::reflect)
        .value("MIRROR", stb::edge_mode::mirror)
        .value("WRAP", stb::edge_mode::wrap)
        .value("ZERO", stb::edge_mode::zero);

    nb::enum_<stb::superpixel_method>(m, "SuperpixelMethod",
                                      "The superpixel algorithm. Functions that take a SuperpixelMethod also take its lower-case\n"
                                      "name: \"slic\", \"slico\" (the default) or \"snic\".")
        .value("SLIC", stb::superpixel_method::slic)
        .value("SLICO", stb::superpixel_method::slico)
        .value("SNIC", stb::superpixel_method::snic);

    auto image_cls = nb::borrow<nb::class_<stb::image>>(m.attr("Image"));

    // ---- conversions
    m.def("to_f32", [](const stb::image& img) { return to_array(stb::to_f32(img), true); }, "src"_a,
          "The pixels of an Image as a float32 array of shape (H, W, C): every value / 255, so 0 is 0.0 and 255 is 1.0.");
    image_cls.def("to_f32", [](const stb::image& img) { return to_array(stb::to_f32(img), true); },
                  "The pixels as a float32 array of shape (H, W, C): every value / 255, so 0 is 0.0 and 255 is 1.0.");
    m.def(
        "to_8u",
        [](const f32_array& a) -> stb::image {
            const stb::mat32f in = view_of(a);
            nb::gil_scoped_release release;
            return stb::to_8u(in);
        },
        "src"_a,
        "A float32 array of shape (H, W) or (H, W, C) with 1 to 4 channels as an Image: round(clamp(v, 0, 1) * 255),\n"
        "NaN becomes 0. The inverse of to_f32.");

    // ---- kernels
    m.def(
        "gaussian_kernel",
        [](float sigma, int radius) {
            stb::mat32f k = stb::gaussian_kernel(sigma, radius);
            return to_array(std::move(k), false);  // shape (1, N)
        },
        "sigma"_a, "radius"_a = 0,
        "The taps of a Gaussian of standard deviation `sigma` as a float32 array of shape (1, N), N = 2 * radius + 1, summing\n"
        "to 1. radius 0 means ceil(3 * sigma). Use it as both kernels of the separable convolve().");

    // ---- gaussian_blur
    m.def(
        "gaussian_blur",
        [](const stb::image& img, float sigma, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::gaussian_blur(img, sigma, e);
        },
        "src"_a, "sigma"_a, nb::kw_only(), "edge"_a = "clamp",
        (std::string("Gaussian blur of an Image (a new Image), or of a float32 array of shape (H, W) or (H, W, C) (a new array).\n"
                     "Each channel on its own; uses all the cores on a big picture, with the same result on any number of threads.\n") +
         edge_doc)
            .c_str());
    m.def(
        "gaussian_blur",
        [](const f32_array& src, float sigma, nb::handle edge) -> f32_result {
            const stb::edge_mode e = edge_of(edge);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::gaussian_blur(in, sigma, e); });
        },
        "src"_a, "sigma"_a, nb::kw_only(), "edge"_a = "clamp");
    image_cls.def(
        "gaussian_blur",
        [](const stb::image& img, float sigma, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::gaussian_blur(img, sigma, e);
        },
        "sigma"_a, nb::kw_only(), "edge"_a = "clamp", "A new Image: this one blurred with a Gaussian of standard deviation `sigma`.");

    // ---- box_blur
    m.def(
        "box_blur",
        [](const stb::image& img, int radius, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::box_blur(img, radius, e);
        },
        "src"_a, "radius"_a, nb::kw_only(), "edge"_a = "clamp",
        "The mean of the (2 * radius + 1)-square around each pixel of an Image (a new Image), or of a float32 array\n"
        "(a new array). The cost does not depend on the radius.");
    m.def(
        "box_blur",
        [](const f32_array& src, int radius, nb::handle edge) -> f32_result {
            const stb::edge_mode e = edge_of(edge);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::box_blur(in, radius, e); });
        },
        "src"_a, "radius"_a, nb::kw_only(), "edge"_a = "clamp");
    image_cls.def(
        "box_blur",
        [](const stb::image& img, int radius, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::box_blur(img, radius, e);
        },
        "radius"_a, nb::kw_only(), "edge"_a = "clamp", "A new Image: the mean of the (2 * radius + 1)-square around each pixel.");

    // ---- convolve: a full 2-D kernel, or a row and a column
    const std::string conv_doc =
        std::string("Convolve with a kernel (a 2-D array or a list of lists of numbers, odd width and height), laid over each\n"
                    "pixel as it is (a correlation, not flipped, like OpenCV's filter2D). With two arguments after `src`, a row\n"
                    "kernel and a column kernel (each a list of an odd number of taps): the same as their product but much\n"
                    "cheaper. Takes an Image or a float32 array; returns the same kind.\n") +
        edge_doc;
    m.def(
        "convolve",
        [](const stb::image& img, nb::handle kernel, nb::handle edge) -> stb::image {
            const stb::mat32f k = kernel_of(kernel);
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::convolve(img, k, e);
        },
        "src"_a, "kernel"_a, nb::kw_only(), "edge"_a = "clamp", conv_doc.c_str());
    m.def(
        "convolve",
        [](const f32_array& src, nb::handle kernel, nb::handle edge) -> f32_result {
            const stb::mat32f k = kernel_of(kernel);
            const stb::edge_mode e = edge_of(edge);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::convolve(in, k, e); });
        },
        "src"_a, "kernel"_a, nb::kw_only(), "edge"_a = "clamp");
    m.def(
        "convolve",
        [](const stb::image& img, nb::handle row, nb::handle col, nb::handle edge) -> stb::image {
            const stb::mat32f r = kernel_of(row), c = kernel_of(col);
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::convolve(img, r, c, e);
        },
        "src"_a, "row_kernel"_a, "col_kernel"_a, nb::kw_only(), "edge"_a = "clamp");
    m.def(
        "convolve",
        [](const f32_array& src, nb::handle row, nb::handle col, nb::handle edge) -> f32_result {
            const stb::mat32f r = kernel_of(row), c = kernel_of(col);
            const stb::edge_mode e = edge_of(edge);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::convolve(in, r, c, e); });
        },
        "src"_a, "row_kernel"_a, "col_kernel"_a, nb::kw_only(), "edge"_a = "clamp");
    image_cls.def(
        "convolve",
        [](const stb::image& img, nb::handle kernel, nb::handle edge) -> stb::image {
            const stb::mat32f k = kernel_of(kernel);
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::convolve(img, k, e);
        },
        "kernel"_a, nb::kw_only(), "edge"_a = "clamp", "A new Image: this one convolved with a 2-D kernel (see libstb.convolve).");
    image_cls.def(
        "convolve",
        [](const stb::image& img, nb::handle row, nb::handle col, nb::handle edge) -> stb::image {
            const stb::mat32f r = kernel_of(row), c = kernel_of(col);
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::convolve(img, r, c, e);
        },
        "row_kernel"_a, "col_kernel"_a, nb::kw_only(), "edge"_a = "clamp");

    // ---- sobel and magnitude
    m.def(
        "sobel",
        [](const stb::image& img, nb::handle edge) {
            const stb::edge_mode e = edge_of(edge);
            stb::gradients g;
            {
                nb::gil_scoped_release release;
                g = stb::sobel(img, e);
            }
            return gradients_tuple(std::move(g), true);
        },
        "src"_a, nb::kw_only(), "edge"_a = "clamp",
        "The Sobel derivatives of an Image (read as value / 255) or a float32 array: a tuple (dx, dy) of float32 arrays\n"
        "of the same shape, positive where the values grow to the right (dx) or downwards (dy). A step of height 1 gives 4.");
    m.def(
        "sobel",
        [](const f32_array& src, nb::handle edge) {
            const stb::edge_mode e = edge_of(edge);
            const bool with_channels = src.ndim() == 3;
            const stb::mat32f in = view_of(src);
            stb::gradients g;
            {
                nb::gil_scoped_release release;
                g = stb::sobel(in, e);
            }
            return gradients_tuple(std::move(g), with_channels);
        },
        "src"_a, nb::kw_only(), "edge"_a = "clamp");
    image_cls.def(
        "sobel",
        [](const stb::image& img, nb::handle edge) {
            const stb::edge_mode e = edge_of(edge);
            stb::gradients g;
            {
                nb::gil_scoped_release release;
                g = stb::sobel(img, e);
            }
            return gradients_tuple(std::move(g), true);
        },
        nb::kw_only(), "edge"_a = "clamp", "The Sobel derivatives as a tuple (dx, dy) of float32 arrays (H, W, C); see libstb.sobel.");
    m.def(
        "magnitude",
        [](const f32_array& dx, const f32_array& dy) -> f32_result {
            const bool with_channels = dx.ndim() == 3;
            stb::gradients g;
            g.dx = view_of(dx).copy();
            g.dy = view_of(dy).copy();
            stb::mat32f out;
            {
                nb::gil_scoped_release release;
                out = stb::magnitude(g);
            }
            return to_array(std::move(out), with_channels);
        },
        "dx"_a, "dy"_a, "sqrt(dx**2 + dy**2) of two float32 arrays of the same shape (the result of sobel()).");

    // ---- superpixels
    nb::class_<stb::superpixel_map>(m, "SuperpixelMap",
                                    "The regions of an image: a label for every pixel, 0 .. count - 1, each region one\n"
                                    "connected piece. Made by superpixels() or Image.superpixels().")
        .def_prop_ro("width", &stb::superpixel_map::width)
        .def_prop_ro("height", &stb::superpixel_map::height)
        .def_prop_ro("count", &stb::superpixel_map::count, "How many regions there are.")
        .def_prop_ro("labels", [](nb::handle self) { return labels_view(self); },
                     "The label of every pixel as a read-only int32 array of shape (H, W) (a view: no copy).")
        .def(
            "label",
            [](const stb::superpixel_map& map, int x, int y) {
                if (x < 0 || y < 0 || x >= map.width() || y >= map.height()) throw nb::index_error("pixel out of range");
                return map.label(x, y);
            },
            "x"_a, "y"_a, "The region of pixel (x, y).")
        .def(
            "boundaries",
            [](const stb::superpixel_map& map, nb::handle color) -> stb::image {
                return map.boundaries(color.is_valid() && !color.is_none() ? text_colour(color) : stb::color(255, 0, 0));
            },
            "color"_a = nb::none(), "An RGBA Image, transparent except for the pixels where a region ends, which are `color` (red by default).")
        .def(
            "overlay",
            [](const stb::superpixel_map& map, const stb::image& src, nb::handle color) -> stb::image {
                const stb::color c = color.is_valid() && !color.is_none() ? text_colour(color) : stb::color(255, 0, 0);
                nb::gil_scoped_release release;
                return map.overlay(src, c);
            },
            "src"_a, "color"_a = nb::none(), "`src` with the region borders drawn in `color` (red by default). A gray image becomes RGB.")
        .def(
            "mean_color",
            [](const stb::superpixel_map& map, const stb::image& src) -> stb::image {
                nb::gil_scoped_release release;
                return map.mean_color(src);
            },
            "src"_a, "`src` with every pixel replaced by the mean of its region.")
        .def("copy", &stb::superpixel_map::copy)
        .def("__repr__", [](const stb::superpixel_map& map) {
            return "SuperpixelMap(" + std::to_string(map.width()) + "x" + std::to_string(map.height()) + ", " +
                   std::to_string(map.count()) + " regions)";
        });

    const char* sp_doc =
        "Cuts an Image into about `count` compact regions of similar colour. `compactness` (CIELAB units, 10 is usual,\n"
        "1 to 40 is the useful range) is how much position counts against colour: higher gives squarer regions, lower\n"
        "follows edges more closely. `method` is \"slico\" (the default; adapts the compactness per region), \"slic\" or\n"
        "\"snic\" (one pass, no iterations); `iterations` is for slic and slico, `enforce_connectivity` merges the small\n"
        "detached pieces slic and slico can leave. The same Image and arguments give the same SuperpixelMap on every\n"
        "number of threads. Returns a SuperpixelMap.";
    const auto make = [](const stb::image& img, int count, float compactness, int iterations, nb::handle method,
                         bool enforce) {
        stb::superpixel_options o;
        o.count = count;
        o.compactness = compactness;
        o.iterations = iterations;
        o.enforce_connectivity = enforce;
        o.method = method_of(method);
        nb::gil_scoped_release release;
        return stb::superpixels(img, o);
    };
    m.def("superpixels", make, "src"_a, "count"_a = 400, "compactness"_a = 10.0f, "iterations"_a = 10, "method"_a = "slico",
          "enforce_connectivity"_a = true, sp_doc);
    image_cls.def("superpixels", make, "count"_a = 400, "compactness"_a = 10.0f, "iterations"_a = 10, "method"_a = "slico",
                  "enforce_connectivity"_a = true, sp_doc);
}
