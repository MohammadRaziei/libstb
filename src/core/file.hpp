#pragma once

// Internal (not installed): whole-file I/O shared by the core sources.

#include <climits>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "stb/error.hpp"

namespace stb::detail {

// Reads the whole file. Refuses files stb could never decode (> INT_MAX)
// BEFORE reading them, so a huge file cannot exhaust memory.
inline std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw io_error("cannot open file: " + path.string());
    const std::streamoff size = f.tellg();
    if (size < 0) throw io_error("cannot determine file size: " + path.string());
    if (size > INT_MAX) throw limit_error("file larger than 2 GiB: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    f.seekg(0);
    if (size > 0 && !f.read(reinterpret_cast<char*>(bytes.data()), size))
        throw io_error("cannot read file: " + path.string());
    return bytes;
}

inline void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw io_error("cannot open file for writing: " + path.string());
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f) throw io_error("cannot write file: " + path.string());
}

}  // namespace stb::detail
