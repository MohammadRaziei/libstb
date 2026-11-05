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

void forward(const stb::logging::record& r) {
    // Never throw into libstb (a logger must not make the call that logs fail), never touch Python once
    // it is shut down (Py_IsFinalizing is not in the stable ABI the extension is built for;
    // Py_IsInitialized turns false early enough in Py_FinalizeEx).
    if (!Py_IsInitialized()) return;
    try {
        nb::gil_scoped_acquire gil;
        nb::object lg = nb::module_::import_("logging").attr("getLogger")(std::string(r.name));
        lg.attr("log")(static_cast<int>(r.levelno), std::string(r.message));
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

    set_bridge(true);
}
