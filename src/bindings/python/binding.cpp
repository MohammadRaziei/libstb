#include "binding.hpp"

NB_MODULE(libstb_py, m) {
    m.doc() = "libstb native module (stb single-header libraries)";

    // --- exceptions: the C++ hierarchy, mirrored ---
    // Registration order matters: nanobind tries translators newest-first, so
    // the base is registered before its subclasses.
    nb::exception<stb::error> py_error(m, "Error", PyExc_RuntimeError);
    nb::exception<stb::decode_error>(m, "DecodeError", py_error);
    nb::exception<stb::encode_error>(m, "EncodeError", py_error);
    nb::exception<stb::limit_error>(m, "LimitError", py_error);

    // stb::io_error is not a libstb.Error: it becomes the builtin OSError, or
    // the subclass its errno selects (FileNotFoundError, PermissionError, ...).
    // Registered last, so it is tried before the base-class translator above.
    nb::register_exception_translator([](const std::exception_ptr& p, void*) {
        try {
            std::rethrow_exception(p);
        } catch (const stb::io_error& e) {
            nb::object os_error = nb::module_::import_("builtins").attr("OSError");
            nb::object exc = e.code() ? os_error(e.code(), e.what()) : os_error(e.what());
            PyErr_SetObject(PyExc_OSError, exc.ptr());
        }
    });

    bind_resizer(m);
    bind_image(m);
    bind_font(m);
}
