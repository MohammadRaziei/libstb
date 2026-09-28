#pragma once

// Image reading (stb_image). The stb header itself is never included here:
// it is compiled into src/core/image.cpp only, with STB_IMAGE_STATIC, so no
// stbi_* symbol leaks out of libstb_core (safe to link next to your own stb).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace libstb {

struct image_info {
    int width = 0;
    int height = 0;
    int channels = 0;  // channels in the source file
};

// 8-bit, row-major, interleaved: data.size() == width * height * channels.
struct image {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<std::uint8_t> data;
};

struct load_options {
    int channels = 0;    // 0 = keep the source's, 1..4 = convert to that many
    bool flip = false;   // flip vertically (thread-safe, per call)
    // Refuse to decode anything whose output would exceed this many bytes.
    // Checked from the header BEFORE any pixel memory is allocated.
    std::size_t max_bytes = std::size_t(1) << 29;  // 512 MiB
};

// All functions are thread-safe. Errors are reported by exception:
//   std::invalid_argument - bad arguments (null buffer, channels not in 0..4)
//   std::runtime_error    - undecodable/unsupported input, or over max_bytes
image_info info(const void* data, std::size_t size);
image load(const void* data, std::size_t size, const load_options& opt = {});

}  // namespace libstb
