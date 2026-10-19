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

void bind_image(nb::module_& m);
void bind_resizer(nb::module_& m);
void bind_font(nb::module_& m);
void bind_zlib(nb::module_& m);
