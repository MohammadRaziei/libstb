#include "binding.hpp"

NB_MODULE(libstb_py, m) {
    m.doc() = "libstb native module (stb single-header libraries)";

    // --- exceptions: the C++ hierarchy, mirrored ---
    // Registration order matters: nanobind tries translators newest-first, so
    // the base is registered before its subclasses.
    // (stb::io_error is deliberately not registered: the Python package
    // does its own file I/O, so it surfaces as a plain OSError there; if it
    // ever escapes, it maps to Error via its base class.)
    nb::exception<stb::error> py_error(m, "Error", PyExc_RuntimeError);
    nb::exception<stb::decode_error>(m, "DecodeError", py_error);
    nb::exception<stb::encode_error>(m, "EncodeError", py_error);
    nb::exception<stb::limit_error>(m, "LimitError", py_error);

    bind_image(m);
    bind_encode(m);
    bind_resizer(m);
    bind_font(m);
}
