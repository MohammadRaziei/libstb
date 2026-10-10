#include <nanobind/ndarray.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>

#include <algorithm>
#include <array>
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
// A new image from `src` read with the given strides (in bytes; h x w x c pixels).
stb::image copy_pixels(const std::uint8_t* src, std::size_t h, std::size_t w, std::size_t c, std::int64_t s0,
                       std::int64_t s1, std::int64_t s2) {
    std::vector<std::uint8_t> px(h * w * c);
    if (s2 == 1 && s1 == std::int64_t(c) && s0 == std::int64_t(w * c)) {
        if (!px.empty()) std::memcpy(px.data(), src, px.size());  // contiguous (maybe read-only)
    } else {
        for (std::size_t y = 0; y < h; ++y)  // strided, e.g. arr[::-1] or a transposed view
            for (std::size_t x = 0; x < w; ++x)
                for (std::size_t k = 0; k < c; ++k)
                    px[(y * w + x) * c + k] = src[std::int64_t(y) * s0 + std::int64_t(x) * s1 + std::int64_t(k) * s2];
    }
    return stb::image(int(w), int(h), int(c), std::move(px));
}

struct py_buffer {  // releases a Py_buffer however the scope is left
    Py_buffer view{};
    bool ok = false;
    ~py_buffer() {
        if (ok) PyBuffer_Release(&view);
    }
};

// An object that only speaks NumPy's __array_interface__ (version 3), e.g. a Pillow image:
// {"shape", "typestr", "data", "strides", "offset"}. `data` is (address, read-only flag), as
// NumPy arrays give it, or an object with the buffer protocol, as Pillow gives (bytes).
// A writable C-contiguous address is shared (the image keeps `obj` alive, like for NumPy
// arrays); everything else is copied. As with NumPy itself, an address cannot be checked:
// only hand over objects you trust. A buffer is bounds-checked against shape and strides.
stb::image image_from_array_interface(nb::handle obj) {
    nb::dict ai = nb::cast<nb::dict>(obj.attr("__array_interface__"));
    if (!ai.contains("typestr") || !ai.contains("shape"))
        throw std::invalid_argument("__array_interface__ needs 'shape' and 'typestr'");
    const std::string typestr = nb::cast<std::string>(ai["typestr"]);
    if (typestr != "|u1" && typestr != "<u1" && typestr != ">u1") throw nb::type_error("Image needs a uint8 array");

    nb::sequence shape = nb::cast<nb::sequence>(ai["shape"]);
    const std::size_t ndim = nb::len(shape);
    if (ndim != 2 && ndim != 3) throw std::invalid_argument("expected shape (H, W) or (H, W, 1..4)");
    const std::int64_t h = nb::cast<std::int64_t>(shape[0]), w = nb::cast<std::int64_t>(shape[1]);
    const std::int64_t c = ndim == 3 ? nb::cast<std::int64_t>(shape[2]) : 1;
    if (c < 1 || c > 4) throw std::invalid_argument("expected shape (H, W) or (H, W, 1..4)");
    if (h < 1 || w < 1) throw std::invalid_argument("image is empty");
    if (h > INT_MAX || w > INT_MAX) throw std::invalid_argument("image dimensions too large");

    std::int64_t s0 = w * c, s1 = c, s2 = 1;
    if (ai.contains("strides") && !ai["strides"].is_none()) {
        nb::sequence st = nb::cast<nb::sequence>(ai["strides"]);
        if (nb::len(st) != ndim) throw std::invalid_argument("__array_interface__: strides do not match shape");
        s0 = nb::cast<std::int64_t>(st[0]);
        s1 = nb::cast<std::int64_t>(st[1]);
        s2 = ndim == 3 ? nb::cast<std::int64_t>(st[2]) : 1;
    }
    const bool contiguous = s2 == 1 && s1 == c && s0 == w * c;
    const std::int64_t offset = ai.contains("offset") ? nb::cast<std::int64_t>(ai["offset"]) : 0;

    nb::object data = ai.contains("data") ? nb::object(ai["data"]) : nb::object(nb::none());
    if (data.is_none()) data = nb::borrow(obj);  // the spec: the object itself exposes the buffer

    if (nb::isinstance<nb::tuple>(data)) {  // (address, read-only)
        nb::tuple t = nb::cast<nb::tuple>(data);
        if (nb::len(t) != 2) throw std::invalid_argument("__array_interface__: bad 'data' tuple");
        const auto addr = nb::cast<std::uintptr_t>(t[0]);
        const bool readonly = nb::cast<bool>(t[1]);
        if (!addr) throw std::invalid_argument("__array_interface__: null data address");
        auto* base = reinterpret_cast<std::uint8_t*>(addr);
        if (contiguous && !readonly) return stb::image::wrap(int(w), int(h), int(c), base, keep_alive(obj));
        return copy_pixels(base, std::size_t(h), std::size_t(w), std::size_t(c), s0, s1, s2);
    }

    py_buffer buf;
    if (PyObject_GetBuffer(data.ptr(), &buf.view, PyBUF_SIMPLE) != 0) throw nb::python_error();
    buf.ok = true;
    // Every byte the strides can reach must lie inside the buffer.
    const std::int64_t lo = offset + std::min<std::int64_t>(0, (h - 1) * s0) + std::min<std::int64_t>(0, (w - 1) * s1) +
                            std::min<std::int64_t>(0, (c - 1) * s2);
    const std::int64_t hi = offset + std::max<std::int64_t>(0, (h - 1) * s0) + std::max<std::int64_t>(0, (w - 1) * s1) +
                            std::max<std::int64_t>(0, (c - 1) * s2);
    if (lo < 0 || hi >= std::int64_t(buf.view.len))
        throw std::invalid_argument("__array_interface__: the data is shorter than shape and strides need");
    return copy_pixels(static_cast<const std::uint8_t*>(buf.view.buf) + offset, std::size_t(h), std::size_t(w),
                       std::size_t(c), s0, s1, s2);
}

