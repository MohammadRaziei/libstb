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

// Filters (gaussian_blur, box_blur, convolve, sobel), the tone functions (histogram, equalize, gamma, ...), the 8-bit <-> float
// conversions and superpixels.
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

// ---- tone (stb/tone.hpp): histogram, equalize, auto_contrast, match_histogram, gamma, levels and look-up tables

stb::tone_mode tone_mode_of(nb::handle h) {
    using stb::tone_mode;
    return to_enum<tone_mode>(h, {{"luma", tone_mode::luma}, {"per_channel", tone_mode::per_channel}}, "mode");
}

using lut_array = nb::ndarray<const std::uint8_t, nb::c_contig, nb::device::cpu>;
using i32_result = nb::ndarray<nb::numpy, std::int32_t>;
using u8_result = nb::ndarray<nb::numpy, std::uint8_t>;

// A histogram as an int32 array of shape (channels, bins).
i32_result histogram_array(const stb::mat32i& h) {
    if (h.empty()) throw std::invalid_argument("the image is empty");
    const std::size_t c = std::size_t(h.channels()), bins = std::size_t(h.width());
    auto* owned = new std::vector<std::int32_t>(c * bins);
    for (std::size_t k = 0; k < c; ++k)
        for (std::size_t b = 0; b < bins; ++b) (*owned)[k * bins + b] = h.data()[b * c + k];
    nb::capsule owner(owned, [](void* p) noexcept { delete static_cast<std::vector<std::int32_t>*>(p); });
    const std::size_t shape[2] = {c, bins};
    return i32_result(owned->data(), 2, shape, owner);
}

// A 256 x 1 x C table as a uint8 array of shape (256,) (one channel) or (256, C).
u8_result lut_to_array(const stb::mat8u& t) {
    const std::size_t c = std::size_t(t.channels());
    auto* owned = new std::vector<std::uint8_t>(t.begin(), t.end());
    nb::capsule owner(owned, [](void* p) noexcept { delete static_cast<std::vector<std::uint8_t>*>(p); });
    const std::size_t shape[2] = {256, c};
    return u8_result(owned->data(), c == 1 ? 1 : 2, shape, owner);
}

stb::mat8u lut_of(const lut_array& a) {
    if ((a.ndim() != 1 && a.ndim() != 2) || a.shape(0) != 256 || (a.ndim() == 2 && a.shape(1) == 0))
        throw std::invalid_argument("a look-up table is an array of shape (256,) or (256, channels)");
    const std::size_t c = a.ndim() == 2 ? a.shape(1) : 1;
    return stb::mat8u(256, 1, int(c), std::vector<std::uint8_t>(a.data(), a.data() + 256 * c));
}

// The Image form of a method: a new Image, or with inplace=True the same object, changed.
template <class CopyFn, class InplaceFn>
nb::object image_form(nb::handle self, bool inplace, CopyFn&& copy_fn, InplaceFn&& inplace_fn) {
    stb::image& img = nb::cast<stb::image&>(self);
    if (inplace) {
        {
            nb::gil_scoped_release release;
            inplace_fn(img);
        }
        return nb::borrow(self);
    }
    stb::image out;
    {
        nb::gil_scoped_release release;
        out = copy_fn(static_cast<const stb::image&>(img));
    }
    return nb::cast(std::move(out));
}

constexpr const char* tone_doc =
    "Takes an Image (gives a new Image) or a float32 array of shape (H, W) or (H, W, C) with values 0 to 1 (gives a new\n"
    "array). 1 to 4 channels: gray, gray + alpha, RGB, RGBA; the alpha channel is never changed.";
constexpr const char* mode_doc =
    "`mode` is \"luma\" (the default: only the brightness changes, so the colours do not shift) or \"per_channel\"\n"
    "(or a ToneMode).";

