#pragma once

// Internal: the range of a pixel type, so that no algorithm has to write 255 for "white". An unsigned integer holds 0 to its own
// maximum (255 for 8 bits, 65535 for 16 bits); a float holds 0 to 1 (the convention of the library).

#include <limits>
#include <type_traits>

namespace stb {
namespace detail_pixel {

template <class T>
struct range {
    static_assert(std::is_floating_point_v<T> || std::is_unsigned_v<T>, "a pixel is an unsigned integer or a float");
    static constexpr bool is_integer = !std::is_floating_point_v<T>;
    // "white": the number a level is divided by to get a value in [0, 1] (1 for a float)
    static constexpr T max = is_integer ? std::numeric_limits<T>::max() : T(1);

    // A value computed in floats, in the units of the pixel (0 to max), as a pixel: a float stays as it is, an integer is rounded
    // to the nearest and clamped to 0 to max.
    static T store(float v) noexcept {
        if constexpr (is_integer) return v <= 0.0f ? T(0) : (v >= float(max) ? max : T(v + 0.5f));
        else return v;
    }
};

}  // namespace detail_pixel
}  // namespace stb