stb::image image_from_array(nb::handle obj) {
    array_ro a;
    if (!nb::try_cast(obj, a, /*convert=*/false)) {
        // Not a buffer / DLPack object: maybe one that only has NumPy's array interface (Pillow).
        if (nb::hasattr(obj, "__array_interface__")) return image_from_array_interface(obj);
        throw nb::type_error("Image needs a uint8 array");
    }
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
    return copy_pixels(static_cast<const std::uint8_t*>(a.data()), h, w, c, s0, s1, s2);
}

stb::load_options make_options(int channels, bool flip, std::size_t max_bytes, bool orient) {
    stb::load_options o;
    o.channels = channels;
    o.flip = flip;
    o.max_bytes = max_bytes;
    o.orient = orient;
    return o;
}

std::uint8_t byte_of(nb::handle h) {
    const long v = nb::cast<long>(h);
    if (v < 0 || v > 255) throw std::invalid_argument("colour values must be in 0..255");
    return static_cast<std::uint8_t>(v);
}

// A colour argument: an int (every value the same) or a sequence of exactly `n` ints.
template <std::size_t N>
std::array<std::uint8_t, N> colour_of(nb::handle obj, std::size_t n, const char* what) {
    std::array<std::uint8_t, N> out{};
    if (PyLong_Check(obj.ptr())) {
        out.fill(byte_of(obj));
        return out;
    }
    nb::sequence seq = nb::cast<nb::sequence>(obj);
    if (nb::len(seq) != n)
        throw std::invalid_argument(std::string(what) + " needs one value per channel (" + std::to_string(n) +
                                    ") or a single int");
    for (std::size_t i = 0; i < n; ++i) out[i] = byte_of(seq[i]);
    return out;
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
// single bool test, so `.numpy()` stays cheap.
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

    m.def("simd_backends", &stb::simd_backends,
          "The SIMD backends convert / composite / flatten can run on this CPU, best first, e.g.\n"
          "['avx2', 'sse2', 'scalar'] (x86-64) or ['neon', 'scalar'] (arm64). All give identical\n"
          "results; they differ in speed. 'scalar' (the plain reference) is always last.");
    m.def("simd_name", &stb::simd_name, "The SIMD backend in use: the first of simd_backends() unless changed.");
    m.def(
        "set_simd",
        [](std::string_view name) {
            if (!stb::set_simd(name)) {
                std::string have;
                for (const auto& b : stb::simd_backends()) have += (have.empty() ? "'" : ", '") + b + "'";
                throw std::invalid_argument("unknown or unavailable SIMD backend '" + std::string(name) +
                                            "' (available: " + have + ", or 'auto')");
            }
        },
        "name"_a,
        "Choose the SIMD backend by name (one of simd_backends()), or 'auto' for the default.\n"
        "ValueError for an unknown or unavailable name (for example 'avx2' on a CPU without it).\n"
        "The environment variable LIBSTB_SIMD=<name> chooses the initial one.");
    m.def(
        "exif_orientation_bytes",
        [](nb::bytes data) {
            const char* p = data.c_str();
            const std::size_t n = data.size();
            nb::gil_scoped_release unlock;
            return stb::exif_orientation(p, n);
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
                      "3-D view of the pixels; `.numpy()` is the same as a numpy ndarray (needs numpy).")
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
            [](const bytes_in& data, int channels, bool flip, std::size_t max_bytes, bool orient) {
                return image::decode(data.data(), data.size(),
                                     make_options(channels, flip, max_bytes, orient));
            },
            "source"_a, nb::kw_only(), "channels"_a = 0, "flip"_a = false,
            "max_bytes"_a = stb::load_options{}.max_bytes, "orient"_a = false, release(),
            "Decode an image from encoded bytes (bytes, bytearray, memoryview) or from a file path.\n\n"
            "channels  : 0 keeps the file's channel count; 1..4 converts.\n"
            "flip      : flip vertically while decoding.\n"
            "orient    : apply the EXIF orientation of a JPEG or PNG, so a phone photo comes out\n"
            "            upright (width and height may swap; a flip is applied after it).\n"
            "max_bytes : refuse images whose decoded size would exceed this (checked from the\n"
            "            header, before any pixel allocation).\n\n"
            "Raises ValueError (bad arguments), DecodeError, LimitError, OSError (unreadable file).")
        .def_static(
            "open",
            [](const std::filesystem::path& path, int channels, bool flip, std::size_t max_bytes,
               bool orient) {
                return image::open(path, make_options(channels, flip, max_bytes, orient));
            },
            "source"_a, nb::kw_only(), "channels"_a = 0, "flip"_a = false,
            "max_bytes"_a = stb::load_options{}.max_bytes, "orient"_a = false, release())

        // --- DLPack import: Image.from_dlpack(x), the mirror of np.from_dlpack(img) ---
        .def_static(
            "from_dlpack",
            [](nb::handle obj, bool copy) {
                if (!nb::hasattr(obj, "__dlpack__"))
                    throw nb::type_error("Image.from_dlpack needs an object with __dlpack__ "
                                         "(a torch/jax/cupy/numpy array, another Image, ...)");
                image img = image_from_array(obj);
                return copy ? img.copy() : std::move(img);
            },
            "obj"_a, nb::kw_only(), "copy"_a = false,
            "An Image from any object that implements DLPack: a torch, jax, cupy or numpy array...\n\n"
            "The array must be uint8 with shape (H, W) or (H, W, 1..4). Like Image(array), a\n"
            "writable C-contiguous CPU array is shared, not copied (the image keeps it alive);\n"
            "read-only or strided ones are copied. copy=True always gives an independent image.\n"
            "Raises TypeError (no __dlpack__, not uint8), ValueError (bad shape).")

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

        // --- geometry: each returns a new Image, this one is not modified ---
        .def("crop", &image::crop, "x"_a, "y"_a, "width"_a, "height"_a, release(),
             "The width x height rectangle whose top-left corner is (x, y).\n\n"
             "Raises ValueError if the rectangle is not inside the image.")
        .def("flip_horizontal", &image::flip_horizontal, release(), "Mirror left <-> right.")
        .def("flip_vertical", &image::flip_vertical, release(), "Mirror top <-> bottom.")
        .def("rotate90", &image::rotate90, "turns"_a = 1, release(),
             "Rotate by `turns` quarter turns clockwise (negative: counter-clockwise).\n"
             "Width and height swap for an odd number of turns.")
        .def("transpose", &image::transpose, release(),
             "Swap rows and columns (mirror across the main diagonal); width and height swap.")
        .def("orient", &image::orient, "orientation"_a, release(),
             "The upright version of an image stored with this EXIF orientation (1..8, see\n"
             "libstb.exif_orientation): the transform a viewer applies. 1 is a copy.")
        .def(
            "pad",
            [](const image& i, int left, int top, int right, int bottom, nb::handle fill) {
                const auto f = colour_of<4>(fill, std::size_t(i.channels()), "fill");
                nb::gil_scoped_release unlock;
                return i.pad(left, top, right, bottom, f);
            },
            "left"_a = 0, "top"_a = 0, "right"_a = 0, "bottom"_a = 0, nb::kw_only(), "fill"_a = 0,
            "A larger Image with this one inside a border of the given widths (all >= 0).\n\n"
            "fill: the border value, an int for every channel or one value per channel,\n"
            "e.g. (255, 255, 255) for white on RGB or (0, 0, 0, 0) for transparent on RGBA.")
        .def("thumbnail", nb::overload_cast<int, int, const resizer*>(&image::thumbnail, nb::const_),
             "max_width"_a, "max_height"_a, "resizer"_a.none() = nb::none(), release(),
             "Shrink to fit inside max_width x max_height, keeping the aspect ratio. An image\n"
             "that already fits is copied, never enlarged. `resizer` is as in resize().")
        .def("thumbnail", nb::overload_cast<int, int, resize_filter>(&image::thumbnail, nb::const_),
             "max_width"_a, "max_height"_a, "resizer"_a, release())
        .def("thumbnail", nb::overload_cast<int, int, std::string_view>(&image::thumbnail, nb::const_),
             "max_width"_a, "max_height"_a, "resizer"_a, release())

        // --- channels and alpha ---
        .def("convert", &image::convert, "channels"_a, release(),
             "The same pixels with 1 (gray), 2 (gray + alpha), 3 (RGB) or 4 (RGBA) channels.\n\n"
             "Gray is 0.299 R + 0.587 G + 0.114 B on the stored values (no gamma, as in Pillow),\n"
             "a missing alpha is 255, and a dropped alpha is discarded: use flatten() to blend it\n"
             "onto a background instead. The same channel count gives a copy.")
        .def(
            "split",
            [](const image& i) {
                std::vector<image> planes;
                {
                    nb::gil_scoped_release unlock;
                    planes = i.split();
                }
                nb::list out;
                for (auto& p : planes) out.append(nb::cast(std::move(p)));
                return nb::tuple(out);
            },
            "A tuple with one single-channel Image per channel.")
        .def_static(
            "merge",
            [](nb::iterable channels) {
                std::vector<nb::object> keep;  // the Images stay alive while we point at them
                std::vector<const image*> ptrs;
                for (nb::handle h : channels) {
                    image* p = nullptr;
                    if (!nb::try_cast<image*>(h, p, /*convert=*/false) || !p)
                        throw nb::type_error("Image.merge needs Image objects");
                    keep.push_back(nb::borrow(h));
                    ptrs.push_back(p);
                }
                nb::gil_scoped_release unlock;
                return image::merge(ptrs);
            },
            "channels"_a,
            "One Image from 1 to 4 single-channel Images of the same size, in channel order\n"
            "(the inverse of split()). Raises ValueError otherwise.")
        .def("composite", &image::composite, "overlay"_a, "x"_a = 0, "y"_a = 0, release(),
             "This image with `overlay` blended on top at (x, y): the usual \"over\" operator with\n"
             "straight alpha. The overlay may hang over the edges (it is clipped) and may have any\n"
             "channel count: without alpha it is opaque, gray is replicated into colour. The result\n"
             "has this image's channel count; if that has no alpha, the overlay's alpha just\n"
             "weights the blend.")
        .def(
            "flatten",
            [](const image& i, nb::handle background) {
                const auto bg = colour_of<3>(background, 3, "background");
                nb::gil_scoped_release unlock;
                return i.flatten(bg);
            },
            "background"_a = nb::make_tuple(255, 255, 255),
            "Blend the alpha channel onto a solid colour (r, g, b; default white, or one int) and\n"
            "drop it: 2 -> 1 channel, 4 -> 3. An image without alpha is copied.")

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
        // DLPack: np.from_dlpack(img), torch.from_dlpack(img), jax.numpy.from_dlpack(img),
        // cupy.from_dlpack(img) ... all zero-copy, and libstb imports none of them. The
        // pixels go out as nanobind's own DLPack object (no numpy needed) that keeps this
        // image alive; it speaks both capsule versions, so consumers get a writable view.
        // stream, max_version and dl_device are forwarded; copy=True exports a fresh copy.
        .def(
            "__dlpack__",
            [](nb::handle self, nb::args args, nb::kwargs kwargs) {
                // copy=True is ours to honour (nanobind refuses it): export a fresh copy.
                nb::object owner = nb::borrow(self);
                if (kwargs.contains("copy")) {
                    nb::object want_copy = kwargs["copy"];
                    nb::del(kwargs["copy"]);
                    if (!want_copy.is_none() && nb::cast<bool>(want_copy))
                        owner = nb::cast(nb::cast<stb::image&>(self).copy());
                }
                auto& img = nb::cast<stb::image&>(owner);
                const std::size_t shape[3] = {std::size_t(img.height()), std::size_t(img.width()),
                                              std::size_t(img.channels())};
                nb::ndarray<nb::array_api, std::uint8_t, nb::ndim<3>, nb::device::cpu> a(
                    img.data(), 3, shape, owner);
                return nb::cast(a).attr("__dlpack__")(*args, **kwargs);
            },
            "DLPack export of the pixels (uint8, shape (height, width, channels), no copy).\n"
            "Use np.from_dlpack(img), torch.from_dlpack(img), jax.numpy.from_dlpack(img), ...")
        .def("__dlpack_device__", [](const image&) { return std::make_tuple(1, 0); },  // kDLCPU, device 0
             "(device type, device id): always the CPU.")
        .def(
            "tobytes",
            [](const image& i) {
                return nb::bytes(reinterpret_cast<const char*>(i.data()),
                                 std::size_t(i.height()) * std::size_t(i.width()) * std::size_t(i.channels()));
            },
            "The pixels as bytes, row-major (height, width, channels). A copy; needs no numpy.")
        .def("__array__",
             [](nb::handle self, nb::object dtype, nb::object copy) -> nb::object {
                 nb::object a = nb::cast(pixels_of(self));
                 if (!dtype.is_none()) {
                     nb::module_ np = nb::module_::import_("numpy");
                     if (!np.attr("dtype")(dtype).equal(np.attr("dtype")("uint8")))
                         return a.attr("astype")(dtype);  // always a copy
                 }
                 if (!copy.is_none() && nb::cast<bool>(copy)) return a.attr("copy")();
                 return a;
             },
             "dtype"_a = nb::none(), "copy"_a = nb::none())
        .def_prop_ro(
            "__array_interface__",
            [](nb::handle self) {
                const auto& i = nb::cast<const image&>(self);
                nb::dict d;
                d["version"] = 3;
                // A gray image is reported as (H, W): the shape Pillow (and NumPy's own
                // conventions for single-channel images) expect. np.asarray(img) and
                // img.numpy() still give (H, W, 1): NumPy reads the buffer protocol first.
                d["shape"] = i.channels() == 1 ? nb::make_tuple(i.height(), i.width())
                                               : nb::make_tuple(i.height(), i.width(), i.channels());
                d["typestr"] = "|u1";
                d["data"] = nb::make_tuple(reinterpret_cast<std::uintptr_t>(i.data()), false);
                d["strides"] = nb::none();
                return d;
            },
            "NumPy's array interface (version 3): lets Pillow, and anything else that reads it,\n"
            "take the pixels: PIL.Image.fromarray(img). Keep the Image alive while using the address.")
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
