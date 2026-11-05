#include <nanobind/stl/string.h>

#include <string>

#include "binding.hpp"
#include "stb/logging.hpp"
#include "stb/util.hpp"

// The bridge from stb::logging to Python's `logging`.
//
// A callback_handler on the "libstb" logger hands every record (the whole libstb.* tree propagates to
// it) to logging.getLogger(record.name).log(levelno, message). Levels are numerically the same in both
// worlds, and the dotted names ("libstb.simd") become the same hierarchy in Python, so there is nothing
// to translate. Filtering belongs to Python: while the bridge is installed the C++ logger is opened to
// debug (unless LIBSTB_LOG is set: an explicit choice of the user is kept), and the Python logger's
// level decides what is shown.

namespace {

stb::logging::callback_handler* g_bridge = nullptr;                     // owned by the "libstb" logger
stb::logging::level g_previous_level = stb::logging::level::notset;     // the logger's level before install

// What a record needs from Python, made once. Python's own loggers are looked up once per name and kept,
// and a record is built into Python strings only when that logger is enabled for it: when Python's level
// filters it out (the usual case at WARNING) the cost is one cached dict lookup and one isEnabledFor call.
// The object is never freed: a record can arrive while the interpreter shuts down, and a destructor that
// runs after it would touch a dead interpreter. (One small leak at exit, no more.)
struct bridge_state {
    nb::object get_logger;  // logging.getLogger
    nb::dict loggers;       // record name -> logging.Logger
};
bridge_state* g_state = nullptr;

void forward(const stb::logging::record& r) {
    // Never throw into libstb (a logger must not make the call that logs fail), never touch Python once
    // it is shut down (Py_IsFinalizing is not in the stable ABI the extension is built for;
    // Py_IsInitialized turns false early enough in Py_FinalizeEx).
    if (!Py_IsInitialized() || g_state == nullptr) return;
    try {
        nb::gil_scoped_acquire gil;
        const nb::str name(r.name.data(), r.name.size());
        nb::object lg;
        if (PyObject* hit = PyDict_GetItemWithError(g_state->loggers.ptr(), name.ptr())) {  // borrowed
            lg = nb::borrow(hit);
        } else {
            if (PyErr_Occurred()) throw nb::python_error();
            lg = g_state->get_logger(name);
            g_state->loggers[name] = lg;
        }
        const int level = static_cast<int>(r.levelno);
        if (!nb::cast<bool>(lg.attr("isEnabledFor")(level))) return;
        lg.attr("log")(level, nb::str(r.message.data(), r.message.size()));
    } catch (...) {
    }
}

void set_bridge(bool enabled) {
    stb::logging::logger& lib = stb::logging::get_logger("libstb");
    if (enabled == (g_bridge != nullptr)) return;
    if (enabled) {
        g_previous_level = lib.get_level();
        g_bridge = &lib.add_handler(stb::logging::callback_handler(forward));
        if (stb::env("LIBSTB_LOG").empty()) lib.set_level(stb::logging::level::debug);
    } else {
        lib.remove_handler(*g_bridge);
        g_bridge = nullptr;
        if (stb::env("LIBSTB_LOG").empty()) lib.set_level(g_previous_level);
    }
}

}  // namespace

void bind_logging(nb::module_& m) {
    m.def("logging_bridge", &set_bridge, nb::arg("enabled"),
          "Send libstb's log records to Python's `logging` (True, the state after `import libstb`) or\n"
          "stop doing so (False: libstb goes back to its own output, LIBSTB_LOG and stderr).");

    // Private: lets the tests emit a record through the C++ side of the bridge.
    m.def("_log", [](const std::string& name, int level, const std::string& message) {
        stb::logging::get_logger(name).log(static_cast<stb::logging::level>(level), "%s", message.c_str());
    });

    g_state = new bridge_state{nb::module_::import_("logging").attr("getLogger"), nb::dict()};
    set_bridge(true);
}