void bind_tone(nb::module_& m, nb::class_<stb::image>& image_cls) {
    nb::enum_<stb::tone_mode>(m, "ToneMode",
                              "What equalize, auto_contrast and match_histogram look at. Functions that take a ToneMode also take\n"
                              "its lower-case name: \"luma\" (the default) or \"per_channel\".")
        .value("LUMA", stb::tone_mode::luma)
        .value("PER_CHANNEL", stb::tone_mode::per_channel);

    // ---- histogram
    const auto histogram8 = [](const stb::image& img) {
        stb::mat32i h;
        {
            nb::gil_scoped_release release;
            h = stb::histogram(img);
        }
        return histogram_array(h);
    };
    const char* histogram_doc =
        "How many pixels have each value, as an int32 array of shape (channels, 256): row k is channel k (alpha included).\n"
        "For a float32 array: `bins` equal bins over [0, 1] (values outside are counted in the first or last bin, NaN not\n"
        "at all) and shape (channels, bins).";
    m.def("histogram", histogram8, "src"_a, histogram_doc);
    m.def(
        "histogram",
        [](const f32_array& src, int bins) {
            const stb::mat32f in = view_of(src);
            stb::mat32i h;
            {
                nb::gil_scoped_release release;
                h = stb::histogram(in, bins);
            }
            return histogram_array(h);
        },
        "src"_a, "bins"_a = 256);
    image_cls.def("histogram", histogram8, "How many pixels have each value, as an int32 array of shape (channels, 256).");

    // ---- equalize
    const std::string equalize_doc =
        std::string("Spreads the levels so that each one is used about as often (histogram equalisation). A picture of one value\n"
                    "is returned unchanged.\n") + tone_doc + "\n" + mode_doc;
    m.def(
        "equalize",
        [](const stb::image& img, nb::handle mode) -> stb::image {
            const stb::tone_mode t = tone_mode_of(mode);
            nb::gil_scoped_release release;
            return stb::equalize(img, t);
        },
        "src"_a, "mode"_a = "luma", equalize_doc.c_str());
    m.def(
        "equalize",
        [](const f32_array& src, nb::handle mode) -> f32_result {
            const stb::tone_mode t = tone_mode_of(mode);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::equalize(in, t); });
        },
        "src"_a, "mode"_a = "luma");
    image_cls.def(
        "equalize",
        [](nb::handle self, nb::handle mode, bool inplace) {
            const stb::tone_mode t = tone_mode_of(mode);
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::equalize(i, t); },
                [&](stb::image& i) { stb::equalize_inplace(i, t); });
        },
        "mode"_a = "luma", nb::kw_only(), "inplace"_a = false,
        "Equalizes the histogram: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- auto_contrast
    const std::string auto_doc =
        std::string("Stretches the values to the whole range after dropping the darkest and brightest `cutoff` percent (0 up to\n"
                    "but not including 50), like Pillow's ImageOps.autocontrast.\n") + tone_doc + "\n" + mode_doc;
    m.def(
        "auto_contrast",
        [](const stb::image& img, float cutoff, nb::handle mode) -> stb::image {
            const stb::tone_mode t = tone_mode_of(mode);
            nb::gil_scoped_release release;
            return stb::auto_contrast(img, cutoff, t);
        },
        "src"_a, "cutoff"_a = 0.0f, "mode"_a = "luma", auto_doc.c_str());
    m.def(
        "auto_contrast",
        [](const f32_array& src, float cutoff, nb::handle mode) -> f32_result {
            const stb::tone_mode t = tone_mode_of(mode);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::auto_contrast(in, cutoff, t); });
        },
        "src"_a, "cutoff"_a = 0.0f, "mode"_a = "luma");
    image_cls.def(
        "auto_contrast",
        [](nb::handle self, float cutoff, nb::handle mode, bool inplace) {
            const stb::tone_mode t = tone_mode_of(mode);
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::auto_contrast(i, cutoff, t); },
                [&](stb::image& i) { stb::auto_contrast_inplace(i, cutoff, t); });
        },
        "cutoff"_a = 0.0f, "mode"_a = "luma", nb::kw_only(), "inplace"_a = false,
        "Stretches the contrast: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- match_histogram
    const std::string match_doc =
        std::string("Moves the levels so that the histogram follows the one of `reference` (an Image, or a float32 array for a\n"
                    "float32 `src`; any size, the same number of channels).\n") + tone_doc + "\n" + mode_doc;
    m.def(
        "match_histogram",
        [](const stb::image& img, const stb::image& reference, nb::handle mode) -> stb::image {
            const stb::tone_mode t = tone_mode_of(mode);
            nb::gil_scoped_release release;
            return stb::match_histogram(img, reference, t);
        },
        "src"_a, "reference"_a, "mode"_a = "luma", match_doc.c_str());
    m.def(
        "match_histogram",
        [](const f32_array& src, const f32_array& reference, nb::handle mode) -> f32_result {
            const stb::tone_mode t = tone_mode_of(mode);
            const stb::mat32f ref = view_of(reference);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::match_histogram(in, ref, t); });
        },
        "src"_a, "reference"_a, "mode"_a = "luma");
    image_cls.def(
        "match_histogram",
        [](nb::handle self, const stb::image& reference, nb::handle mode, bool inplace) {
            const stb::tone_mode t = tone_mode_of(mode);
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::match_histogram(i, reference, t); },
                [&](stb::image& i) { stb::match_histogram_inplace(i, reference, t); });
        },
        "reference"_a, "mode"_a = "luma", nb::kw_only(), "inplace"_a = false,
        "Matches the histogram of `reference`: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- gamma
    const std::string gamma_doc =
        std::string("Every colour channel c becomes c ** (1 / g): g above 1 brightens, below 1 darkens. g must be a positive\n"
                    "finite number.\n") + tone_doc;
    m.def(
        "gamma",
        [](const stb::image& img, float g) -> stb::image {
            nb::gil_scoped_release release;
            return stb::gamma(img, g);
        },
        "src"_a, "g"_a, gamma_doc.c_str());
    m.def(
        "gamma", [](const f32_array& src, float g) -> f32_result {
            return run_f32(src, [&](const stb::mat32f& in) { return stb::gamma(in, g); });
        },
        "src"_a, "g"_a);
    image_cls.def(
        "gamma",
        [](nb::handle self, float g, bool inplace) {
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::gamma(i, g); },
                [&](stb::image& i) { stb::gamma_inplace(i, g); });
        },
        "g"_a, nb::kw_only(), "inplace"_a = false,
        "Gamma correction: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- levels
    const std::string levels_doc =
        std::string("The levels adjustment of an image editor: [in_black, in_white] is stretched to 0..1, raised to 1 / gamma and\n"
                    "mapped to [out_black, out_white]. All levels are fractions of the full range, 0 <= in_black < in_white <= 1.\n") +
        tone_doc;
    m.def(
        "levels",
        [](const stb::image& img, float in_black, float in_white, float g, float out_black, float out_white) -> stb::image {
            nb::gil_scoped_release release;
            return stb::levels(img, in_black, in_white, g, out_black, out_white);
        },
        "src"_a, "in_black"_a, "in_white"_a, "gamma"_a = 1.0f, "out_black"_a = 0.0f, "out_white"_a = 1.0f, levels_doc.c_str());
    m.def(
        "levels",
        [](const f32_array& src, float in_black, float in_white, float g, float out_black, float out_white) -> f32_result {
            return run_f32(src, [&](const stb::mat32f& in) { return stb::levels(in, in_black, in_white, g, out_black, out_white); });
        },
        "src"_a, "in_black"_a, "in_white"_a, "gamma"_a = 1.0f, "out_black"_a = 0.0f, "out_white"_a = 1.0f);
    image_cls.def(
        "levels",
        [](nb::handle self, float in_black, float in_white, float g, float out_black, float out_white, bool inplace) {
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::levels(i, in_black, in_white, g, out_black, out_white); },
                [&](stb::image& i) { stb::levels_inplace(i, in_black, in_white, g, out_black, out_white); });
        },
        "in_black"_a, "in_white"_a, "gamma"_a = 1.0f, "out_black"_a = 0.0f, "out_white"_a = 1.0f, nb::kw_only(), "inplace"_a = false,
        "The levels adjustment: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- look-up tables
    m.def("gamma_lut", [](float g) { return lut_to_array(stb::gamma_lut(g)); }, "g"_a,
          "The look-up table of gamma(): a uint8 array of shape (256,).");
    m.def(
        "levels_lut",
        [](float in_black, float in_white, float g, float out_black, float out_white) {
            return lut_to_array(stb::levels_lut(in_black, in_white, g, out_black, out_white));
        },
        "in_black"_a, "in_white"_a, "gamma"_a = 1.0f, "out_black"_a = 0.0f, "out_white"_a = 1.0f,
        "The look-up table of levels(): a uint8 array of shape (256,).");
    const auto apply_lut = [](const stb::image& img, const lut_array& lut) -> stb::image {
        const stb::mat8u table = lut_of(lut);
        nb::gil_scoped_release release;
        return stb::apply_lut(img, table);
    };
    m.def("apply_lut", apply_lut, "src"_a, "lut"_a,
          "An Image with every value v replaced by lut[v]. `lut` is a uint8 array of shape (256,) (one table for every channel,\n"
          "alpha included) or (256, channels) (one table per channel).");
    image_cls.def("apply_lut", apply_lut, "lut"_a, "A new Image with every value v replaced by lut[v] (see libstb.apply_lut).");
}

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

    bind_tone(m, image_cls);
}
