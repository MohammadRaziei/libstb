#pragma once

// stb::env: read an environment variable.

#include <cstdlib>
#include <string>

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

}  // namespace stb
