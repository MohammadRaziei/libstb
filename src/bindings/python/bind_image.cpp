#include <nanobind/ndarray.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>

#include <climits>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "binding.hpp"

// std::vector<uint8_t> -> bytes (nanobind's generic vector caster would make
// a list of ints). Output only: this is what image::to_* return.
namespace nanobind::detail {
template <>
struct type_caster<std::vector<std::uint8_t>> {
    NB_TYPE_CASTER(std::vector<std::uint8_t>, const_name("bytes"))

    bool from_python(handle, std::uint8_t, cleanup_list*) noexcept { return false; }

    static handle from_cpp(const std::vector<std::uint8_t>& v, rv_policy, cleanup_list*) noexcept {
        return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(v.data()),
                                         static_cast<Py_ssize_t>(v.size()));
    }
};
}  // namespace nanobind::detail

using namespace nb::literals;

namespace {

using array_rw = nb::ndarray<nb::device::cpu>;          // only writable arrays match
using array_ro = nb::ndarray<nb::ro, nb::device::cpu>;  // read-only ones too
using bytes_in = nb::ndarray<const std::uint8_t, nb::ndim<1>, nb::c_contig, nb::device::cpu>;

// Keeps a Python object alive for as long as an image views its memory. The
// last reference may be dropped from any thread, hence the GIL.
std::shared_ptr<void> keep_alive(nb::handle obj) {
    auto* ref = new nb::object(nb::borrow(obj));
    return std::shared_ptr<void>(ref, [](nb::object* p) {
        if (Py_IsInitialized()) {  // otherwise the interpreter is gone: leak, do not touch it
            nb::gil_scoped_acquire gil;
            delete p;
        }
    });
}

// Image(array): a uint8 array of shape (H, W) or (H, W, 1..4).
//  - writable and C-contiguous: the image views the array's memory (no copy);
//  - anything else (read-only, strided, e.g. arr[::-1]): copied.
stb::image image_from_array(nb::handle obj) {
    array_ro a;
    if (!nb::try_cast(obj, a, /*convert=*/false))
        throw nb::type_error("Image needs a uint8 array");
    if (a.dtype() != nb::dtype<std::uint8_t>())
        throw nb::type_error("Image needs a uint8 array");
    if (a.ndim() != 2 && a.ndim() != 3)
        throw std::invalid_argument("expected shape (H, W) or (H, W, 1..4)");
    const std::size_t h = a.shape(0), w = a.shape(1), c = a.ndim() == 3 ? a.shape(2) : 1;
    if (c < 1 || c > 4) throw std::invalid_argument("expected shape (H, W) or (H, W, 1..4)");
    if (h > INT_MAX || w > INT_MAX) throw std::invalid_argument("image dimensions too large");

    const std::int64_t s0 = a.stride(0), s1 = a.stride(1), s2 = a.ndim() == 3 ? a.stride(2) : 1;
    const bool contiguous = s2 == 1 && s1 == std::int64_t(c) && s0 == std::int64_t(w * c);

    array_rw rw;
    if (contiguous && nb::try_cast(obj, rw, /*convert=*/false))
        return stb::image::wrap(int(w), int(h), int(c), static_cast<std::uint8_t*>(rw.data()),
                                keep_alive(obj));

    const auto* src = static_cast<const std::uint8_t*>(a.data());
    std::vector<std::uint8_t> px(h * w * c);
    if (contiguous) {
        if (!px.empty()) std::memcpy(px.data(), src, px.size());  // read-only but contiguous
    } else {
        for (std::size_t y = 0; y < h; ++y)  // strided, e.g. arr[::-1] or a transposed view
            for (std::size_t x = 0; x < w; ++x)
                for (std::size_t k = 0; k < c; ++k)
                    px[(y * w + x) * c + k] = src[std::int64_t(y) * s0 + std::int64_t(x) * s1 + std::int64_t(k) * s2];
    }
    return stb::image(int(w), int(h), int(c), std::move(px));
}

stb::load_options make_options(int channels, bool flip, std::size_t max_bytes) {
    stb::load_options o;
    o.channels = channels;
    o.flip = flip;
    o.max_bytes = max_bytes;
    return o;
}

// Buffer protocol: memoryview(img) is a writable 3-D (height, width, channels)
// uint8 view of the pixels, no copy, no numpy. The view holds a reference to the
// image (view->obj), so the pixels cannot be freed under it. numpy also reads
// this, so np.asarray(img) stays zero-copy.
struct buffer_meta {
    Py_ssize_t shape[3];
    Py_ssize_t strides[3];
};

int image_getbuffer(PyObject* exporter, Py_buffer* view, int flags) {
    if (!view) {
        PyErr_SetString(PyExc_BufferError, "no buffer requested");
        return -1;
    }
    auto* img = nb::inst_ptr<stb::image>(nb::handle(exporter));
    const auto h = Py_ssize_t(img->height()), w = Py_ssize_t(img->width()), c = Py_ssize_t(img->channels());

    auto* meta = new buffer_meta{{h, w, c}, {w * c, c, 1}};
    Py_INCREF(exporter);
    view->obj = exporter;
    view->buf = img->data();
    view->len = h * w * c;
    view->readonly = 0;
    view->itemsize = 1;
    view->format = (flags & PyBUF_FORMAT) ? const_cast<char*>("B") : nullptr;
    view->suboffsets = nullptr;
    view->internal = meta;
    if (flags & PyBUF_ND) {  // asked for a shape: give the real 3-D one
        view->ndim = 3;
        view->shape = meta->shape;
        view->strides = (flags & PyBUF_STRIDES) == PyBUF_STRIDES ? meta->strides : nullptr;
    } else {  // a plain byte buffer, as the protocol requires when no shape is asked for
        view->ndim = 1;
        view->shape = nullptr;
        view->strides = nullptr;
    }
    return 0;
}

void image_releasebuffer(PyObject*, Py_buffer* view) { delete static_cast<buffer_meta*>(view->internal); }

const PyType_Slot image_slots[] = {
    {Py_bf_getbuffer, reinterpret_cast<void*>(image_getbuffer)},
    {Py_bf_releasebuffer, reinterpret_cast<void*>(image_releasebuffer)},
    {0, nullptr},
};

// numpy is an optional extra (`pip install "libstb[numpy]"`), imported only when
// a numpy object is actually asked for, never at `import libstb`. Everything
// else (open, resize, to_*, write*, fonts, Image(array) from any buffer) works
// without it. Checked once per process: after the first success this is a
// single bool test, so `.array` stays as cheap as before.
void require_numpy() {
    static bool available = false;
    if (available) return;
    try {
        nb::module_::import_("numpy");
        available = true;
    } catch (nb::python_error&) {
        throw nb::import_error(
            "this needs numpy, which libstb does not install by default: "
            "pip install \"libstb[numpy]\". Image.tobytes() and memoryview(img) "
            "give you the pixels without it.");
    }
}

// The pixels as an ndarray sharing the image's memory; `self` owns it.
nb::ndarray<nb::numpy, std::uint8_t, nb::ndim<3>> pixels_of(nb::handle self) {
    require_numpy();
    auto& img = nb::cast<stb::image&>(self);
    const std::size_t shape[3] = {std::size_t(img.height()), std::size_t(img.width()),
                                  std::size_t(img.channels())};
    return {img.data(), 3, shape, self};
}

}  // namespace

