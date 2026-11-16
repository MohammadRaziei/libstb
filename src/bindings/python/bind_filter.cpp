#include <algorithm>
#include <cctype>
#include <cstring>
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

// ---- median_filter, unsharp_mask, pixelize (stb/filter.hpp), colour spaces (stb/colorspace.hpp), metrics (stb/metrics.hpp)

stb::color_space space_of(nb::handle h, const char* what) {
    using stb::color_space;
    return to_enum<color_space>(h,
                                {{"rgb", color_space::rgb}, {"hsv", color_space::hsv}, {"hsl", color_space::hsl}, {"ycbcr", color_space::ycbcr}},
                                what);
}

constexpr const char* alpha_doc =
    "Takes an Image (gives a new Image) or a float32 array of shape (H, W) or (H, W, C) with values 0 to 1 (gives a new\n"
    "array); 1 to 4 channels: gray, gray + alpha, RGB, RGBA.";

void bind_image_ops(nb::module_& m, nb::class_<stb::image>& image_cls) {
    nb::enum_<stb::color_space>(m, "ColorSpace",
                                "A colour space for convert_color. Functions that take a ColorSpace also take its lower-case name:\n"
                                "\"rgb\", \"hsv\", \"hsl\" or \"ycbcr\".")
        .value("RGB", stb::color_space::rgb)
        .value("HSV", stb::color_space::hsv)
        .value("HSL", stb::color_space::hsl)
        .value("YCBCR", stb::color_space::ycbcr);

    // ---- median_filter
    const std::string median_doc =
        std::string("The median of the (2 * radius + 1)-square around each value, per channel (radius 1 to 1000): removes isolated\n"
                    "noise and keeps edges. Takes an Image (a new Image) or a float32 array of shape (H, W) or (H, W, C) (a new array).\n") +
        edge_doc;
    m.def(
        "median_filter",
        [](const stb::image& img, int radius, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::median_filter(img, radius, e);
        },
        "src"_a, "radius"_a, nb::kw_only(), "edge"_a = "clamp", median_doc.c_str());
    m.def(
        "median_filter",
        [](const f32_array& src, int radius, nb::handle edge) -> f32_result {
            const stb::edge_mode e = edge_of(edge);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::median_filter(in, radius, e); });
        },
        "src"_a, "radius"_a, nb::kw_only(), "edge"_a = "clamp");
    image_cls.def(
        "median_filter",
        [](const stb::image& img, int radius, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::median_filter(img, radius, e);
        },
        "radius"_a, nb::kw_only(), "edge"_a = "clamp", "A new Image: the median filter of this one.");

    // ---- unsharp_mask
    const std::string unsharp_doc =
        std::string("out = src + amount * (src - gaussian_blur(src, sigma)), clamped to 0..1; a value whose difference from the blur is\n"
                    "below `threshold` (a fraction of the full range) is left alone. amount 1 is \"100 %\" of Pillow's UnsharpMask.\n"
                    "The alpha channel is never changed.\n") + alpha_doc + "\n" + edge_doc;
    m.def(
        "unsharp_mask",
        [](const stb::image& img, float sigma, float amount, float threshold, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return stb::unsharp_mask(img, sigma, amount, threshold, e);
        },
        "src"_a, "sigma"_a, "amount"_a = 1.0f, "threshold"_a = 0.0f, nb::kw_only(), "edge"_a = "clamp", unsharp_doc.c_str());
    m.def(
        "unsharp_mask",
        [](const f32_array& src, float sigma, float amount, float threshold, nb::handle edge) -> f32_result {
            const stb::edge_mode e = edge_of(edge);
            return run_f32(src, [&](const stb::mat32f& in) { return stb::unsharp_mask(in, sigma, amount, threshold, e); });
        },
        "src"_a, "sigma"_a, "amount"_a = 1.0f, "threshold"_a = 0.0f, nb::kw_only(), "edge"_a = "clamp");
    image_cls.def(
        "unsharp_mask",
        [](nb::handle self, float sigma, float amount, float threshold, nb::handle edge, bool inplace) {
            const stb::edge_mode e = edge_of(edge);
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::unsharp_mask(i, sigma, amount, threshold, e); },
                [&](stb::image& i) { stb::unsharp_mask_inplace(i, sigma, amount, threshold, e); });
        },
        "sigma"_a, "amount"_a = 1.0f, "threshold"_a = 0.0f, nb::kw_only(), "edge"_a = "clamp", "inplace"_a = false,
        "Unsharp mask: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- pixelize
    const std::string pixelize_doc =
        std::string("Cuts the picture into blocks of block_w x block_h values (block_h 0: the same as block_w), from the top left, and\n"
                    "fills each block with its mean; the blocks at the right and bottom edges are smaller. The colour of an image with\n"
                    "alpha is the mean weighted by alpha, and the alpha of a block is the mean of its alpha.\n") + alpha_doc;
    m.def(
        "pixelize",
        [](const stb::image& img, int block_w, int block_h) -> stb::image {
            nb::gil_scoped_release release;
            return stb::pixelize(img, block_w, block_h);
        },
        "src"_a, "block_w"_a, "block_h"_a = 0, pixelize_doc.c_str());
    m.def(
        "pixelize",
        [](const f32_array& src, int block_w, int block_h) -> f32_result {
            return run_f32(src, [&](const stb::mat32f& in) { return stb::pixelize(in, block_w, block_h); });
        },
        "src"_a, "block_w"_a, "block_h"_a = 0);
    image_cls.def(
        "pixelize",
        [](nb::handle self, int block_w, int block_h, bool inplace) {
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::pixelize(i, block_w, block_h); },
                [&](stb::image& i) { stb::pixelize_inplace(i, block_w, block_h); });
        },
        "block_w"_a, "block_h"_a = 0, nb::kw_only(), "inplace"_a = false,
        "Pixelizes: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- convert_color
    const std::string color_doc =
        std::string("Converts the colour channels from the space `from_space` to `to_space` (ColorSpace, or \"rgb\", \"hsv\", \"hsl\",\n"
                    "\"ycbcr\"). 3 or 4 channels (an alpha channel is never changed); the array keeps its type and size and its\n"
                    "channels become (H, S, V), (H, S, L), (Y, Cb, Cr) or (R, G, B). In 8 bits every channel is 0 to 255 and the hue has\n"
                    "256 steps for the full turn; in float32 every channel is 0 to 1 and the hue is a fraction of the turn. YCbCr is the\n"
                    "full-range BT.601 of JPEG.\n");
    m.def(
        "convert_color",
        [](const stb::image& img, nb::handle from_space, nb::handle to_space) -> stb::image {
            const stb::color_space a = space_of(from_space, "from_space"), b = space_of(to_space, "to_space");
            nb::gil_scoped_release release;
            return stb::convert_color(img, a, b);
        },
        "src"_a, "from_space"_a, "to_space"_a, color_doc.c_str());
    m.def(
        "convert_color",
        [](const f32_array& src, nb::handle from_space, nb::handle to_space) -> f32_result {
            const stb::color_space a = space_of(from_space, "from_space"), b = space_of(to_space, "to_space");
            return run_f32(src, [&](const stb::mat32f& in) { return stb::convert_color(in, a, b); });
        },
        "src"_a, "from_space"_a, "to_space"_a);
    image_cls.def(
        "convert_color",
        [](nb::handle self, nb::handle from_space, nb::handle to_space, bool inplace) {
            const stb::color_space a = space_of(from_space, "from_space"), b = space_of(to_space, "to_space");
            return image_form(
                self, inplace, [&](const stb::image& i) { return stb::convert_color(i, a, b); },
                [&](stb::image& i) { stb::convert_color_inplace(i, a, b); });
        },
        "from_space"_a, "to_space"_a, nb::kw_only(), "inplace"_a = false,
        "Converts the colour space: a new Image, or this one changed (and returned) with inplace=True.");

    // ---- metrics
    const char* pair_doc =
        "Both pictures are Images, or both float32 arrays, of the same size and channels. Every channel counts, alpha included.";
    const auto both = [&](const char* name, double (*on_u8)(const stb::mat8u&, const stb::mat8u&),
                          double (*on_f32)(const stb::mat32f&, const stb::mat32f&), const std::string& doc) {
        m.def(
            name,
            [on_u8](const stb::image& a, const stb::image& b) {
                nb::gil_scoped_release release;
                return on_u8(a, b);
            },
            "a"_a, "b"_a, doc.c_str());
        m.def(
            name,
            [on_f32](const f32_array& a, const f32_array& b) {
                const stb::mat32f ma = view_of(a), mb = view_of(b);
                nb::gil_scoped_release release;
                return on_f32(ma, mb);
            },
            "a"_a, "b"_a);
    };
    both("mae", stb::mae, stb::mae,
         std::string("The mean absolute difference, in levels (0 to 255) for Images and in value units for float32 arrays.\n") + pair_doc);
    both("mse", stb::mse, stb::mse, std::string("The mean squared difference (levels^2 for Images).\n") + pair_doc);
    both("psnr", stb::psnr, stb::psnr,
         std::string("Peak signal-to-noise ratio in dB (peak 255 for Images, 1 for float32 arrays); inf for equal pictures.\n") + pair_doc);
    both("ssim", stb::ssim, stb::ssim,
         std::string("Structural similarity (Wang et al. 2004: Gaussian window 11 x 11, sigma 1.5, like scikit-image's\n"
                     "structural_similarity(gaussian_weights=True)): the mean of ssim_map(). The picture must be at least 11 x 11.\n") + pair_doc);
    m.def(
        "ssim_map",
        [](const stb::image& a, const stb::image& b) {
            stb::mat32f out;
            {
                nb::gil_scoped_release release;
                out = stb::ssim_map(a, b);
            }
            return to_array(std::move(out), true);
        },
        "a"_a, "b"_a,
        "The local SSIM values as a float32 array of shape (H - 10, W - 10, C): one per window that lies inside the picture.");
    m.def(
        "ssim_map",
        [](const f32_array& a, const f32_array& b) {
            const bool with_channels = a.ndim() == 3;
            const stb::mat32f ma = view_of(a), mb = view_of(b);
            stb::mat32f out;
            {
                nb::gil_scoped_release release;
                out = stb::ssim_map(ma, mb);
            }
            return to_array(std::move(out), with_channels);
        },
        "a"_a, "b"_a);
}

