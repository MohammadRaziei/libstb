#pragma once

// color: an RGBA colour (0xRRGGBBAA) with hex and CSS-name parsing and the `_c` literal.
// Derived from novasvg (mohammadraziei/novasvg, include/novasvg/detail/color.h) so that both
// libraries share one colour class; the namespace is stb and the names follow libstb's
// snake_case style (Color -> color, getR() -> r(), fromString -> from_string, ...).
// Parsing, formatting and blending live in src/draw/color.cpp; this header keeps the class
// surface and the trivial inline members.

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "stb/export.hpp"

namespace stb {

struct color_blend;

/**
 * @brief Thrown when a color string cannot be parsed (bad hex digits or length, unknown name).
 *
 * Derives from std::invalid_argument, so `catch (const std::invalid_argument&)` keeps working.
 * In Python it is `libstb.invalid_color_error`, a subclass of `ValueError`.
 */
class STB_API invalid_color_error : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

/**
 * @brief An RGBA color stored as four 8-bit channels (0xRRGGBBAA when packed).
 *
 * @code
 * color c1("#FF0000");        // hex string
 * color c2("skyblue");        // CSS name (case-insensitive)
 * color c3 = "red"_c;         // literal (using namespace stb::color_literals)
 * color c4 = "gray"_c | 50;   // blend with transparent, LaTeX style
 * color c5 = "gray"_c | "black"_c;   // 50% blend of two colors
 * uint32_t v = c1.value();    // 0xRRGGBBAA
 * @endcode
 *
 * The alpha channel defaults to 255 (opaque) when not given.
 */
class STB_API color {
public:
    // ---- Constructors ----

    /// Opaque black.
    color() : color(0, 0, 0, 255) {}

    /// Not explicit on purpose: `c.set_color({255, 0, 0})` works. There is no one- or two-value
    /// form, so a stray int (`color c = 5;`) does not turn into a colour by accident.
    color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r_(r), g_(g), b_(b), a_(a) {}

    /// Parses a hex or CSS-name string, see from_string().
    color(const std::string& str) : color(from_string(str)) {}

    /// So that a string literal converts directly (`c.set_color("white")`): const char* ->
    /// std::string -> color would be two user conversions, which C++ does not chain.
    color(const char* str) : color(from_string(std::string(str))) {}

    color(std::nullptr_t) = delete;  // keeps `color(0)` from meaning a null string

    /// Converts from any type that has red(), green(), blue() and alpha() accessors.
    template <typename OtherColor,
              typename = decltype(std::declval<const OtherColor&>().red()),
              typename = decltype(std::declval<const OtherColor&>().green()),
              typename = decltype(std::declval<const OtherColor&>().blue()),
              typename = decltype(std::declval<const OtherColor&>().alpha())>
    constexpr color(const OtherColor& other)
        : r_(static_cast<uint8_t>(other.red())),
          g_(static_cast<uint8_t>(other.green())),
          b_(static_cast<uint8_t>(other.blue())),
          a_(static_cast<uint8_t>(other.alpha())) {}

    /// Converts to any type constructible from (r, g, b, a).
    template <typename OtherColor>
    constexpr OtherColor as() const {
        return OtherColor(r_, g_, b_, a_);
    }

    // ---- Channels ----

    uint8_t r() const { return r_; }
    uint8_t g() const { return g_; }
    uint8_t b() const { return b_; }
    uint8_t a() const { return a_; }
    void set_r(uint8_t value) { r_ = value; }
    void set_g(uint8_t value) { g_ = value; }
    void set_b(uint8_t value) { b_ = value; }
    void set_a(uint8_t value) { a_ = value; }

    /// The color packed as 0xRRGGBBAA.
    uint32_t value() const {
        return (static_cast<uint32_t>(r_) << 24) | (static_cast<uint32_t>(g_) << 16) |
               (static_cast<uint32_t>(b_) << 8) | static_cast<uint32_t>(a_);
    }

    friend bool operator==(const color& lhs, const color& rhs) {
        return lhs.r_ == rhs.r_ && lhs.g_ == rhs.g_ && lhs.b_ == rhs.b_ && lhs.a_ == rhs.a_;
    }
    friend bool operator!=(const color& lhs, const color& rhs) { return !(lhs == rhs); }

    // ---- String conversion ----

    /// "color(r=255, g=0, b=0, a=255)".
    std::string to_string() const;
    operator std::string() const { return to_string(); }

    // ---- Blending ----

    /// `c | 50` defers the blend: the result is a color_blend that blends with transparent
    /// when used as a color, or with the next color when chained (`c | 50 | other`).
    color_blend operator|(int percentage) const;

    /// 50% blend of the two colors.
    color operator|(const color& other) const { return blend(other, 50); }

    /// Linear mix per channel: `percentage` (clamped to 0..100) percent of `other`.
    color blend(const color& other, int percentage) const;

