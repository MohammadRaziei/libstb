// Python.h (through nanobind) must come before any standard header (it defines _POSIX_C_SOURCE and _XOPEN_SOURCE)
#include <nanobind/nanobind.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <string>
#include <utility>
#include <vector>

#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include "binding.hpp"
#include "stb.h"
#include "stb_kinds.hpp"

// Filters (gaussian_blur, box_blur, convolve, sobel), the tone functions (histogram, equalize, gamma, ...), the 8-bit <-> float
// conversions and superpixels.
// Every filter takes an Image (8 bits) or a float32 numpy array of shape (H, W) or (H, W, C), and returns
// the same kind; they are module functions and, for the Image, methods too.

namespace nb = nanobind;
using namespace nb::literals;

namespace {

using polybind::def_dispatch;
using stb_py::stb_kinds;

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

stb::interpolation_method interpolation_of(nb::handle h) {
    using stb::interpolation_method;
    return to_enum<interpolation_method>(h, {{"nearest", interpolation_method::nearest}, {"bilinear", interpolation_method::bilinear}, {"bicubic", interpolation_method::bicubic}},
                                  "interpolation");
}

// A Matrix, or a sequence of the six numbers a, b, c, d, e, f.
stb::matrix matrix_of(nb::handle h) {
    if (nb::isinstance<stb::matrix>(h)) return nb::cast<stb::matrix>(h);
    std::vector<float> v;
    if (nb::try_cast(h, v, /*convert=*/true) && v.size() == 6) return stb::matrix{v[0], v[1], v[2], v[3], v[4], v[5]};
    throw std::invalid_argument("expected a Matrix or a sequence of 6 numbers (a, b, c, d, e, f)");
}

// A Homography, a Matrix, or a sequence of 9 numbers (by rows) or of the 6 of a matrix.
stb::homography homography_of(nb::handle h) {
    if (nb::isinstance<stb::homography>(h)) return nb::cast<stb::homography>(h);
    if (nb::isinstance<stb::matrix>(h)) return stb::homography(nb::cast<stb::matrix>(h));
    std::vector<float> v;
    if (nb::try_cast(h, v, /*convert=*/true)) {
        if (v.size() == 9) {
            stb::homography r;
            for (std::size_t i = 0; i < 9; ++i) r.m[i] = v[i];
            return r;
        }
        if (v.size() == 6) return stb::homography(stb::matrix{v[0], v[1], v[2], v[3], v[4], v[5]});
    }
    throw std::invalid_argument("expected a Homography, a Matrix, or a sequence of 9 numbers (by rows)");
}

stb::superpixel_method method_of(nb::handle h) {
    using stb::superpixel_method;
    return to_enum<superpixel_method>(
        h,
        {{"slic", superpixel_method::slic},
         {"slico", superpixel_method::slico},
         {"snic", superpixel_method::snic},
         {"sicle", superpixel_method::sicle}},
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

// The kinds a function may take: the 8-bit ones (an Image, a uint8 array) or the float one.
inline constexpr polybind::kinds<stb::image, stb::mat8u> byte_kinds{};
inline constexpr polybind::kinds<stb::mat32f> float_kinds{};
// Every kind that cast() reads: an Image and an array of any of the pixel types.
inline constexpr polybind::kinds<stb::image, stb::mat8u, stb::mat<std::uint16_t>, stb::mat<std::uint32_t>, stb::mat<std::int16_t>,
                                 stb::mat<std::int32_t>, stb::mat32f, stb::mat<double>>
    cast_kinds{};

// The type of the argument of a generic lambda (`const auto& x`) without const and reference.
template <class T>
using kind_of = std::decay_t<T>;

// A filter of a uint8 input gives an image8u (the class); the result must be of the kind of the argument, so a uint8 array
// (a mat8u) gets a mat8u back and an Image an Image.
template <class M, class R>
M of_kind(R&& r) {
    if constexpr (std::is_same_v<M, std::decay_t<R>>) return std::forward<R>(r);
    else return M(std::move(r));
}

// What every function that takes an Image or an array says about its arguments and result.
const std::string kinds_doc =
    "Takes an Image, a uint8 array (0 to 255) or a float32 array (0 to 1) of shape (H, W) or (H, W, C) and gives a new one of the same\n"
    "kind; the argument is not changed and an array is not converted (any other type is a TypeError).\n";

// A kernel (a list or an array) is read with the GIL: the function itself runs without it.
stb::mat32f kernel_with_gil(nb::handle h) {
    nb::gil_scoped_acquire gil;
    return kernel_of(h);
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

// What histogram() gives, as a type of its own: an int32 matrix (stb::mat32i) is an array kind of its own for cast().
struct hist_value {
    stb::mat32i h;
};

}  // namespace

// A histogram as the int32 array of shape (channels, bins) that Python gets.
template <>
struct polybind::nanobind_backend::kind<hist_value> {
    using new_ret = i32_result;
    static constexpr auto new_policy = nb::rv_policy::automatic;
    static new_ret wrap(hist_value&& v) { return histogram_array(v.h); }
};

namespace {

// The pixel types cast() knows: the ones a numpy array of a picture has.
using u16_mat = stb::mat<std::uint16_t>;
using u32_mat = stb::mat<std::uint32_t>;
using i16_mat = stb::mat<std::int16_t>;
using i32_mat = stb::mat<std::int32_t>;
using f64_mat = stb::mat<double>;

// The result of cast(): an array of any of those types or an Image, which one is known only when the type is read from Python.
using cast_value = std::variant<stb::mat8u, u16_mat, u32_mat, i16_mat, i32_mat, stb::mat32f, f64_mat, stb::image>;

enum class pixel_type { uint8, uint16, uint32, int16, int32, float32, float64 };

// "uint8", "uint16", "uint32", "int16", "int32", "float32" or "float64", or a numpy dtype or scalar type (numpy.float32): read with the GIL.
pixel_type pixel_type_of(nb::handle h) {
    std::string name;
    if (nb::isinstance<nb::str>(h)) name = nb::cast<std::string>(h);
    else if (nb::hasattr(h, "name")) name = nb::cast<std::string>(h.attr("name"));
    else if (nb::hasattr(h, "__name__")) name = nb::cast<std::string>(h.attr("__name__"));
    else throw std::invalid_argument("the type is a name (\"uint8\", \"float32\", ...) or a numpy dtype");
    static const std::pair<const char*, pixel_type> names[] = {
        {"uint8", pixel_type::uint8},   {"uint16", pixel_type::uint16}, {"uint32", pixel_type::uint32},   {"int16", pixel_type::int16},
        {"int32", pixel_type::int32},   {"float32", pixel_type::float32}, {"float64", pixel_type::float64}};
    for (const auto& [n, t] : names)
        if (name == n) return t;
    throw std::invalid_argument("cannot cast to \"" + name +
                                "\": use \"uint8\", \"uint16\", \"uint32\", \"int16\", \"int32\", \"float32\" or \"float64\"");
}

// stb::cast<To>(x) as a cast_value: an Image to uint8 stays an Image, every other result is an array (a mat8u for uint8).
template <class To, class M>
cast_value cast_to(const M& x) {
    auto r = stb::cast<To>(x);
    if constexpr (std::is_same_v<To, std::uint8_t>) {
        if constexpr (std::is_same_v<M, stb::image>) return cast_value(std::move(r));
        else return cast_value(stb::mat8u(std::move(r)));
    } else {
        return cast_value(std::move(r));
    }
}

template <class M>
cast_value cast_as(const M& x, pixel_type t) {
    switch (t) {
        case pixel_type::uint8: return cast_to<std::uint8_t>(x);
        case pixel_type::uint16: return cast_to<std::uint16_t>(x);
        case pixel_type::uint32: return cast_to<std::uint32_t>(x);
        case pixel_type::int16: return cast_to<std::int16_t>(x);
        case pixel_type::int32: return cast_to<std::int32_t>(x);
        case pixel_type::float32: return cast_to<float>(x);
        case pixel_type::float64: return cast_to<double>(x);
    }
    throw std::logic_error("unknown pixel type");
}

}  // namespace

// An array (with the shape of the argument) or an Image, as the Python object.
template <>
struct polybind::nanobind_backend::kind<cast_value> {
    using new_ret = nb::object;
    static constexpr auto new_policy = nb::rv_policy::automatic;
    template <class H>
    static new_ret wrap(cast_value&& v, const H& like) {
        return std::visit(
            [&](auto&& a) -> nb::object {
                using A = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<A, stb::image>) return nb::cast(std::move(a));
                else return nb::cast(kind<A>::wrap(std::move(a), like));
            },
            std::move(v));
    }
};

namespace {

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

constexpr const char* tone_doc =
    "Takes an Image, a uint8 array (0 to 255) or a float32 array (0 to 1) of shape (H, W) or (H, W, C) and gives a new one of the same\n"
    "kind; the argument is not changed and an array is not converted (any other type is a TypeError). 1 to 4 channels: gray,\n"
    "gray + alpha, RGB, RGBA; the alpha channel is never changed.";
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
    const char* histogram_doc =
        "How many pixels have each value, as an int32 array of shape (channels, 256): row k is channel k (alpha included).\n"
        "For a float32 array: `bins` equal bins over [0, 1] (values outside are counted in the first or last bin, NaN not\n"
        "at all) and shape (channels, bins).";
    def_dispatch(m, image_cls, "histogram", byte_kinds,
                 [](const auto& x) { return hist_value{stb::histogram(x)}; },
                 "src"_a, histogram_doc);
    def_dispatch(m, image_cls, "histogram", float_kinds,
                 [](const auto& x, int bins) { return hist_value{stb::histogram(x, bins)}; },
                 "src"_a, "bins"_a = 256, histogram_doc);

    // ---- equalize
    const std::string equalize_doc =
        std::string("Spreads the levels so that each one is used about as often (histogram equalisation). A picture of one value\n"
                    "is returned unchanged.\n") + tone_doc + "\n" + mode_doc;
    def_dispatch(m, image_cls, "equalize", stb_kinds,
                 [](const auto& x, stb::tone_mode mode) { return stb::equalize(x, mode); },
                 "src"_a, "mode"_a = "luma", equalize_doc.c_str());

    // ---- auto_contrast
    const std::string auto_doc =
        std::string("Stretches the values to the whole range after dropping the darkest and brightest `cutoff` percent (0 up to\n"
                    "but not including 50), like Pillow's ImageOps.autocontrast.\n") + tone_doc + "\n" + mode_doc;
    def_dispatch(m, image_cls, "auto_contrast", stb_kinds,
                 [](const auto& x, float cutoff, stb::tone_mode mode) { return stb::auto_contrast(x, cutoff, mode); },
                 "src"_a, "cutoff"_a = 0.0f, "mode"_a = "luma", auto_doc.c_str());

    // ---- clahe
    const std::string clahe_doc =
        std::string("Contrast-limited adaptive histogram equalisation: every one of tiles_x x tiles_y tiles (tiles_y 0 means the same as\\n"
                    "tiles_x) is equalised on its own with its histogram cut at `clip_limit` times the mean count of a bin (at least 1; 1\\n"
                    "spreads nothing, 2 to 4 is usual), and each pixel mixes the mappings of its four nearest tiles. A tile of one value is\\n"
                    "left as it is. ValueError for a bad clip_limit or tile count, or more tiles than pixels along an axis.\\n") + tone_doc + "\\n" + mode_doc;
    def_dispatch(m, image_cls, "clahe", stb_kinds,
                 [](const auto& x, float clip_limit, int tiles_x, int tiles_y, stb::tone_mode mode) {
                     return stb::clahe(x, clip_limit, tiles_x, tiles_y, mode);
                 },
                 "src"_a, "clip_limit"_a = 2.0f, "tiles_x"_a = 8, "tiles_y"_a = 0, "mode"_a = "luma", clahe_doc.c_str());

    // ---- match_histogram
    const std::string match_doc =
        std::string("Moves the levels so that the histogram follows the one of `reference` (an Image, or a float32 array for a\n"
                    "float32 `src`; any size, the same number of channels).\n") + tone_doc + "\n" + mode_doc;
    def_dispatch(m, image_cls, "match_histogram", stb_kinds,
                 [](const auto& x, const auto& reference, stb::tone_mode mode) { return stb::match_histogram(x, reference, mode); },
                 "src"_a, "reference"_a, "mode"_a = "luma", match_doc.c_str());

    // ---- gamma
    const std::string gamma_doc =
        std::string("Every colour channel c becomes c ** (1 / g): g above 1 brightens, below 1 darkens. g must be a positive\n"
                    "finite number.\n") + tone_doc;
    def_dispatch(m, image_cls, "gamma", stb_kinds,
                 [](const auto& x, float g) { return stb::gamma(x, g); },
                 "src"_a, "g"_a, gamma_doc.c_str());

    // ---- levels
    const std::string levels_doc =
        std::string("The levels adjustment of an image editor: [in_black, in_white] is stretched to 0..1, raised to 1 / gamma and\n"
                    "mapped to [out_black, out_white]. All levels are fractions of the full range, 0 <= in_black < in_white <= 1.\n") +
        tone_doc;
    def_dispatch(m, image_cls, "levels", stb_kinds,
                 [](const auto& x, float in_black, float in_white, float g, float out_black, float out_white) {
                     return stb::levels(x, in_black, in_white, g, out_black, out_white);
                 },
                 "src"_a, "in_black"_a, "in_white"_a, "g"_a = 1.0f, "out_black"_a = 0.0f, "out_white"_a = 1.0f, levels_doc.c_str());

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
    def_dispatch(m, image_cls, "median_filter", stb_kinds,
                 [](const auto& x, int radius, stb::edge_mode edge) {
                     return of_kind<kind_of<decltype(x)>>(stb::median_filter(x, radius, edge));
                 },
                 "src"_a, "radius"_a, nb::kw_only(), "edge"_a = "clamp", (std::string(median_doc) + kinds_doc).c_str());

    // ---- bilateral_filter
    const std::string bilateral_doc =
        std::string("Smooths without blurring edges: each colour channel becomes the mean of the (2 * radius + 1)-square around it\n"
                    "(radius 0 to 1000; 0 changes nothing), weighted by distance in pixels (sigma_space) and by how different the colour is\n"
                    "from the middle pixel's (sigma_range, in units of the full range 0 to 1; all colour channels together). Alpha is copied.\n"
                    "Takes an Image (a new Image) or a float32 array of shape (H, W) or (H, W, C) (a new array); 1 to 4 channels.\n") +
        edge_doc;
    def_dispatch(m, image_cls, "bilateral_filter", stb_kinds,
                 [](const auto& x, int radius, float sigma_space, float sigma_range, stb::edge_mode edge) {
                     return of_kind<kind_of<decltype(x)>>(stb::bilateral_filter(x, radius, sigma_space, sigma_range, edge));
                 },
                 "src"_a, "radius"_a, "sigma_space"_a, "sigma_range"_a, nb::kw_only(), "edge"_a = "clamp", (std::string(bilateral_doc) + kinds_doc).c_str());

    // ---- guided_filter
    const std::string guided_doc =
        std::string("Smooths keeping the edges of a guide, at a cost that does not depend on the radius. In every (2 * radius + 1)-square the\n"
                    "output is the linear function of the guide that fits the image best, `eps` (in units of the full range squared, 1e-3 is\n"
                    "gentle) keeping the slope down. The guide is the image itself (guide=None) or another Image / float32 array of the same\n"
                    "size, gray or colour (3 or 4 channels: a colour guide uses its 3 x 3 covariance). Alpha is copied. radius 0 changes\n"
                    "nothing. Takes an Image (a new Image) or a float32 array of shape (H, W) or (H, W, C) (a new array).\n") +
        edge_doc;
    def_dispatch(m, image_cls, "guided_filter", stb_kinds,
                 [](const auto& x, int radius, float eps, stb::edge_mode edge) {
                     return of_kind<kind_of<decltype(x)>>(stb::guided_filter(x, radius, eps, edge));
                 },
                 "src"_a, "radius"_a, "eps"_a, nb::kw_only(), "edge"_a = "clamp", (std::string(guided_doc) + kinds_doc).c_str());
    def_dispatch(m, image_cls, "guided_filter", stb_kinds,
                 [](const auto& x, int radius, float eps, const auto& guide, stb::edge_mode edge) {
                     return of_kind<kind_of<decltype(x)>>(stb::guided_filter(x, guide, radius, eps, edge));
                 },
                 "src"_a, "radius"_a, "eps"_a, "guide"_a, nb::kw_only(), "edge"_a = "clamp", (std::string(guided_doc) + kinds_doc).c_str());

    // ---- warp_affine, warp_perspective
    nb::enum_<stb::interpolation_method>(m, "InterpolationMethod",
                                  "How a warp reads a value between pixel centres. Functions that take an InterpolationMethod also take its\n"
                                  "lower-case name: \"nearest\", \"bilinear\" (the default) or \"bicubic\" (Catmull-Rom).")
        .value("NEAREST", stb::interpolation_method::nearest)
        .value("BILINEAR", stb::interpolation_method::bilinear)
        .value("BICUBIC", stb::interpolation_method::bicubic);

    nb::class_<stb::homography>(
        m, "Homography",
        "A projective transform of the plane, as a 3 x 3 matrix by rows: x' = (m0 x + m1 y + m2) / (m6 x + m7 y + m8) and\n"
        "y' = (m3 x + m4 y + m5) / (m6 x + m7 y + m8). The default is the identity; Homography(matrix) turns a Matrix\n"
        "into one, Homography(values) takes 9 numbers, and Homography.from_quad() fits one to four pairs of points.\n"
        "This is the C++ class stb::homography.")
        .def(nb::init<>())
        .def(
            "__init__", [](stb::homography* self, nb::handle value) { new (self) stb::homography(homography_of(value)); }, "value"_a,
            "From a Matrix, or from 9 numbers by rows (6 numbers a, b, c, d, e, f are a Matrix).")
        .def_static(
            "from_quad",
            [](const std::vector<std::tuple<float, float>>& src, const std::vector<std::tuple<float, float>>& dst) {
                if (src.size() != 4 || dst.size() != 4) throw std::invalid_argument("from_quad takes four points and four points");
                stb::point s[4], d[4];
                for (std::size_t i = 0; i < 4; ++i) {
                    s[i] = stb::point{std::get<0>(src[i]), std::get<1>(src[i])};
                    d[i] = stb::point{std::get<0>(dst[i]), std::get<1>(dst[i])};
                }
                const std::optional<stb::homography> h = stb::homography::from_quad(s, d);
                if (!h) throw std::invalid_argument("the points are degenerate: no transform takes them to those");
                return *h;
            },
            "src"_a, "dst"_a, "The transform that takes the four points `src` to the four points `dst` (lists of (x, y)). ValueError if they are degenerate.")
        .def("then", &stb::homography::then, "next"_a, "A new transform that applies this one first and `next` second.")
        .def(
            "map", [](const stb::homography& h, float x, float y) { const stb::point p = h.map(x, y); return std::make_tuple(p.x, p.y); }, "x"_a, "y"_a,
            "The point (x, y) after the transform, as a tuple ((nan, nan) where it is sent to infinity).")
        .def("inverse", &stb::homography::inverse, "The inverse transform, or None if there is none.")
        .def("__iter__", [](const stb::homography& h) { return nb::iter(nb::cast(std::vector<float>(h.m, h.m + 9))); }, "The nine numbers by rows.")
        .def("__eq__", [](const stb::homography& a, const stb::homography& b) { return std::equal(a.m, a.m + 9, b.m); }, nb::is_operator())
        .def("__repr__", [](const stb::homography& h) {
            char buf[256];
            std::snprintf(buf, sizeof buf, "Homography(%g, %g, %g; %g, %g, %g; %g, %g, %g)", double(h.m[0]), double(h.m[1]), double(h.m[2]),
                          double(h.m[3]), double(h.m[4]), double(h.m[5]), double(h.m[6]), double(h.m[7]), double(h.m[8]));
            return std::string(buf);
        });

    const std::string warp_doc =
        std::string("A picture seen through a transform that takes a point of the source to a point of the result, in the canvas / SVG\n"
                    "convention of Matrix: the centre of pixel (x, y) is the point (x + 0.5, y + 0.5), so the identity returns the picture\n"
                    "and a whole-number translation moves it by exactly that. `out_width` and `out_height` are the size of the result (0, the\n"
                    "default, is that of the picture, for each of them on its own). `edge` is what is read where the transform reaches outside the picture; the default is \"zero\"\n"
                    "(transparent black on RGBA), not \"clamp\". `interpolation` is \"nearest\", \"bilinear\" (the default) or \"bicubic\".\n"
                    "Every channel is sampled on its own (alpha too). ValueError for a transform that cannot be inverted or a bad size.\n"
                    "Takes an Image (a new Image) or a float32 array of shape (H, W) or (H, W, C) (a new array); 1 to 4 channels.\n");
    const std::string affine_doc = warp_doc + "`transform` is a Matrix or the six numbers a, b, c, d, e, f.\n";
    const std::string persp_doc = warp_doc + "`transform` is a Homography, a Matrix, or 9 numbers by rows.\n";
    def_dispatch(m, image_cls, "warp_affine", stb_kinds,
                 [](const auto& x, nb::handle transform, int out_width, int out_height, nb::handle interpolation, stb::edge_mode edge) {
                     const stb::homography h = with_gil([&] { return stb::homography(matrix_of(transform)); });
                     const stb::interpolation_method ip = with_gil([&] { return interpolation_of(interpolation); });
                     return stb::warp_perspective(x, h, out_width, out_height, ip, edge);
                 },
                 "src"_a, "transform"_a, "out_width"_a = 0, "out_height"_a = 0, nb::kw_only(), "interpolation"_a = "bilinear", "edge"_a = "zero", (std::string(affine_doc) + kinds_doc).c_str());
    def_dispatch(m, image_cls, "warp_perspective", stb_kinds,
                 [](const auto& x, nb::handle transform, int out_width, int out_height, nb::handle interpolation, stb::edge_mode edge) {
                     const stb::homography h = with_gil([&] { return homography_of(transform); });
                     const stb::interpolation_method ip = with_gil([&] { return interpolation_of(interpolation); });
                     return stb::warp_perspective(x, h, out_width, out_height, ip, edge);
                 },
                 "src"_a, "transform"_a, "out_width"_a = 0, "out_height"_a = 0, nb::kw_only(), "interpolation"_a = "bilinear", "edge"_a = "zero", (std::string(persp_doc) + kinds_doc).c_str());

    // ---- unsharp_mask
    const std::string unsharp_doc =
        std::string("out = src + amount * (src - gaussian_blur(src, sigma)), clamped to 0..1; a value whose difference from the blur is\n"
                    "below `threshold` (a fraction of the full range) is left alone. amount 1 is \"100 %\" of Pillow's UnsharpMask.\n"
                    "The alpha channel is never changed.\n") + alpha_doc + "\n" + edge_doc;
    def_dispatch(m, image_cls, "unsharp_mask", stb_kinds,
                 [](const auto& x, float sigma, float amount, float threshold, stb::edge_mode edge) {
                     return stb::unsharp_mask(x, sigma, amount, threshold, edge);
                 },
                 "src"_a, "sigma"_a, "amount"_a = 1.0f, "threshold"_a = 0.0f, nb::kw_only(), "edge"_a = "clamp", unsharp_doc.c_str());

    // ---- pixelize
    const std::string pixelize_doc =
        std::string("Cuts the picture into blocks of block_w x block_h values (block_h 0: the same as block_w), from the top left, and\n"
                    "fills each block with its mean; the blocks at the right and bottom edges are smaller. The colour of an image with\n"
                    "alpha is the mean weighted by alpha, and the alpha of a block is the mean of its alpha.\n") + alpha_doc;
    def_dispatch(m, image_cls, "pixelize", stb_kinds,
                 [](const auto& x, int block_w, int block_h) { return stb::pixelize(x, block_w, block_h); },
                 "src"_a, "block_w"_a, "block_h"_a = 0, pixelize_doc.c_str());

    // ---- convert_color
    const std::string color_doc =
        std::string("Converts the colour channels from the space `from_space` to `to_space` (ColorSpace, or \"rgb\", \"hsv\", \"hsl\",\n"
                    "\"ycbcr\"). 3 or 4 channels (an alpha channel is never changed); the array keeps its type and size and its\n"
                    "channels become (H, S, V), (H, S, L), (Y, Cb, Cr) or (R, G, B). In 8 bits every channel is 0 to 255 and the hue has\n"
                    "256 steps for the full turn; in float32 every channel is 0 to 1 and the hue is a fraction of the turn. YCbCr is the\n"
                    "full-range BT.601 of JPEG.\n");
    def_dispatch(m, image_cls, "convert_color", stb_kinds,
                 [](const auto& x, stb::color_space from, stb::color_space to) { return stb::convert_color(x, from, to); },
                 "src"_a, "from_space"_a, "to_space"_a, color_doc.c_str());

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
    def_dispatch(m, image_cls, "ssim_map", stb_kinds,
                 [](const auto& a, const auto& b) { return stb::ssim_map(a, b); },
                 "a"_a, "b"_a, (std::string("The local SSIM values as a float32 array of shape (H - 10, W - 10, C) ((H - 10, W - 10) for a (H, W) array): one per window that lies inside the picture.\n") + "Two Images, two uint8 arrays or two float32 arrays of the same shape.\n").c_str());
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

// A mask (the result of threshold, canny, ...) is a gray Image for an Image and a uint8 array for an array.
template <class M>
auto mask_of(stb::mat8u&& mask) {
    if constexpr (std::is_same_v<M, stb::image>) return mask_image(mask);
    else return std::move(mask);
}

// Python objects (a name, a matrix) are read with the GIL: the function itself runs without it.
template <class F>
auto with_gil(F&& f) {
    nb::gil_scoped_acquire gil;
    return f();
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

// The sixteen functions that change their argument: one name for an Image, a uint8 array and a float32 array, and a method of Image.
// Each lambda is written once (polybind::def_dispatch makes the overloads); it gives its argument back, so calls chain.
void bind_inplace(nb::module_& m, nb::class_<stb::image>& image_cls) {
    const auto doc = [](const char* what, const char* same) {
        return std::string(what) + " in place and returns it, so calls chain. It takes an Image, a uint8 array or a float32 array (an array\n"
               "must be writable and C-contiguous; any other type is a TypeError). The parameters are those of " + same + ".";
    };
    const std::string morph = doc("Applies the operation to its argument", "the function without _inplace");

    // ---- tone
    const std::string d_eq = doc("Equalizes the histogram of its argument", "equalize()");
    def_dispatch(m, image_cls, "equalize_inplace", stb_kinds,
                 [](auto& x, stb::tone_mode mode) -> auto& { return stb::equalize_inplace(x, mode); }, "img"_a,
                 "mode"_a = "luma", d_eq.c_str());
    const std::string d_ac = doc("Stretches the levels of its argument", "auto_contrast()");
    def_dispatch(m, image_cls, "auto_contrast_inplace", stb_kinds,
                 [](auto& x, float cutoff, stb::tone_mode mode) -> auto& { return stb::auto_contrast_inplace(x, cutoff, mode); },
                 "img"_a, "cutoff"_a = 0.0f, "mode"_a = "luma", d_ac.c_str());
    const std::string d_mh = doc("Matches the histogram of its argument to `reference` (the same kind of data)", "match_histogram()");
    def_dispatch(m, image_cls, "match_histogram_inplace", stb_kinds,
                 [](auto& x, const auto& reference, stb::tone_mode mode) -> auto& {
                     return stb::match_histogram_inplace(x, reference, mode);
                 },
                 "img"_a, "reference"_a, "mode"_a = "luma", d_mh.c_str());
    const std::string d_cl = doc("Equalizes the histogram of its argument by tiles", "clahe()");
    def_dispatch(m, image_cls, "clahe_inplace", stb_kinds,
                 [](auto& x, float clip_limit, int tiles_x, int tiles_y, stb::tone_mode mode) -> auto& {
                     return stb::clahe_inplace(x, clip_limit, tiles_x, tiles_y, mode);
                 },
                 "img"_a, "clip_limit"_a = 2.0f, "tiles_x"_a = 8, "tiles_y"_a = 0, "mode"_a = "luma", d_cl.c_str());
    const std::string d_ga = doc("Applies the gamma curve to its argument", "gamma()");
    def_dispatch(m, image_cls, "gamma_inplace", stb_kinds,
                 [](auto& x, float g) -> auto& { return stb::gamma_inplace(x, g); }, "img"_a, "g"_a, d_ga.c_str());
    const std::string d_lv = doc("Applies the levels curve to its argument", "levels()");
    def_dispatch(m, image_cls, "levels_inplace", stb_kinds,
                 [](auto& x, float in_black, float in_white, float g, float out_black, float out_white) -> auto& {
                     return stb::levels_inplace(x, in_black, in_white, g, out_black, out_white);
                 },
                 "img"_a, "in_black"_a, "in_white"_a, "g"_a = 1.0f, "out_black"_a = 0.0f, "out_white"_a = 1.0f, d_lv.c_str());

    // ---- colour, sharpening, pixels
    const std::string d_cc = doc("Converts its argument from one colour space to another", "convert_color()");
    def_dispatch(m, image_cls, "convert_color_inplace", stb_kinds,
                 [](auto& x, stb::color_space from, stb::color_space to) -> auto& { return stb::convert_color_inplace(x, from, to); },
                 "img"_a, "from_space"_a, "to_space"_a, d_cc.c_str());
    const std::string d_us = doc("Sharpens its argument", "unsharp_mask()");
    def_dispatch(m, image_cls, "unsharp_mask_inplace", stb_kinds,
                 [](auto& x, float sigma, float amount, float threshold, stb::edge_mode edge) -> auto& {
                     return stb::unsharp_mask_inplace(x, sigma, amount, threshold, edge);
                 },
                 "img"_a, "sigma"_a, "amount"_a = 1.0f, "threshold"_a = 0.0f, nb::kw_only(), "edge"_a = "clamp", d_us.c_str());
    const std::string d_px = doc("Pixelizes its argument", "pixelize()");
    def_dispatch(m, image_cls, "pixelize_inplace", stb_kinds,
                 [](auto& x, int block_w, int block_h) -> auto& { return stb::pixelize_inplace(x, block_w, block_h); }, "img"_a,
                 "block_w"_a, "block_h"_a = 0, d_px.c_str());

    // ---- morphology: the structuring element `se` is a gray Image
#define STB_DEF_MORPH_INPLACE(NAME)                                                                                  \
    def_dispatch(m, image_cls, #NAME "_inplace", stb_kinds,                                                          \
                 [](auto& x, const stb::image& se, stb::edge_mode edge) -> auto& { return stb::NAME##_inplace(x, se, edge); }, \
                 "img"_a, "se"_a, nb::kw_only(), "edge"_a = "clamp", morph.c_str())
    STB_DEF_MORPH_INPLACE(erode);
    STB_DEF_MORPH_INPLACE(dilate);
    STB_DEF_MORPH_INPLACE(morph_open);
    STB_DEF_MORPH_INPLACE(morph_close);
    STB_DEF_MORPH_INPLACE(morph_gradient);
    STB_DEF_MORPH_INPLACE(top_hat);
    STB_DEF_MORPH_INPLACE(black_hat);
#undef STB_DEF_MORPH_INPLACE
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
    def_dispatch(m, image_cls, "scharr", stb_kinds,
                 [](const auto& x, stb::edge_mode edge) { return stb::scharr(x, edge); },
                 "src"_a, nb::kw_only(), "edge"_a = "clamp", "Like sobel() with {3, 10, 3} as the smoothing across: a tuple (dx, dy) of float32 arrays. A step of height 1 gives 16.");

    // ---- thresholds: the masks are gray Images of 0 and 255
    def_dispatch(m, image_cls, "threshold", stb_kinds,
                 [](const auto& x, double t, bool invert) {
                     using M = kind_of<decltype(x)>;
                     using V = stb::traits::mat_value_t<M>;  // uint8_t for an Image and a uint8 array, float for a float array
                     if constexpr (std::is_floating_point_v<V>) {
                         return mask_of<M>(stb::threshold(x, V(t), invert));
                     } else {
                         const auto top = std::numeric_limits<V>::max();
                         if (t < 0 || t > double(top) || t != std::floor(t))
                             throw std::invalid_argument("t must be a whole level from 0 to " + std::to_string(static_cast<unsigned long long>(top)));
                         return mask_of<M>(stb::threshold(x, V(t), invert));
                     }
                 },
                 "src"_a, "t"_a, "invert"_a = false, "The mask of the values above `t`: 255 where src > t, else 0; invert swaps the two. src is an Image or a uint8 array of one channel\n"
                 "(t is a whole level 0 to 255), or a float32 array (t is a value of the 0 to 1 range). The mask is a gray Image for an Image\n"
                 "and a uint8 array for an array. out = 255 * H(src - t) with H the step function.");
    def_dispatch(m, image_cls, "otsu_threshold", stb_kinds,
                 [](const auto& x) {
                     using V = stb::traits::mat_value_t<kind_of<decltype(x)>>;
                     if constexpr (std::is_floating_point_v<V>) return double(stb::otsu_threshold(x));
                     else return static_cast<long long>(stb::otsu_threshold(x));
                 },
                 "src"_a, "Otsu's level (an int for an Image or a uint8 array, a float for a float32 array): the one that best splits the histogram in two, as\n"
                 "scikit-image's threshold_otsu. Use it as the `t` of threshold().");
    const std::string adaptive_doc =
        std::string("The mask (a gray Image) of the values above the local level of their neighbourhood: 255 where\n"
                    "src > local - offset, `local` being the mean or the Gaussian-weighted mean of the (2 * radius + 1)-square around the value.\n"
                    "`offset` is in levels for an Image and in values for a float32 array. Takes one channel.\n") + edge_doc;
    def_dispatch(m, image_cls, "adaptive_threshold", stb_kinds,
                 [](const auto& x, int radius, float offset, nb::handle method, bool invert, stb::edge_mode edge) {
                     const stb::adaptive_method a = with_gil([&] { return adaptive_of(method); });
                     return mask_of<kind_of<decltype(x)>>(stb::adaptive_threshold(x, radius, offset, a, invert, edge));
                 },
                 "src"_a, "radius"_a, "offset"_a = 0.0f, "method"_a = "gaussian", "invert"_a = false, nb::kw_only(), "edge"_a = "clamp", (std::string(adaptive_doc) + "\n" + kinds_doc + "The mask is a gray Image for an Image and a uint8 array for an array.\n").c_str());

    // ---- morphology
    m.def(
        "structuring_element",
        [](nb::handle shape, int width, int height) { return mask_image(stb::structuring_element(shape_of(shape), width, height)); },
        "shape"_a, "width"_a, "height"_a = 0,
        "A structuring element as a gray Image (members 255): \"rect\", \"ellipse\" or \"cross\" (or a MorphShape), width x height\n"
        "values, both odd (height 0: the same as the width). Any odd-sized gray Image with non-zero members works as an element too.");
    const std::string morph_doc =
        std::string("Takes an Image, a uint8 array or a float32 array (any number of channels; an array is not converted: any other type\n"
                    "is a TypeError) and the structuring element `se` (a gray Image), and gives a new one of the same kind. Each channel\n"
                    "is done on its own. ") +
        edge_doc;
#define STB_DEF_MORPH(NAME, WHAT)                                                                                           \
    def_dispatch(m, image_cls, #NAME, stb_kinds,                                                                            \
                 [](const auto& x, const stb::image& se, stb::edge_mode edge) {                                             \
                     return stb::NAME(x, static_cast<const stb::mat8u&>(se), edge);                                         \
                 },                                                                                                         \
                 "src"_a, "se"_a, nb::kw_only(), "edge"_a = "clamp", (std::string(WHAT "\n") + morph_doc).c_str())
    STB_DEF_MORPH(erode, "The smallest value under the element for every position.");
    STB_DEF_MORPH(dilate, "The largest value under the element for every position.");
    STB_DEF_MORPH(morph_open, "Erode, then dilate: removes small bright parts.");
    STB_DEF_MORPH(morph_close, "Dilate, then erode: fills small dark holes.");
    STB_DEF_MORPH(morph_gradient, "dilate - erode: the outline of the shapes.");
    STB_DEF_MORPH(top_hat, "src - morph_open(src): the small bright parts (never below 0).");
    STB_DEF_MORPH(black_hat, "morph_close(src) - src: the small dark parts (never below 0).");
#undef STB_DEF_MORPH

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
    def_dispatch(m, image_cls, "canny", stb_kinds,
                 [](const auto& x, float low, float high, float sigma) {
                     return mask_of<kind_of<decltype(x)>>(stb::canny(x, low, high, sigma));
                 },
                 "src"_a, "low"_a = 0.1f, "high"_a = 0.2f, "sigma"_a = 1.0f, (std::string(canny_doc) + "\nThe mask is a gray Image for an Image and a uint8 array for an array.\n").c_str());
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
                                      "name: \"slic\", \"slico\" (the default), \"snic\" or \"sicle\".")
        .value("SLIC", stb::superpixel_method::slic)
        .value("SLICO", stb::superpixel_method::slico)
        .value("SNIC", stb::superpixel_method::snic)
        .value("SICLE", stb::superpixel_method::sicle);

    auto image_cls = nb::borrow<nb::class_<stb::image>>(m.attr("Image"));

    // ---- conversions
    def_dispatch(m, image_cls, "cast", cast_kinds,
                 [](const auto& x, nb::handle to) -> cast_value {
                     const pixel_type t = with_gil([&] { return pixel_type_of(to); });
                     return cast_as(x, t);
                 },
                 "src"_a, "to"_a,
                 "The same picture in another pixel type, keeping the range: white stays white. `to` is \"uint8\", \"uint16\", \"uint32\",\n"
                 "\"int16\", \"int32\", \"float32\" or \"float64\" (or a numpy dtype or type). A value is read as value / max of its type (a float is\n"
                 "0 to 1, a signed integer -1 to 1) and written as round(clamp(value * max of the new type, lowest, max)); NaN becomes 0. So\n"
                 "uint8 to float32 divides by 255 (0 is 0.0 and 255 is 1.0), float32 to uint8 is round(clamp(v, 0, 1) * 255), uint8 to uint16\n"
                 "multiplies by 257. The same type gives a copy. An Image to uint8 is an Image; every other result is a new array of the shape\n"
                 "of the argument ((H, W, C) for an Image). The argument is an Image or an array of one of those types.");

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
    def_dispatch(m, image_cls, "gaussian_blur", stb_kinds,
                 [](const auto& x, float sigma, stb::edge_mode edge) {
                     return of_kind<kind_of<decltype(x)>>(stb::gaussian_blur(x, sigma, edge));
                 },
                 "src"_a, "sigma"_a, nb::kw_only(), "edge"_a = "clamp", (std::string("Gaussian blur with a standard deviation `sigma`. Each channel on its own; uses all the cores on a big picture, with the same result on any number of threads.\n") + kinds_doc + edge_doc).c_str());

    // ---- box_blur
    def_dispatch(m, image_cls, "box_blur", stb_kinds,
                 [](const auto& x, int radius, stb::edge_mode edge) {
                     return of_kind<kind_of<decltype(x)>>(stb::box_blur(x, radius, edge));
                 },
                 "src"_a, "radius"_a, nb::kw_only(), "edge"_a = "clamp", (std::string("The mean of the (2 * radius + 1)-square around each pixel. The cost does not depend on the radius.\n") + kinds_doc).c_str());

    // ---- convolve: a full 2-D kernel, or a row and a column
    const std::string conv_doc =
        std::string("Convolve with a kernel (a 2-D array or a list of lists of numbers, odd width and height), laid over each\n"
                    "pixel as it is (a correlation, not flipped, like OpenCV's filter2D). With two arguments after `src`, a row\n"
                    "kernel and a column kernel (each a list of an odd number of taps): the same as their product but much\n"
                    "cheaper. Takes an Image or a float32 array; returns the same kind.\n") +
        edge_doc;
    def_dispatch(m, image_cls, "convolve", stb_kinds,
                 [](const auto& x, nb::handle kernel, stb::edge_mode edge) {
                     const stb::mat32f k = kernel_with_gil(kernel);
                     return of_kind<kind_of<decltype(x)>>(stb::convolve(x, k, edge));
                 },
                 "src"_a, "kernel"_a, nb::kw_only(), "edge"_a = "clamp", (std::string(conv_doc) + kinds_doc).c_str());
    def_dispatch(m, image_cls, "convolve", stb_kinds,
                 [](const auto& x, nb::handle row, nb::handle col, stb::edge_mode edge) {
                     const stb::mat32f r = kernel_with_gil(row), c = kernel_with_gil(col);
                     return of_kind<kind_of<decltype(x)>>(stb::convolve(x, r, c, edge));
                 },
                 "src"_a, "row_kernel"_a, "col_kernel"_a, nb::kw_only(), "edge"_a = "clamp", (std::string(conv_doc) + kinds_doc).c_str());

    // ---- sobel and magnitude
    def_dispatch(m, image_cls, "sobel", stb_kinds,
                 [](const auto& x, stb::edge_mode edge) { return stb::sobel(x, edge); },
                 "src"_a, nb::kw_only(), "edge"_a = "clamp", "The Sobel derivatives of an Image or a uint8 array (read as value / 255) or a float32 array: a tuple (dx, dy) of float32 arrays\n"
                 "of the same shape ((H, W, C) for an Image), positive where the values grow to the right (dx) or downwards (dy). A step of\n"
                 "height 1 gives 4.");
    def_dispatch(m, image_cls, "magnitude", float_kinds,
                 [](const auto& dx, const auto& dy) {
                     stb::gradients g;
                     g.dx = dx.copy();
                     g.dy = dy.copy();
                     return stb::magnitude(g);
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
        "\"snic\" (one pass, no iterations) or \"sicle\" (starts from many more seeds than regions and drops the least\n"
        "relevant ones run after run; follows edges closely and gives exactly `count` regions; `compactness` is not used).\n"
        "`iterations` is the number of refinements of slic and slico (default 10) and the most runs of sicle (default 5);\n"
        "`enforce_connectivity` merges the small detached pieces slic and slico can leave; `oversample` (sicle: the number\n"
        "of seeds it starts from, 0 is automatic) and `removal_rate` (sicle: the share of the seeds dropped per run, in\n"
        "[0, 1), 0 follows `iterations`; 0.63 is DISF) are for sicle. The same Image and arguments give the same\n"
        "SuperpixelMap on every number of threads. Returns a SuperpixelMap.";
    const auto make = [](const stb::image& img, int count, float compactness, std::optional<int> iterations, nb::handle method,
                         bool enforce, int oversample, float removal_rate) {
        const stb::superpixel_method me = method_of(method);
        nb::gil_scoped_release release;
        stb::superpixel sp = std::move(stb::superpixel(img, count).method(me).compactness(compactness).enforce_connectivity(enforce)
                                           .oversample(oversample).removal_rate(removal_rate));
        if (iterations) sp.iterations(*iterations);  // not given: the method's own default
        return sp.run();
    };
    m.def("superpixels", make, "src"_a, "count"_a = 400, "compactness"_a = 10.0f, "iterations"_a = nb::none(), "method"_a = "slico",
          "enforce_connectivity"_a = true, "oversample"_a = 0, "removal_rate"_a = 0.0f, sp_doc);
    image_cls.def("superpixels", make, "count"_a = 400, "compactness"_a = 10.0f, "iterations"_a = nb::none(), "method"_a = "slico",
                  "enforce_connectivity"_a = true, "oversample"_a = 0, "removal_rate"_a = 0.0f, sp_doc);

    bind_tone(m, image_cls);
    bind_image_ops(m, image_cls);
    bind_segmentation(m, image_cls);
    bind_inplace(m, image_cls);
}
