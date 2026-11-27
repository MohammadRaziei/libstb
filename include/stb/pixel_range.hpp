#pragma once

// The range of a pixel type, so that no algorithm has to write 255 for "white". An integer type holds the values from its lowest
// to its own maximum (0 to 255 for 8 bits, 0 to 65535 for 16 bits, -32768 to 32767 for a signed 16 bits); a float holds 0 to 1 (the
// convention of the library) and is not clamped.
//
// "White" is `max`: a level is read as level / max, so an unsigned type is 0 to 1 and a signed type -1 to 1 (its lowest value is a
// hair below -1, as in the usual integer-to-float conversions).

#include <limits>
#include <type_traits>

namespace stb {

template <class T>
struct pixel_range {
    static_assert((std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_floating_point_v<T>,
                  "a pixel is an integer (not bool) or a float");
    static constexpr bool is_integer = std::is_integral_v<T>;
    // "white": the number a level is divided by to get a value in [0, 1] (1 for a float)
    static constexpr T max = is_integer ? std::numeric_limits<T>::max() : T(1);
    // the smallest value an integer type holds (0 for an unsigned type); a float has no lower limit and this is 0
    static constexpr T lowest = is_integer ? std::numeric_limits<T>::lowest() : T(0);

    // A value computed in floating point (F is float or double), in the units of the pixel (0 to max), as a pixel: a float stays as
    // it is; an integer is rounded to the nearest (half away from zero) and clamped to lowest to max, and NaN becomes 0.
    template <class F>
    static T store(F v) noexcept {
        static_assert(std::is_floating_point_v<F>);
        if constexpr (!is_integer) {
            return T(v);
        } else {
            if (v != v) return T(0);
            if (v <= F(lowest)) return lowest;
            if (v >= F(max)) return max;
            if constexpr (std::is_signed_v<T>) return T(v < F(0) ? v - F(0.5) : v + F(0.5));
            else return T(v + F(0.5));
        }
    }
};

}  // namespace stb