// ---- scharr, thresholds, morphology, connected components and canny (stb/threshold.hpp, morphology.hpp, components.hpp, filter.hpp)

stb::morph_shape shape_of(nb::handle h) {
    using stb::morph_shape;
    return to_enum<morph_shape>(h, {{"rect", morph_shape::rect}, {"ellipse", morph_shape::ellipse}, {"cross", morph_shape::cross}}, "shape");
}

stb::adaptive_method adaptive_of(nb::handle h) {
    using stb::adaptive_method;
    return to_enum<adaptive_method>(h, {{"mean", adaptive_method::mean}, {"gaussian", adaptive_method::gaussian}}, "method");
}

stb::connectivity connectivity_of(nb::handle h) {
    using stb::connectivity;
    return to_enum<connectivity>(h, {{"four", connectivity::four}, {"eight", connectivity::eight}}, "connectivity");
}

// A mask (a mat8u of one channel) as a gray Image: the masks of this section are given back as Images, whatever the input was.
stb::image mask_image(const stb::mat8u& mask) {
    if (mask.empty()) return stb::image();
    stb::image out(mask.width(), mask.height(), stb::pixel_format::gray);
    std::memcpy(out.data(), mask.data(), mask.size());
    return out;
}

// The statistics of the pieces as a float64 array (count, 7): area, x, y, width, height, cx, cy.
nb::object stats_array(const stb::components& c) {
    auto* data = new std::vector<double>(std::max<std::size_t>(1, c.stats.size() * 7));
    for (std::size_t i = 0; i < c.stats.size(); ++i) {
        const stb::component_stat& s = c.stats[i];
        const double v[7] = {double(s.area), double(s.x), double(s.y), double(s.width), double(s.height), double(s.cx), double(s.cy)};
        for (std::size_t k = 0; k < 7; ++k) (*data)[i * 7 + k] = v[k];
    }
    nb::capsule owner(data, [](void* p) noexcept { delete static_cast<std::vector<double>*>(p); });
    const std::size_t shape[2] = {c.stats.size(), 7};
    return nb::cast(nb::ndarray<nb::numpy, double, nb::ndim<2>>(data->data(), 2, shape, owner));
}

