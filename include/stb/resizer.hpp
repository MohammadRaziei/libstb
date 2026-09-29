#pragma once

// stb_image_resize2 lives in src/core/resizer.cpp only.

#include <cstddef>
#include <string_view>

#include "stb/error.hpp"
#include "stb/image.hpp"

namespace stb {

enum class resize_filter {
    automatic,      // Catmull-Rom when enlarging, Mitchell when shrinking
    box,
    triangle,       // == bilinear when enlarging
    cubic_bspline,  // smooth, slightly blurry
    catmull_rom,    // sharp, interpolating
    mitchell,       // good all-round compromise
    point           // nearest neighbour
};

enum class resize_edge { clamp, reflect, wrap, zero };

// Maps a scipy/PIL-style name to a filter. Case-insensitive; '-' and ' ' are
// treated like '_'. Accepted names:
//   "auto" "automatic" "default"        -> automatic
//   "nearest" "point"                   -> point
//   "linear" "bilinear" "triangle"      -> triangle
//   "cubic" "bicubic" "catmull_rom"     -> catmull_rom   (PIL/OpenCV meaning of "cubic")
//   "bspline" "cubic_bspline"           -> cubic_bspline (scipy's meaning of "cubic")
//   "mitchell"                          -> mitchell
//   "box" "area"                        -> box
// Throws invalid_argument (listing the valid names) for anything else.
resize_filter resize_filter_from_name(std::string_view name);

struct resize_options {
    resize_filter filter = resize_filter::automatic;
    resize_edge edge = resize_edge::clamp;
    // Treat 8-bit colour channels as sRGB and blend in linear light (correct
    // for photos/UI; alpha is always linear). Turn off for data such as
    // normal maps or masks, which are already linear.
    bool srgb = true;
    // Refuse to produce an output larger than this many bytes.
    std::size_t max_bytes = std::size_t(1) << 29;  // 512 MiB
};

// Resizes 8-bit images of 1..4 channels (2 = gray+alpha, 4 = RGBA; alpha is
// weighted correctly, so transparent pixels do not bleed colour).
// Immutable after construction, hence shareable between threads.
class resizer {
public:
    resizer() = default;
    explicit resizer(const resize_options& options);  // throws invalid_argument on a bad enum value
    // Shortcuts for "default options, just this filter":
    //   resizer(resize_filter::mitchell)   resizer("cubic")
    // The name is parsed by resize_filter_from_name (invalid_argument if unknown).
    explicit resizer(resize_filter filter);
    explicit resizer(std::string_view filter_name);

    const resize_options& options() const noexcept { return options_; }

    // Throws invalid_argument (empty image, width/height < 1),
    // limit_error (output larger than options().max_bytes or 2 GiB),
    // error (allocation failure inside stb).
    image resize(const image& src, int width, int height) const;

private:
    resize_options options_;
};

}  // namespace stb