    // ---- Factories ----

    /**
     * @brief Parses "#RGB", "#RGBA", "#RRGGBB", "#RRGGBBAA", the same without '#', or a CSS name.
     * @throws invalid_color_error on an empty string, bad hex digits or length, or an unknown name.
     */
    static color from_string(const std::string& str);

    /**
     * @brief Parses a hex string: "#RGB", "#RGBA", "#RRGGBB" or "#RRGGBBAA".
     * @throws invalid_color_error if the '#' is missing, the length is wrong, or a digit is not hex.
     */
    static color from_hash(const std::string& hex_code);

    /**
     * @brief Looks up a CSS color name (case-insensitive; "gray" and "grey" both work).
     *
     * Known names: black, white, red, green, lime, blue, yellow, cyan, magenta, silver, gray,
     * grey, maroon, olive, purple, teal, navy, orange, pink, brown, transparent.
     * @throws invalid_color_error if the name is not recognized.
     */
    static color from_name(const std::string& name);

    static color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return color(r, g, b, a); }

    /// From 0xRRGGBBAA.
    static color from_value(uint32_t value) {
        return color(static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
                     static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF));
    }

    /**
     * @brief From 0xRRGGBB; the result is always opaque.
     * @throws std::invalid_argument if value is above 0xFFFFFF (most likely a 0xRRGGBBAA value,
     *         which belongs to from_value()).
     */
    static color from_rgb(uint32_t value) {
        if (value > 0xFFFFFFu) {
            throw std::invalid_argument(
                "color::from_rgb: value does not fit in 0xRRGGBB (use from_value for 0xRRGGBBAA)");
        }
        return color(static_cast<uint8_t>((value >> 16) & 0xFF), static_cast<uint8_t>((value >> 8) & 0xFF),
                     static_cast<uint8_t>(value & 0xFF));
    }

    // ---- Predefined colors (CSS values; green is (0, 128, 0), lime is (0, 255, 0)) ----

    static color black()       { return color(0, 0, 0); }
    static color white()       { return color(255, 255, 255); }
    static color red()         { return color(255, 0, 0); }
    static color green()       { return color(0, 128, 0); }
    static color lime()        { return color(0, 255, 0); }
    static color blue()        { return color(0, 0, 255); }
    static color yellow()      { return color(255, 255, 0); }
    static color cyan()        { return color(0, 255, 255); }
    static color magenta()     { return color(255, 0, 255); }
    static color silver()      { return color(192, 192, 192); }
    static color gray()        { return color(128, 128, 128); }
    static color maroon()      { return color(128, 0, 0); }
    static color olive()       { return color(128, 128, 0); }
    static color purple()      { return color(128, 0, 128); }
    static color teal()        { return color(0, 128, 128); }
    static color navy()        { return color(0, 0, 128); }
    static color orange()      { return color(255, 165, 0); }
    static color pink()        { return color(255, 192, 203); }
    static color brown()       { return color(165, 42, 42); }
    static color transparent() { return color(0, 0, 0, 0); }

private:
    uint8_t r_;
    uint8_t g_;
    uint8_t b_;
    uint8_t a_;
};

/// The pending result of `color | percentage`: converts to a color (blend with transparent) or
/// takes part in a longer chain such as `"gray"_c | 50 | "black"_c`.
struct STB_API color_blend {
    color base_color;
    int percentage;  ///< 0-100

    color_blend(const color& base, int percent) : base_color(base), percentage(percent) {}

    color operator|(const color& other) const { return base_color.blend(other, percentage); }

    color_blend operator|(const color_blend& other) const {
        // First blend: base_color | percentage -> transparent; the second percentage then applies
        // to the next color in the chain.
        return color_blend(base_color.blend(color::transparent(), percentage), other.percentage);
    }

    operator color() const { return base_color.blend(color::transparent(), percentage); }
};

// Defined here because color_blend is incomplete inside class color.
inline color_blend color::operator|(int percentage) const { return color_blend(*this, percentage); }

STB_API std::ostream& operator<<(std::ostream& os, const color& c);
STB_API std::ostream& operator<<(std::ostream& os, const color_blend& blend);

}  // namespace stb

namespace stb::color_literals {

/// `"red"_c`, `"#ff8800"_c`: same parsing as color::from_string().
inline color operator""_c(const char* str, std::size_t len) { return color::from_string(std::string(str, len)); }

/// `0xFF8800_rgb`: an opaque color from 0xRRGGBB.
inline color operator""_rgb(unsigned long long value) {
    if (value > 0xFFFFFFull) {
        throw std::invalid_argument("_rgb: value does not fit in 0xRRGGBB");
    }
    return color::from_rgb(static_cast<uint32_t>(value));
}

}  // namespace stb::color_literals