// One morphology operation: module function on an Image and on a float32 array, and an Image method.
template <class CopyFn, class InplaceFn>
void def_morphology(nb::module_& m, nb::class_<stb::image>& image_cls, const char* name, const std::string& doc, CopyFn copy_fn,
                    InplaceFn inplace_fn) {
    m.def(
        name,
        [copy_fn](const stb::image& img, const stb::image& se, nb::handle edge) -> stb::image {
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return copy_fn(img, static_cast<const stb::mat8u&>(se), e);
        },
        "src"_a, "se"_a, nb::kw_only(), "edge"_a = "clamp", doc.c_str());
    m.def(
        name,
        [copy_fn](const f32_array& src, const stb::image& se, nb::handle edge) -> f32_result {
            const stb::edge_mode e = edge_of(edge);
            return run_f32(src, [&](const stb::mat32f& in) { return copy_fn(in, static_cast<const stb::mat8u&>(se), e); });
        },
        "src"_a, "se"_a, nb::kw_only(), "edge"_a = "clamp");
    image_cls.def(
        name,
        [copy_fn, inplace_fn](nb::handle self, const stb::image& se, nb::handle edge, bool inplace) {
            const stb::edge_mode e = edge_of(edge);
            return image_form(
                self, inplace, [&](const stb::image& i) { return copy_fn(i, static_cast<const stb::mat8u&>(se), e); },
                [&](stb::image& i) { inplace_fn(i, static_cast<const stb::mat8u&>(se), e); });
        },
        "se"_a, nb::kw_only(), "edge"_a = "clamp", "inplace"_a = false,
        "A new Image, or this one changed (and returned) with inplace=True; see the module function of the same name.");
}

