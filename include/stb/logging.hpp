#pragma once

// stb::logging: a small logging module modelled on Python's `logging`, so that the Python binding can
// hand libstb's messages to Python's own logging (same names, same levels, same hierarchy).
//
//   auto& lg = stb::logging::get_logger("libstb.simd");           // logging.getLogger("libstb.simd")
//   lg.warning("backend %s is not available, using %s", want, got);   // printf style (stb::sformat)
//   stb::logging::get_logger("libstb").add_handler(stb::logging::file_handler("stb.log"));
//
// What maps to what:
//   logger     Logger     a node of a tree of dotted names ("libstb" is the parent of "libstb.simd"). A logger
//                         has its own level (notset = ask the parent), passes records to its handlers and then,
//                         if propagate() is true, to the handlers of its parents.
//   handler    Handler    where a record goes: stream_handler, file_handler, callback_handler, null_handler, or
//                         your own class derived from handler. A handler has a level, a formatter and filters.
//   formatter  Formatter  a pattern such as "%(asctime)s [%(levelname)s] %(name)s: %(message)s".
//   record     LogRecord  one message: logger name, level, text, time, thread.
//   level                 the same numbers as Python (debug 10 ... critical 50).
//
// The libstb code logs through loggers named "libstb" and "libstb.<part>". Nothing is printed unless a
// message reaches a handler, with one exception, the same as Python's "last resort": when no handler exists
// anywhere up the tree, warnings and worse go to stderr as "<logger>: <message>". The level of "libstb"
// can be set from the environment with LIBSTB_LOG=debug|info|warning|error|critical (or a number).
//
// Threads: every call is thread safe. A handler writes one record at a time. Logging never throws: an
// exception from a handler or from out of memory is swallowed, as in Python.

#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "stb/export.hpp"
#include "stb/util.hpp"

namespace stb::logging {

// ----------------------------------------------------------------------------------------- levels

enum class level : int { notset = 0, debug = 10, info = 20, warning = 30, error = 40, critical = 50 };

// "DEBUG", "INFO", ... ; "Level 15" for a number in between (as Python does).
STB_API std::string level_name(level lv);

// "debug", "WARNING", "warn", "fatal" (case does not matter) or a whole number: 25 is a level between
// info and warning. Anything else gives nullopt.
STB_API std::optional<level> level_from_string(std::string_view text);

// ---------------------------------------------------------------------------------------- records

struct record {
    std::string_view name;     // the logger's name ("root" for the root logger)
    level levelno;
    std::string_view message;  // already formatted
    std::chrono::system_clock::time_point created;
    std::thread::id thread;
};

// ------------------------------------------------------------------------------------- formatter

// A pattern with Python's %(field)x syntax: %(asctime)s %(created)f %(msecs)d %(levelname)s %(levelno)d
// %(name)s %(message)s %(thread)d %(process)d, and %% for a percent sign. A field may have a width and
// alignment, "%(levelname)-8s", and %(created) a precision, "%(created).3f". asctime is local time:
// "2026-11-04 10:38:12,345", or, when `datefmt` is given, strftime(datefmt) (no milliseconds). A pattern
// that is malformed or names an unknown field throws std::invalid_argument. Cheap to copy.
class STB_API formatter {
public:
    static constexpr const char* default_pattern = "%(asctime)s [%(levelname)s] %(name)s: %(message)s";

    explicit formatter(std::string pattern = default_pattern, std::string datefmt = {});

    std::string format(const record& r) const;
    const std::string& pattern() const noexcept;

private:
    struct impl;
    std::shared_ptr<const impl> impl_;
};

// ------------------------------------------------------------------------------------- handlers

using filter_fn = std::function<bool(const record&)>;  // return false to drop the record

// The base of every handler. To write your own, derive and override emit():
//
//     struct my_handler : stb::logging::handler {
//         void emit(const stb::logging::record& r) override { /* use format(r) or r.message */ }
//     };
//
// emit() is called with the handler's lock held, only for a record that passed the level and the filters,
// so it needs no locking of its own. A handler can be moved but not copied.
class STB_API handler {
public:
    handler();
    virtual ~handler();
    handler(handler&&) noexcept;
    handler& operator=(handler&&) noexcept;
    handler(const handler&) = delete;
    handler& operator=(const handler&) = delete;

    void set_level(level lv) noexcept;  // default notset: every record that reaches the handler
    level get_level() const noexcept;
    void set_formatter(formatter f);
    void add_filter(filter_fn f);

