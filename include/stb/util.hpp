#pragma once

// Small helpers with no better home: environment variables, string formatting, UTF-8 and base64.

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stb/export.hpp"

// The compiler checks the arguments of the sformat family against their format string (-Wformat),
// where it can: `format(printf, A, B)` says "argument A is the format, the values start at B" (B is
// 0 for the va_list versions). MSVC marks the format parameter for its code analysis.
#if defined(__GNUC__) || defined(__clang__)
#define STB_FORMAT_CHECK(format_index, first_value) __attribute__((format(printf, format_index, first_value)))
#define STB_FORMAT_STRING
#elif defined(_MSC_VER)
#include <sal.h>
#define STB_FORMAT_CHECK(format_index, first_value)
#define STB_FORMAT_STRING _Printf_format_string_
#else
#define STB_FORMAT_CHECK(format_index, first_value)
#define STB_FORMAT_STRING
#endif
namespace stb {

// The value of the environment variable `name`, as a copy. When it is not defined, `def` is
// returned instead (an empty string when `def` is nullptr, the default). A variable that is
// defined but empty gives an empty string, not `def`.
//
// MSVC marks std::getenv as unsafe (C4996), because the pointer it returns can be invalidated by
// a concurrent putenv, and asks for _dupenv_s, which hands out a copy: that is what is used
// there. Elsewhere the value is copied right away.
inline std::string env(const char* name, const char* def = nullptr) {
#if defined(_MSC_VER)
    char* buf = nullptr;
    std::size_t len = 0;
    const bool found = _dupenv_s(&buf, &len, name) == 0 && buf != nullptr;
    std::string out = found ? std::string(buf) : std::string(def ? def : "");
    std::free(buf);
    return out;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string(def ? def : "");
#endif
}

namespace detail {

// Appends the text vsnprintf makes from (fmt, args) to `out`. Most results fit the stack buffer
// and cost one vsnprintf and one append; a longer one is measured by that first call, `out` is grown
// once to that size and the text is formatted straight into it.
inline void sformat_append(std::string& out, const char* fmt, std::va_list args) {
    char buf[256];
    std::va_list again;
    va_copy(again, args);
    struct end_list {
        std::va_list& l;
        ~end_list() { va_end(l); }
    } guard{again};
    const int n = std::vsnprintf(buf, sizeof buf, fmt, args);
    if (n < 0) return;  // an encoding error (for instance %ls): nothing is appended
    if (std::size_t(n) < sizeof buf) {
        out.append(buf, std::size_t(n));
        return;
    }
    const std::size_t old = out.size();
    out.resize(old + std::size_t(n));
    std::vsnprintf(&out[old], std::size_t(n) + 1, fmt, again);  // writes the terminating NUL at out[size()], which is allowed
}

}  // namespace detail

// printf for std::string: sformat("%dx%d (%s)", w, h, name). The format and its conversions are
// those of printf (%d %zu %lld %s %f %x %c %%, widths, precisions, `%.*s` for a string_view); the
// compiler warns about an argument that does not match its conversion on GCC and Clang. An encoding
// error gives an empty string rather than an exception; only std::bad_alloc can be thrown. Like
// printf it follows the C locale of the process, which libstb never changes (a program that sets
// LC_NUMERIC may get a comma in %f), and a null %s is undefined.
STB_FORMAT_CHECK(1, 0)
inline std::string vsformat(STB_FORMAT_STRING const char* fmt, std::va_list args) {
    std::string out;
    detail::sformat_append(out, fmt, args);
    return out;
}

STB_FORMAT_CHECK(1, 2)
inline std::string sformat(STB_FORMAT_STRING const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    struct end_list {
        std::va_list& l;
        ~end_list() { va_end(l); }
    } guard{args};
    return vsformat(fmt, args);
}

// The same, appended to the end of `out`: building a multi-part text costs no temporary string.
STB_FORMAT_CHECK(2, 3)
inline void sformat_to(std::string& out, STB_FORMAT_STRING const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    struct end_list {
        std::va_list& l;
        ~end_list() { va_end(l); }
    } guard{args};
    detail::sformat_append(out, fmt, args);
}

// Strict UTF-8 -> code points. Throws std::invalid_argument on malformed
// input: truncated or stray continuation bytes, overlong encodings,
// surrogates (U+D800..DFFF) and values above U+10FFFF.
STB_API std::u32string utf8_decode(std::string_view text);

// Base64 with the standard alphabet (RFC 4648: A-Z a-z 0-9 + /), the encoding of data: URLs.
//
// Decoding follows the "forgiving-base64 decode" of the WHATWG Infra standard, which is what browsers do for
// atob() and data: URLs: ASCII white space (space, tab, LF, FF, CR) is ignored anywhere; the padding is optional,
// but when it is there it must complete the last group of four (one or two '=', and only at the very end); a
// character outside the alphabet, data after the padding, or a single leftover character (the text without
// padding is 1 more than a multiple of 4 long) is an error. The unused bits of the last character are ignored.
// Empty text, or text of white space only, is zero bytes.
// try_base64_decode gives nullopt on an error; base64_decode throws std::invalid_argument.
STB_API std::optional<std::vector<std::uint8_t>> try_base64_decode(std::string_view text);
STB_API std::vector<std::uint8_t> base64_decode(std::string_view text);

// The text of `size` bytes in base64, with padding, on one line.
STB_API std::string base64_encode(const std::uint8_t* data, std::size_t size);

}  // namespace stb