void bind_segmentation(nb::module_& m, nb::class_<stb::image>& image_cls) {
    nb::enum_<stb::morph_shape>(m, "MorphShape", "The shape of structuring_element(); the lower-case names \"rect\", \"ellipse\", \"cross\" work too.")
        .value("RECT", stb::morph_shape::rect)
        .value("ELLIPSE", stb::morph_shape::ellipse)
        .value("CROSS", stb::morph_shape::cross);
    nb::enum_<stb::adaptive_method>(m, "AdaptiveMethod", "How adaptive_threshold weighs the neighbourhood: \"mean\" or \"gaussian\".")
        .value("MEAN", stb::adaptive_method::mean)
        .value("GAUSSIAN", stb::adaptive_method::gaussian);
    nb::enum_<stb::connectivity>(m, "Connectivity", "Which values touch in connected_components: \"four\" (an edge) or \"eight\" (an edge or a corner).")
        .value("FOUR", stb::connectivity::four)
        .value("EIGHT", stb::connectivity::eight);

    // ---- scharr
    m.def(
        "scharr",
        [](const stb::image& img, nb::handle edge) {
            const stb::edge_mode e = edge_of(edge);
            stb::gradients g;
            {
                nb::gil_scoped_release release;
                g = stb::scharr(img, e);
            }
            return gradients_tuple(std::move(g), true);
        },
        "src"_a, nb::kw_only(), "edge"_a = "clamp",
        "Like sobel() with {3, 10, 3} as the smoothing across: a tuple (dx, dy) of float32 arrays. A step of height 1 gives 16.");
    m.def(
        "scharr",
        [](const f32_array& src, nb::handle edge) {
            const stb::edge_mode e = edge_of(edge);
            const bool with_channels = src.ndim() == 3;
            const stb::mat32f in = view_of(src);
            stb::gradients g;
            {
                nb::gil_scoped_release release;
                g = stb::scharr(in, e);
            }
            return gradients_tuple(std::move(g), with_channels);
        },
        "src"_a, nb::kw_only(), "edge"_a = "clamp");

    // ---- thresholds: the masks are gray Images of 0 and 255
    m.def(
        "threshold",
        [](const stb::image& img, int t, bool invert) {
            if (t < 0 || t > 255) throw std::invalid_argument("t must be a level from 0 to 255");
            nb::gil_scoped_release release;
            return mask_image(stb::threshold(img, std::uint8_t(t), invert));
        },
        "src"_a, "t"_a, "invert"_a = false,
        "The mask (a gray Image) of the values above `t`: 255 where src > t, else 0; invert swaps the two. src is an Image of one\n"
        "channel with t a level 0 to 255, or a float32 array (H, W) with t a value of the 0 to 1 range. out = 255 * H(src - t) with\n"
        "H the step function.");
    m.def(
        "threshold",
        [](const f32_array& src, float t, bool invert) {
            const stb::mat32f in = view_of(src);
            nb::gil_scoped_release release;
            return mask_image(stb::threshold(in, t, invert));
        },
        "src"_a, "t"_a, "invert"_a = false);
    m.def(
        "otsu_threshold",
        [](const stb::image& img) {
            nb::gil_scoped_release release;
            return int(stb::otsu_threshold(img));
        },
        "src"_a,
        "Otsu's level (an int for an Image, a float for a float32 array): the one that best splits the histogram in two, as\n"
        "scikit-image's threshold_otsu. Use it as the `t` of threshold().");
    m.def(
        "otsu_threshold",
        [](const f32_array& src) {
            const stb::mat32f in = view_of(src);
            nb::gil_scoped_release release;
            return double(stb::otsu_threshold(in));
        },
        "src"_a);
    const std::string adaptive_doc =
        std::string("The mask (a gray Image) of the values above the local level of their neighbourhood: 255 where\n"
                    "src > local - offset, `local` being the mean or the Gaussian-weighted mean of the (2 * radius + 1)-square around the value.\n"
                    "`offset` is in levels for an Image and in values for a float32 array. Takes one channel.\n") + edge_doc;
    m.def(
        "adaptive_threshold",
        [](const stb::image& img, int radius, float offset, nb::handle method, bool invert, nb::handle edge) {
            const stb::adaptive_method a = adaptive_of(method);
            const stb::edge_mode e = edge_of(edge);
            nb::gil_scoped_release release;
            return mask_image(stb::adaptive_threshold(img, radius, offset, a, invert, e));
        },
        "src"_a, "radius"_a, "offset"_a = 0.0f, "method"_a = "gaussian", "invert"_a = false, nb::kw_only(), "edge"_a = "clamp",
        adaptive_doc.c_str());
    m.def(
        "adaptive_threshold",
        [](const f32_array& src, int radius, float offset, nb::handle method, bool invert, nb::handle edge) {
            const stb::adaptive_method a = adaptive_of(method);
            const stb::edge_mode e = edge_of(edge);
            const stb::mat32f in = view_of(src);
            nb::gil_scoped_release release;
            return mask_image(stb::adaptive_threshold(in, radius, offset, a, invert, e));
        },
        "src"_a, "radius"_a, "offset"_a = 0.0f, "method"_a = "gaussian", "invert"_a = false, nb::kw_only(), "edge"_a = "clamp");

    // ---- morphology
    m.def(
        "structuring_element",
        [](nb::handle shape, int width, int height) { return mask_image(stb::structuring_element(shape_of(shape), width, height)); },
        "shape"_a, "width"_a, "height"_a = 0,
        "A structuring element as a gray Image (members 255): \"rect\", \"ellipse\" or \"cross\" (or a MorphShape), width x height\n"
        "values, both odd (height 0: the same as the width). Any odd-sized gray Image with non-zero members works as an element too.");
    const std::string morph_doc =
        std::string("Takes an Image (a new Image) or a float32 array (a new array), any number of channels, and the structuring element\n"
                    "`se` (a gray Image). Each channel is done on its own. ") +
        edge_doc;
    def_morphology(
        m, image_cls, "erode", "The smallest value under the element for every position.\n" + morph_doc,
        [](const auto& s, const stb::mat8u& se, stb::edge_mode e) { return stb::erode(s, se, e); },
        [](stb::image& i, const stb::mat8u& se, stb::edge_mode e) { stb::erode_inplace(i, se, e); });
    def_morphology(
        m, image_cls, "dilate", "The largest value under the element for every position.\n" + morph_doc,
        [](const auto& s, const stb::mat8u& se, stb::edge_mode e) { return stb::dilate(s, se, e); },
        [](stb::image& i, const stb::mat8u& se, stb::edge_mode e) { stb::dilate_inplace(i, se, e); });
    def_morphology(
        m, image_cls, "morph_open", "Erode, then dilate: removes small bright parts.\n" + morph_doc,
        [](const auto& s, const stb::mat8u& se, stb::edge_mode e) { return stb::morph_open(s, se, e); },
        [](stb::image& i, const stb::mat8u& se, stb::edge_mode e) { stb::morph_open_inplace(i, se, e); });
    def_morphology(
        m, image_cls, "morph_close", "Dilate, then erode: fills small dark holes.\n" + morph_doc,
        [](const auto& s, const stb::mat8u& se, stb::edge_mode e) { return stb::morph_close(s, se, e); },
        [](stb::image& i, const stb::mat8u& se, stb::edge_mode e) { stb::morph_close_inplace(i, se, e); });
    def_morphology(
        m, image_cls, "morph_gradient", "dilate - erode: the outline of the shapes.\n" + morph_doc,
        [](const auto& s, const stb::mat8u& se, stb::edge_mode e) { return stb::morph_gradient(s, se, e); },
        [](stb::image& i, const stb::mat8u& se, stb::edge_mode e) { stb::morph_gradient_inplace(i, se, e); });
    def_morphology(
        m, image_cls, "top_hat", "src - morph_open(src): the small bright parts (never below 0).\n" + morph_doc,
        [](const auto& s, const stb::mat8u& se, stb::edge_mode e) { return stb::top_hat(s, se, e); },
        [](stb::image& i, const stb::mat8u& se, stb::edge_mode e) { stb::top_hat_inplace(i, se, e); });
    def_morphology(
        m, image_cls, "black_hat", "morph_close(src) - src: the small dark parts (never below 0).\n" + morph_doc,
        [](const auto& s, const stb::mat8u& se, stb::edge_mode e) { return stb::black_hat(s, se, e); },
        [](stb::image& i, const stb::mat8u& se, stb::edge_mode e) { stb::black_hat_inplace(i, se, e); });

    // ---- connected components
    nb::class_<stb::components>(m, "Components",
                                "The pieces of a mask: `labels` (int32 array (H, W), 0 the background, 1..count in the order of the first value of\n"
                                "each piece), `count`, and `stats` (float64 array (count, 7): area, x, y, width, height, cx, cy of the piece\n"
                                "labelled i + 1 in row i; cx and cy are the centre of mass).")
        .def_prop_ro("count", [](const stb::components& c) { return c.count; })
        .def_prop_ro("labels",
                     [](nb::handle self) {
                         const stb::components& c = nb::cast<const stb::components&>(self);
                         static const std::int32_t none = 0;
                         const std::size_t shape[2] = {std::size_t(c.labels.height()), std::size_t(c.labels.width())};
                         const std::int32_t* data = c.labels.empty() ? &none : c.labels.data();
                         return nb::cast(nb::ndarray<nb::numpy, const std::int32_t, nb::ndim<2>>(data, 2, shape, self));  // read-only, keeps the object alive
                     })
        .def_prop_ro("stats", [](const stb::components& c) { return stats_array(c); })
        .def("__repr__", [](const stb::components& c) { return "<libstb.Components count=" + std::to_string(c.count) + ">"; });
    m.def(
        "connected_components",
        [](const stb::image& mask, nb::handle conn) {
            const stb::connectivity c = connectivity_of(conn);
            nb::gil_scoped_release release;
            return stb::connected_components(mask, c);
        },
        "mask"_a, "connectivity"_a = "four",
        "The pieces of the non-zero values of a mask (a gray Image) as a Components. connectivity is \"four\" (values that share\n"
        "an edge touch) or \"eight\" (an edge or a corner). With an int32 array (H, W) of labels instead of an Image, the pieces are\n"
        "the touching values of the same label, and a negative label is the background.");
    m.def(
        "connected_components",
        [](const nb::ndarray<const std::int32_t, nb::c_contig, nb::device::cpu>& labels, nb::handle conn) {
            if (labels.ndim() != 2) throw std::invalid_argument("expected an int32 array of shape (H, W)");
            const stb::connectivity c = connectivity_of(conn);
            stb::mat32i map(int(labels.shape(1)), int(labels.shape(0)), 1);
            if (labels.size() != 0) std::memcpy(map.data(), labels.data(), labels.size() * sizeof(std::int32_t));
            nb::gil_scoped_release release;
            return stb::connected_components(map, c);
        },
        "labels"_a, "connectivity"_a = "four");

    // ---- canny
    const std::string canny_doc =
        "The edges of a picture as a mask (a gray Image, 255 on an edge, one value wide). Takes an Image or a float32 array of 1, 3 or 4\n"
        "channels (3 and 4 are read as luma). A Gaussian blur of `sigma` (0: none), the Sobel gradient, the thinning to the ridge, and the\n"
        "hysteresis: a ridge point is an edge if its magnitude is at least `high`, or at least `low` and connected to such a point. The\n"
        "limits are fractions of a step from 0 to 1 across the picture (the Sobel magnitude divided by 4).";
    m.def(
        "canny",
        [](const stb::image& img, float low, float high, float sigma) {
            nb::gil_scoped_release release;
            return mask_image(stb::canny(img, low, high, sigma));
        },
        "src"_a, "low"_a = 0.1f, "high"_a = 0.2f, "sigma"_a = 1.0f, canny_doc.c_str());
    m.def(
        "canny",
        [](const f32_array& src, float low, float high, float sigma) {
            const stb::mat32f in = view_of(src);
            nb::gil_scoped_release release;
            return mask_image(stb::canny(in, low, high, sigma));
        },
        "src"_a, "low"_a = 0.1f, "high"_a = 0.2f, "sigma"_a = 1.0f);
    image_cls.def(
        "canny", [](const stb::image& img, float low, float high, float sigma) {
            nb::gil_scoped_release release;
            return mask_image(stb::canny(img, low, high, sigma));
        },
        "low"_a = 0.1f, "high"_a = 0.2f, "sigma"_a = 1.0f, "The edges of this Image as a gray Image mask; see libstb.canny.");
    image_cls.def(
        "threshold",
        [](const stb::image& img, int t, bool invert) {
            if (t < 0 || t > 255) throw std::invalid_argument("t must be a level from 0 to 255");
            nb::gil_scoped_release release;
            return mask_image(stb::threshold(img, std::uint8_t(t), invert));
        },
        "t"_a, "invert"_a = false, "The mask of the values above `t` as a gray Image; see libstb.threshold.");
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
    bind_image_ops(m, image_cls);
    bind_segmentation(m, image_cls);
}
