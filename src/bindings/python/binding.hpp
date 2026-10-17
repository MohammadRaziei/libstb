#pragma once

// Internal helpers shared by the bind_*.cpp files.

#include <nanobind/nanobind.h>

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

void bind_image(nb::module_& m);
void bind_resizer(nb::module_& m);
void bind_font(nb::module_& m);
void bind_zlib(nb::module_& m);
