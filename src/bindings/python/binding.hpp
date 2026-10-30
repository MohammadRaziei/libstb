#pragma once

// Internal helpers shared by the bind_*.cpp files.

#include <nanobind/nanobind.h>

#include <array>
#include <stdexcept>
#include <string>

#include "stb.h"

namespace nb = nanobind;

// A contiguous bytes-like object (bytes, bytearray, memoryview, array.array, a numpy array...),
// held as a Py_buffer and released however the scope is left. Throws the Python error
// (TypeError, BufferError) if `obj` does not offer one.
struct py_buffer {
    Py_buffer view{};
    bool ok = false;

    py_buffer() = default;
    explicit py_buffer(PyObject* obj) {
        if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) != 0) throw nb::python_error();
        ok = true;
    }
    py_buffer(const py_buffer&) = delete;
    py_buffer& operator=(const py_buffer&) = delete;
    ~py_buffer() {
        if (ok) PyBuffer_Release(&view);
    }
};

inline std::uint8_t byte_of(nb::handle h) {
    const long v = nb::cast<long>(h);
    if (v < 0 || v > 255) throw std::invalid_argument("colour values must be in 0..255");
    return static_cast<std::uint8_t>(v);
}

// A colour argument: an int (every value the same) or a sequence of exactly `n` ints.
template <std::size_t N>
inline std::array<std::uint8_t, N> colour_of(nb::handle obj, std::size_t n, const char* what) {
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

// A colour: an int (gray), (r, g, b), (r, g, b, a) (a missing alpha is 255), or a string
// stb::color parses ("red", "#ff8800", "#f80", "ff8800").
inline stb::color text_colour(nb::handle obj) {
    if (nb::isinstance<nb::str>(obj)) return stb::color(nb::cast<std::string>(obj));
    if (PyLong_Check(obj.ptr())) {
        const std::uint8_t v = byte_of(obj);
        return stb::color(v, v, v);
    }
    if (nb::len(nb::cast<nb::sequence>(obj)) == 4) {
        const auto c = colour_of<4>(obj, 4, "color");
        return stb::color(c[0], c[1], c[2], c[3]);
    }
    const auto c = colour_of<3>(obj, 3, "color");
    return stb::color(c[0], c[1], c[2]);
}

// An enum argument given as the enum or by its lower-case name ("multiply", "center").
inline stb::blend_mode to_blend_mode(nb::handle h) {
    if (nb::isinstance<nb::str>(h)) return stb::blend_mode_from_name(nb::cast<std::string>(h));
    stb::blend_mode m;
    if (nb::try_cast<stb::blend_mode>(h, m, /*convert=*/false)) return m;
    throw nb::type_error("mode must be a BlendMode or its name, e.g. 'multiply'");
}

inline stb::align to_align(nb::handle h) {
    if (nb::isinstance<nb::str>(h)) {
        const std::string n = nb::cast<std::string>(h);
        if (n == "start") return stb::align::start;
        if (n == "center") return stb::align::center;
        if (n == "end") return stb::align::end;
        throw std::invalid_argument("align must be 'start', 'center' or 'end'");
    }
    stb::align a;
    if (nb::try_cast<stb::align>(h, a, /*convert=*/false)) return a;
    throw nb::type_error("align must be an Align or one of 'start', 'center', 'end'");
}

void bind_image(nb::module_& m);
void bind_resizer(nb::module_& m);
void bind_font(nb::module_& m);
void bind_zlib(nb::module_& m);
