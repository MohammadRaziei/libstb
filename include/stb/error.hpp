#pragma once

#include <stdexcept>

namespace stb {

// Everything libstb can fail at *at runtime* derives from stb::error, so
// `catch (const stb::error&)` handles all of it:
//
//   error                     (std::runtime_error)
//   |- decode_error           input is corrupt, truncated, empty or an unsupported format
//   |- encode_error           an encoder failed to produce output
//   |- limit_error            a size limit was hit (max_bytes, 2 GiB stb ceiling)
//   `- io_error               a file could not be opened, read or written
//
// Programmer errors (null pointer, channels outside 1..4, unknown file
// extension, quality outside 1..100 ...) are NOT stb::error: they throw
// std::invalid_argument, following the std convention that logic errors and
// runtime errors are different families.
class error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class decode_error : public error {
public:
    using error::error;
};

class encode_error : public error {
public:
    using error::error;
};

class limit_error : public error {
public:
    using error::error;
};

class io_error : public error {
public:
    using error::error;
};

}  // namespace stb