void bind_image(nb::module_& m) {
    using stb::image;
    using stb::resize_filter;
    using stb::resizer;
    using release = nb::call_guard<nb::gil_scoped_release>;  // the C++ call runs without the GIL

    m.attr("DEFAULT_MAX_BYTES") = stb::load_options{}.max_bytes;

    // ponytail: plain tuple; the Python package wraps it in ImageInfo.
    m.def(
        "info_bytes",
        [](nb::bytes data) {
            const char* p = data.c_str();
            const std::size_t n = data.size();
            stb::image_info i;
            {
                nb::gil_scoped_release unlock;  // decode without holding the GIL
                i = stb::image_info::read(p, n);
            }
            return std::make_tuple(i.width, i.height, i.channels);
        },
        "data"_a);

    // The native Image *is* stb::image: every method below is the C++ member.
    nb::class_<image>(m, "Image", nb::type_slots(image_slots),
                      "An 8-bit image: height x width x channels (1..4) uint8 pixels.\n\n"
                      "Image(array) takes a uint8 array of shape (H, W) or (H, W, 1..4). A writable,\n"
                      "C-contiguous array is used in place, not copied (the image and the array then\n"
                      "share their pixels); any other array (read-only, strided) is copied. Call\n"
                      "img.copy() for an independent image.\n"
                      "Image.open(source) decodes a path or encoded bytes. memoryview(img) is a no-copy\n"
                      "3-D view of the pixels; `.array` is the same as a numpy ndarray (needs numpy).")
        .def(
            "__init__", [](image* self, nb::handle array) { new (self) image(image_from_array(array)); },
            "array"_a)

        // --- copying is explicit ---
        .def("copy", &image::copy, release(),
             "An independent image that owns its own pixels (a deep copy).")
        .def("__copy__", &image::copy, release())
        .def("__deepcopy__", [](const image& i, nb::handle) { return i.copy(); }, "memo"_a)

        // --- decoding: open(bytes-like | path, *, channels, flip, max_bytes) ---
        .def_static(
            "open",
            [](const bytes_in& data, int channels, bool flip, std::size_t max_bytes) {
                return image::decode(data.data(), data.size(), make_options(channels, flip, max_bytes));
            },
            "source"_a, nb::kw_only(), "channels"_a = 0, "flip"_a = false,
            "max_bytes"_a = stb::load_options{}.max_bytes, release(),
            "Decode an image from encoded bytes (bytes, bytearray, memoryview) or from a file path.\n\n"
            "channels  : 0 keeps the file's channel count; 1..4 converts.\n"
            "flip      : flip vertically while decoding.\n"
            "max_bytes : refuse images whose decoded size would exceed this (checked from the\n"
            "            header, before any pixel allocation).\n\n"
            "Raises ValueError (bad arguments), DecodeError, LimitError, OSError (unreadable file).")
        .def_static(
            "open",
            [](const std::filesystem::path& path, int channels, bool flip, std::size_t max_bytes) {
                return image::open(path, make_options(channels, flip, max_bytes));
            },
            "source"_a, nb::kw_only(), "channels"_a = 0, "flip"_a = false,
            "max_bytes"_a = stb::load_options{}.max_bytes, release())

        // --- resizing: resizer=None (default), a Resizer, a Resizer.Filter, or a filter name ---
        .def("resize", nb::overload_cast<int, int, const resizer*>(&image::resize, nb::const_),
             "width"_a, "height"_a, "resizer"_a.none() = nb::none(), release(),
             "A new Image of the given size (this one is not modified).\n\n"
             "resizer: None (default Resizer()), a Resizer, a Resizer.Filter, or a filter name such\n"
             "as \"nearest\", \"linear\", \"cubic\", \"bspline\", \"mitchell\", \"box\", \"auto\".\n"
             "A name or Filter is shorthand for Resizer(name); use a Resizer to also set\n"
             "edge/srgb/max_bytes.\n\n"
             "Raises ValueError (bad size or unknown filter name), LimitError (result too large).")
        .def("resize", nb::overload_cast<int, int, resize_filter>(&image::resize, nb::const_),
             "width"_a, "height"_a, "resizer"_a, release())
        .def("resize", nb::overload_cast<int, int, std::string_view>(&image::resize, nb::const_),
             "width"_a, "height"_a, "resizer"_a, release())

        // --- encoding: to_* = the bytes of a file, write_* = the same written to `path` ---
        .def("to_png", &image::to_png, "compression"_a = stb::default_png_compression, release(),
             "PNG bytes. compression 1..9: higher is smaller and slower.")
        .def("to_jpg", &image::to_jpg, "quality"_a = stb::default_jpg_quality, release(),
             "JPEG bytes. quality 1..100; alpha, if any, is dropped.")
        .def("to_bmp", &image::to_bmp, release(), "BMP bytes.")
        .def("to_tga", &image::to_tga, "rle"_a = true, release(),
             "TGA bytes, run-length encoded unless rle=False.")
        .def("write_png", &image::write_png, "path"_a, "compression"_a = stb::default_png_compression,
             release(), "Write a PNG file (only after encoding succeeded).")
        .def("write_jpg", &image::write_jpg, "path"_a, "quality"_a = stb::default_jpg_quality, release(),
             "Write a JPEG file (only after encoding succeeded).")
        .def("write_bmp", &image::write_bmp, "path"_a, release(), "Write a BMP file.")
        .def("write_tga", &image::write_tga, "path"_a, "rle"_a = true, release(), "Write a TGA file.")
        .def("write", &image::write, "path"_a, release(),
             "Write to `path` in the format its extension names (.png .jpg .jpeg .bmp .tga,\n"
             "case-insensitive; ValueError otherwise), with default settings. For other\n"
             "settings call write_* directly.")

        // --- accessors and numpy interop ---
        .def_prop_ro("width", &image::width)
        .def_prop_ro("height", &image::height)
        .def_prop_ro("channels", &image::channels)
        .def_prop_ro("shape",
                     [](const image& i) { return std::make_tuple(i.height(), i.width(), i.channels()); })
        .def_prop_ro("array", &pixels_of,
                     "The pixels as a uint8 numpy ndarray (height, width, channels), no copy.\n"
                     "Needs numpy (pip install \"libstb[numpy]\"); imported here, on first use.")
        .def(
            "tobytes",
            [](const image& i) {
                return nb::bytes(reinterpret_cast<const char*>(i.data()),
                                 std::size_t(i.height()) * std::size_t(i.width()) * std::size_t(i.channels()));
            },
            "The pixels as bytes, row-major (height, width, channels). A copy; needs no numpy.")
        .def("__array__",
             [](nb::handle self, nb::object dtype, nb::object copy) -> nb::object {
                 nb::object a = self.attr("array");
                 if (!dtype.is_none()) {
                     nb::module_ np = nb::module_::import_("numpy");
                     if (!np.attr("dtype")(dtype).equal(np.attr("dtype")("uint8")))
                         return a.attr("astype")(dtype);  // always a copy
                 }
                 if (!copy.is_none() && nb::cast<bool>(copy)) return a.attr("copy")();
                 return a;
             },
             "dtype"_a = nb::none(), "copy"_a = nb::none())
        .def("numpy",
             [](nb::handle self, nb::object dtype, bool copy) -> nb::object {
                 return self.attr("__array__")(dtype, copy);
             },
             nb::kw_only(), "dtype"_a = nb::none(), "copy"_a = false,
             "The pixels as an ndarray: a view (no copy) unless copy=True or the requested\n"
             "dtype differs from uint8.")
        .def("__repr__", [](const image& i) {
            return "Image(width=" + std::to_string(i.width()) + ", height=" + std::to_string(i.height()) +
                   ", channels=" + std::to_string(i.channels()) + ")";
        });
}