    void handle(const record& r);  // what a logger calls: level, filters, then emit() under the lock
    void flush();

protected:
    virtual void emit(const record& r) = 0;
    virtual void do_flush() {}
    std::string format(const record& r) const;  // with this handler's formatter (the default one if none)

private:
    struct state;
    std::unique_ptr<state> st_;
};

// Discards everything. Python's NullHandler: put one on a library's logger to say "no output unless the
// application configures it".
class STB_API null_handler : public handler {
protected:
    void emit(const record&) override {}
};

enum class color_mode { automatic, always, never };

// One line per record on a C stream (stderr by default), flushed after every record. With
// color_mode::automatic the line is colored by level when the stream is a terminal and NO_COLOR is not
// set (never on Windows, where ANSI sequences need the console set up by the program: use always).
class STB_API stream_handler : public handler {
public:
    explicit stream_handler(std::FILE* file = stderr, color_mode color = color_mode::automatic);

protected:
    void emit(const record& r) override;
    void do_flush() override;

private:
    std::FILE* file_;
    bool color_;
};

// One line per record appended to a file (append = false truncates it first), flushed after every record.
// Throws stb::io_error when the file cannot be opened.
class STB_API file_handler : public handler {
public:
    explicit file_handler(const std::filesystem::path& path, bool append = true);

protected:
    void emit(const record& r) override;
    void do_flush() override;

private:
    std::ofstream out_;
};

// Calls a function with each record (not formatted; format it with a formatter if you want a line). This
// is how the Python binding hands libstb's records to Python's logging.
class STB_API callback_handler : public handler {
public:
    explicit callback_handler(std::function<void(const record&)> fn);

protected:
    void emit(const record& r) override;

private:
    std::function<void(const record&)> fn_;
};

// ------------------------------------------------------------------------------------- loggers

namespace detail {
struct manager;
}

// Get loggers with get_logger(); they live until the program ends, so a reference stays valid.
class STB_API logger {
public:
    ~logger();
    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;

    const std::string& name() const noexcept;
    logger* parent() const noexcept;  // nullptr for the root logger

    void set_level(level lv) noexcept;  // notset: use the parent's
    level get_level() const noexcept;
    level effective_level() const noexcept;  // the first level that is not notset, going up the tree
    bool is_enabled_for(level lv) const noexcept;

    void set_propagate(bool on) noexcept;  // default true: also pass records to the parents' handlers
    bool propagate() const noexcept;

    // Adds the handler (moved into the logger, which owns it) and returns a reference to it, valid until the
    // handler is removed or the logger is cleared: use it to set a level or a formatter, or to remove it.
    template <class H>
    H& add_handler(H h) {
        static_assert(std::is_base_of_v<handler, H>, "add_handler needs a class derived from stb::logging::handler");
        static_assert(std::is_move_constructible_v<H>, "the handler must be movable");
        auto owned = std::make_shared<H>(std::move(h));
        H& ref = *owned;
        add_owned(std::move(owned));
        return ref;
    }
    template <class H>
    void remove_handler(const H& h) {
        remove_owned(&h);
    }
    void clear_handlers();
    std::size_t handler_count() const;
    bool has_handlers() const;  // this logger or a parent it propagates to has one

    // printf style, like stb::sformat (the compiler checks the arguments). The text is built only when the
    // level is enabled; the arguments are still evaluated by the caller.
    STB_FORMAT_CHECK(2, 3) void debug(STB_FORMAT_STRING const char* fmt, ...);
    STB_FORMAT_CHECK(2, 3) void info(STB_FORMAT_STRING const char* fmt, ...);
    STB_FORMAT_CHECK(2, 3) void warning(STB_FORMAT_STRING const char* fmt, ...);
    STB_FORMAT_CHECK(2, 3) void error(STB_FORMAT_STRING const char* fmt, ...);
    STB_FORMAT_CHECK(2, 3) void critical(STB_FORMAT_STRING const char* fmt, ...);
    STB_FORMAT_CHECK(3, 4) void log(level lv, STB_FORMAT_STRING const char* fmt, ...);
    STB_FORMAT_CHECK(3, 0) void vlog(level lv, STB_FORMAT_STRING const char* fmt, std::va_list args);

private:
    friend struct detail::manager;
    logger(std::string name, logger* parent);

    void add_owned(std::shared_ptr<handler> h);
    void remove_owned(const handler* h);
    void dispatch(const record& r);

    struct state;
    std::unique_ptr<state> st_;
};

// logging.getLogger(name). The same name gives the same logger. "" (or "root") is the root logger. Dotted
// names form the tree: "libstb.simd" is a child of "libstb", which is a child of the root. The root's level
// is warning.
STB_API logger& get_logger(std::string_view name = {});

// logging.basicConfig: when the root logger has no handler, adds a stream_handler on stderr with the default
// formatter; then sets the root level.
STB_API void basic_config(level lv = level::warning);

}  // namespace stb::logging
