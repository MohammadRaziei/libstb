#pragma once

// color: an RGBA colour (0xRRGGBBAA) with hex and CSS-name parsing and the `_c` literal.
// Copied from novasvg (mohammadraziei/novasvg, include/novasvg/detail/color.h) so that both
// libraries share one colour class; the namespace is stb, the names follow libstb's
// snake_case style (Color -> color, getR() -> r(), fromString -> from_string, ...) and the
// NOVASVG_MIN/MAX macros became std::clamp. The static constants Color::Black/White/
// Transparent were dropped: black()/white()/transparent() do the same. The "engine-facing additions" block (float
// channels, isOpaque, colorWithAlpha) was left out: only novasvg's renderer used it.

#include <stdexcept>
#include <string>
#include <cstdint>
#include <math.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <ostream>
#include <utility>
#include <type_traits>

/**
 * @namespace stb
 * @brief Main namespace for libstb
 */
namespace stb {
struct color_blend;

/**
 * @brief Thrown when a color string cannot be parsed (bad hex digits or length, unknown name).
 *
 * Derives from std::invalid_argument, so existing `catch (const std::invalid_argument&)` code
 * keeps working. In Python it is `libstb.invalid_color_error`, a subclass of `ValueError`.
 */
class invalid_color_error : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};


/**
 * @class color
 * @brief Represents a color in RGBA format (0xRRGGBBAA).
 * 
 * This class provides a comprehensive color representation with support for
 * various color creation methods and manipulation operations.
 * 
 * @section ColorFeatures Features
 * - RGBA format storage (0xRRGGBBAA)
 * - Implicit conversion to std::string
 * - Stream output operator (<<)
 * - String literal operator (_c)
 * - Blend operator (|) for mixing colors
 * 
 * @section ColorUsage Usage Examples
 * @code
 * // Constructor from hex string
 * color c1("#FF0000");
 * 
 * // Constructor from color name
 * color c2("skyblue");
 * 
 * // Using literal operator
 * color c3 = "red"_c;
 * 
 * // Blend with transparent (LaTeX style)
 * color c4 = "gray"_c | 50;
 * 
 * // Blend with another color
 * color c5 = "gray"_c | "black"_c;
 * 
 * // Chain blends
 * color c6 = "gray"_c | 50 | "black"_c;
 * 
 * // Convert to integer
 * uint32_t val = c1.value();
 * 
 * // Stream output
 * std::cout << c1 << std::endl;
 * 
 * // Implicit string conversion
 * std::string s = c1;
 * @endcode
 * 
 * @note The alpha component defaults to 255 (opaque) when not specified.
 * 
 * @author NovaSVG Team
 * @version 1.0.0
 */
class color {
private:
    uint8_t r_;  ///< Red component (0-255)
    uint8_t g_;  ///< Green component (0-255)
    uint8_t b_;  ///< Blue component (0-255)
    uint8_t a_;  ///< Alpha component (0-255), 0 = transparent, 255 = opaque

public:
    // ==================== Constructors ====================

    /**
     * @brief Construct a new color object from RGBA components.
     * 
     * Creates a color with specified red, green, blue, and alpha components.
     * 
     * @param r Red component (0-255). Default is 0.
     * @param g Green component (0-255). Default is 0.
     * @param b Blue component (0-255). Default is 0.
     * @param a Alpha component (0-255). Default is 255 (fully opaque).
     * 
     * @note Values outside the 0-255 range are clamped automatically.
     * 
     * @par Example
     * @code
     * color c1;              // Black, opaque (0, 0, 0, 255)
     * color c2(255, 0, 0);   // Red, opaque
     * color c3(0, 255, 0, 128); // Green, semi-transparent
     * @endcode
     */
    explicit color(uint8_t r = 0, uint8_t g = 0, uint8_t b = 0, uint8_t a = 255)
        : r_(r), g_(g), b_(b), a_(a) {}

    /**
     * @brief Construct a new color object from a string.
     * 
     * Automatically detects whether the string is a hex color code
     * or a color name and delegates to the appropriate parser.
     * 
     * @param str Hex color string (e.g., "#FF0000", "#F00") or 
     *            color name (e.g., "red", "skyblue")
     * 
     * @throws invalid_color_error (a std::invalid_argument) If the format is invalid or 
     *         the color name is not recognized.
     * 
     * @par Example
     * @code
     * color c1("#FF0000");   // Red from hex
     * color c2("#F00");      // Red from shorthand hex
     * color c3("blue");      // Blue from name
     * color c4("invalid");   // throws invalid_color_error
     * @endcode
     * 
     * @see from_string()
     * @see from_hash()
     * @see from_name()
     */
    color(const std::string& str) : color(from_string(str)) {}

    // ==================== Interop with Other color Types ====================

    /**
     * @brief Construct a color from any "color-like" type.
     *
     * This constructor lets color be built (implicitly) from any other
     * color type that exposes red()/green()/blue()/alpha() accessors
     * returning integral values -- for example stb::color from
     * graphics.h. It exists so that stb::color can be used
     * as a drop-in replacement for stb::color throughout the engine.
     *
     * @warning The conversion is always done component-by-component
     *          (r, g, b, a), NEVER via value()/raw 32-bit integers.
     *          Different color types are free to pack RGBA into a
     *          32-bit value using different byte orders -- this class
     *          uses 0xRRGGBBAA, while e.g. stb::color uses
     *          0xAARRGGBB. Converting through the packed value would
     *          silently corrupt colors.
     *
     * @tparam OtherColor Any type providing red()/green()/blue()/alpha().
     *
     * @par Example
     * @code
     * stb::color engine_color(255, 0, 0);     // engine's own color
     * stb::color c = engine_color;      // implicit conversion in
     * @endcode
     */
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

    /**
     * @brief Convert this color to any "color-like" type constructible
     *        from four (r, g, b, a) components.
     *
     * Symmetric counterpart to the templated constructor above. Lets this
     * color be handed to APIs expecting a different color type (e.g. the
     * engine's stb::color) without ever touching value()/raw bit
     * layout. Kept explicit (a named method, not an implicit conversion
     * operator) since an unconstrained implicit `operator T()` would be
     * far too eager to match unrelated types and would also collide with
     * the existing `operator std::string()` below.
     *
     * @tparam OtherColor Target type, constructible as OtherColor(r, g, b, a).
     *
     * @par Example
     * @code
     * using namespace stb::color_literals;
     * stb::color c = "red"_c;
     * stb::color engine_color = c.as<stb::color>();  // out
     * @endcode
     */
    template <typename OtherColor>
    constexpr OtherColor as() const {
        return OtherColor(r_, g_, b_, a_);
    }

    // ==================== Getters & Setters ====================

    /**
     * @brief Get the red component of the color.
     * @return Red component value (0-255).
     */
    uint8_t r() const { return r_; }
    
    /**
     * @brief Get the green component of the color.
     * @return Green component value (0-255).
     */
    uint8_t g() const { return g_; }
    
    /**
     * @brief Get the blue component of the color.
     * @return Blue component value (0-255).
     */
    uint8_t b() const { return b_; }
    
    /**
     * @brief Get the alpha component of the color.
     * @return Alpha component value (0-255), where 0 is fully transparent
     *         and 255 is fully opaque.
     */
    uint8_t a() const { return a_; }

    /**
     * @brief Set the red component of the color.
     * @param value New red component value (0-255).
     */
    void set_r(uint8_t value) { r_ = value; }
    
    /**
     * @brief Set the green component of the color.
     * @param value New green component value (0-255).
     */
    void set_g(uint8_t value) { g_ = value; }
    
    /**
     * @brief Set the blue component of the color.
     * @param value New blue component value (0-255).
     */
    void set_b(uint8_t value) { b_ = value; }
    
    /**
     * @brief Set the alpha component of the color.
     * @param value New alpha component value (0-255).
     */
    void set_a(uint8_t value) { a_ = value; }

    // ==================== Value Conversion ====================

    /**
     * @brief Convert the color to a 32-bit unsigned integer.
     * 
     * Converts the RGBA color components to a single 32-bit integer
     * in the format 0xRRGGBBAA (little-endian: 0xAABBGGRR on some systems).
     * 
     * @return 32-bit integer representation of the color.
     * 
     * @par Example
     * @code
     * color c(255, 0, 0, 255);  // Red
     * uint32_t val = c.value(); // 0xFF0000FF
     * @endcode
     * 
     * @note The format is 0xRRGGBBAA where:
     *       - RR = Red (bits 24-31)
     *       - GG = Green (bits 16-23)
     *       - BB = Blue (bits 8-15)
     *       - AA = Alpha (bits 0-7)
     */
    uint32_t value() const {
        return (static_cast<uint32_t>(r_) << 24) |
               (static_cast<uint32_t>(g_) << 16) |
               (static_cast<uint32_t>(b_) << 8) |
               static_cast<uint32_t>(a_);
    }

    /**
     * @brief Equality comparison operator.
     * 
     * Compares two colors for equality. Two colors are equal if all their
     * RGBA components are identical.
     * 
     * @param lhs Left-hand side color.
     * @param rhs Right-hand side color.
     * @return true if both colors have identical R, G, B, and A components.
     * 
     * @par Example
     * @code
     * color c1(255, 0, 0, 255);
     * color c2(255, 0, 0, 255);
     * color c3(0, 0, 0, 255);
     * 
     * bool eq1 = (c1 == c2);  // true
     * bool eq2 = (c1 == c3);  // false
     * @endcode
     */
    friend bool operator==(const color& lhs, const color& rhs) {
        return lhs.r_ == rhs.r_ &&
            lhs.g_ == rhs.g_ &&
            lhs.b_ == rhs.b_ &&
            lhs.a_ == rhs.a_;
    }

    /**
     * @brief Inequality comparison operator.
     * 
     * Compares two colors for inequality. Two colors are not equal if any
     * of their RGBA components differ.
     * 
     * @param lhs Left-hand side color.
     * @param rhs Right-hand side color.
     * @return true if any of the R, G, B, or A components differ.
     * 
     * @par Example
     * @code
     * color c1(255, 0, 0, 255);
     * color c2(255, 0, 0, 255);
     * color c3(0, 0, 0, 255);
     * 
     * bool neq1 = (c1 != c2);  // false
     * bool neq2 = (c1 != c3);  // true
     * @endcode
     */
    friend bool operator!=(const color& lhs, const color& rhs) {
        return !(lhs == rhs);
    }

    // ==================== String Conversion ====================

    /**
     * @brief Implicit conversion to std::string.
     * 
     * Allows automatic conversion of color to std::string in contexts
     * that require a string, such as function parameters or string
     * concatenation.
     * 
     * @return String representation of the color.
     * 
     * @par Example
     * @code
     * color c(255, 0, 0);
     * std::string s = c;  // Uses implicit conversion
     * std::cout << "color: " + c << std::endl;  // Concatenation works
     * @endcode
     */
    operator std::string() const { return to_string(); }

    /**
     * @brief Convert the color to a human-readable string.
     * 
     * Returns a string representation of the color in the format:
     * "color(r=RR, g=GG, b=BB, a=AA)"
     * 
     * @return String representation of the color.
     * 
     * @par Example
     * @code
     * color c(255, 0, 0, 255);
     * std::string s = c.to_string(); // "color(r=255, g=0, b=0, a=255)"
     * @endcode
     */
    std::string to_string() const {
        std::ostringstream oss;
        oss << "color(r=" << static_cast<int>(r_) 
            << ", g=" << static_cast<int>(g_) 
            << ", b=" << static_cast<int>(b_) 
            << ", a=" << static_cast<int>(a_) << ")";
        return oss.str();
    }

    // ==================== Blend Operators ====================

    /**
     * @brief Blend this color with transparent by a percentage.
     * 
     * Similar to LaTeX's color!percentage syntax. Creates a new color
     * that is a blend of this color and transparent.
     * 
     * @param percentage Percentage of transparent to mix (0-100).
     *                   - 0 = original color (fully opaque)
     *                   - 100 = fully transparent
     * 
     * @return A color_blend object that can be further chained with other colors.
     * 
     * @par Example
     * @code
     * color c = "gray"_c | 50;  // 50% gray + 50% transparent
     * @endcode
     * 
     * @note This returns a color_blend to allow chaining with another color.
     *       Use color_blend|color to finalize the blend.
     * 
     * @see operator|(const color&) for direct blending with another color
     */
    color_blend operator|(int percentage) const;

    /**
     * @brief Blend this color with another color (50% each).
     * 
     * Creates a new color that is an equal blend of this color and
     * the specified color.
     * 
     * @param other The color to blend with.
     * @return The blended color (50% this + 50% other).
     * 
     * @par Example
     * @code
     * color c = "gray"_c | "black"_c;  // 50% gray + 50% black
     * @endcode
     */
    color operator|(const color& other) const {
        return blend(other, 50);
    }

    /**
     * @brief Blend this color with another color by a specific percentage.
     * 
     * Linear interpolation between this color and another color.
     * 
     * @param other The color to blend with.
     * @param percentage Percentage of the other color to include (0-100).
     *                   - 0 = this color only
     *                   - 100 = other color only
     * 
     * @return The blended color.
     * 
     * @par Example
     * @code
     * color c = color::red().blend(color::blue(), 25);  // 75% red + 25% blue
     * @endcode
     */
    color blend(const color& other, int percentage) const {
        int pct = std::clamp(percentage, 0, 100);
        double p = pct / 100.0;
        double q = 1.0 - p;

        return color(
            static_cast<uint8_t>(std::ceil(r_ * q + other.r_ * p)),
            static_cast<uint8_t>(std::ceil(g_ * q + other.g_ * p)),
            static_cast<uint8_t>(std::ceil(b_ * q + other.b_ * p)),
            static_cast<uint8_t>(std::ceil(a_ * q + other.a_ * p))
        );
    }

    // ==================== Static Factory Methods ====================

    /**
     * @brief Create a color from a string (auto-detects hex or name).
     * 
     * This method automatically detects whether the input is a hex color
     * code or a color name and delegates to the appropriate parser.
     * 
     * @param str Hex color string or color name.
     * 
     * @return A color instance.
     * 
     * @throws invalid_color_error (a std::invalid_argument) If the format is invalid or 
     *         the color name is not recognized.
     * 
     * @par Supported Hex Formats
     * - \#RRGGBB   (e.g., "#FF0000" = red)
     * - \#RGB      (e.g., "#F00" = red)
     * - \#RRGGBBAA (e.g., "#FF000080" = red with 50% alpha)
     * - \#RGBA     (e.g., "#F008" = red with 50% alpha)
     * 
     * @par Supported color Names
     * All standard CSS color names are supported:
     * black, white, red, green, lime, blue, yellow, cyan, magenta,
     * silver, gray, maroon, olive, purple, teal, navy, orange, pink,
     * brown, transparent
     * 
     * @par Example
     * @code
     * color c1 = color::from_string("#FF0000");  // Red from hex
     * color c2 = color::from_string("blue");    // Blue from name
     * @endcode
     * 
     * @see from_hash()
     * @see from_name()
     */
    static color from_string(const std::string& str) {
        if (str.empty()) {
            throw invalid_color_error("Empty string provided");
        }
        bool is_hex = std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isxdigit(c); });
        return (str[0] == '#') ? from_hash(str) : ( is_hex ? from_hash("#" + str) : from_name(str));
    }

    /**
     * @brief Create a color from RGBA components.
     * 
     * Convenience factory method for creating colors from individual
     * RGBA components.
     * 
     * @param r Red component (0-255).
     * @param g Green component (0-255).
     * @param b Blue component (0-255).
     * @param a Alpha component (0-255). Default is 255 (opaque).
     * 
     * @return A color instance.
     * 
     * @par Example
     * @code
     * color c = color::rgba(255, 0, 0, 255);  // Red, opaque
     * @endcode
     */
    static color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return color(r, g, b, a);
    }

    /**
     * @brief Create a color from a hexadecimal string.
     * 
     * Parses a hex color string in various formats.
     * 
     * @param hex_code Hexadecimal string representing the color.
     * 
     * @return A color instance.
     * 
     * @throws invalid_color_error (a std::invalid_argument) If the hex string format is invalid.
     * 
     * @par Supported Formats
     * | Format    | Example    | Description                          |
     * |-----------|------------|--------------------------------------|
     * | \#RRGGBB   | \#FF0000    | Full hex (red)                      |
     * | \#RGB      | \#F00       | Shorthand (red)                     |
     * | \#RRGGBBAA | \#FF000080  | Full hex with alpha (50% red)       |
     * | \#RGBA     | \#F008      | Shorthand with alpha (50% red)       |
     * 
     * @par Example
     * @code
     * color c1 = color::from_hash("#FF0000");   // Red
     * color c2 = color::from_hash("#F00");      // Red (shorthand)
     * color c3 = color::from_hash("#FF000080"); // 50% alpha red
     * @endcode
     */
    static color from_hash(const std::string& hex_code) {
        if (hex_code.empty() || hex_code[0] != '#') {
            throw invalid_color_error("Hex color string must start with '#'");
        }

        std::string hex_body = hex_code.substr(1);
        
        if (hex_body.length() != 3 && hex_body.length() != 4 &&
            hex_body.length() != 6 && hex_body.length() != 8) {
            throw invalid_color_error("Invalid hex color '" + hex_code +
                                        "': expected 3, 4, 6 or 8 hex digits after '#' (#RGB, #RGBA, #RRGGBB, #RRGGBBAA)");
        }

        // Check every digit up front: std::stoi would throw a bare "stoi" on a leading bad
        // character and silently stop at a trailing one ("1g" parses as 1).
        for (char c : hex_body) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                throw invalid_color_error("Invalid hex color '" + hex_code + "': '" + std::string(1, c) +
                                            "' is not a hex digit (0-9, a-f, A-F)");
            }
        }

        // Expand shorthand: #RGB -> #RRGGBB
        if (hex_body.length() == 3) {
            hex_body = std::string() + hex_body[0] + hex_body[0] 
                              + hex_body[1] + hex_body[1] 
                              + hex_body[2] + hex_body[2];
        }
        // Expand shorthand with alpha: #RGBA -> #RRGGBBAA
        else if (hex_body.length() == 4) {
            hex_body = std::string() + hex_body[0] + hex_body[0] 
                              + hex_body[1] + hex_body[1] 
                              + hex_body[2] + hex_body[2]
                              + hex_body[3] + hex_body[3];
        }

        uint8_t r = static_cast<uint8_t>(std::stoi(hex_body.substr(0, 2), nullptr, 16));
        uint8_t g = static_cast<uint8_t>(std::stoi(hex_body.substr(2, 2), nullptr, 16));
        uint8_t b = static_cast<uint8_t>(std::stoi(hex_body.substr(4, 2), nullptr, 16));
        uint8_t a = (hex_body.length() == 8) 
            ? static_cast<uint8_t>(std::stoi(hex_body.substr(6, 2), nullptr, 16))
            : 255;

        return color(r, g, b, a);
    }

    /**
     * @brief Create a color from a standard CSS color name.
     * 
     * Looks up a color by its name and returns the corresponding color.
     * 
     * @param name The name of the color (case-insensitive).
     * 
     * @return A color instance.
     * 
     * @throws invalid_color_error (a std::invalid_argument) If the color name is not recognized.
     * 
     * @par Supported Colors
     * - Basic: black, white, red, green, blue, yellow, cyan, magenta
     * - Extended: silver, gray, maroon, olive, purple, teal, navy
     * - Additional: lime, orange, pink, brown, transparent
     * 
     * @note Both "gray" and "grey" spellings are supported.
     * 
     * @par Example
     * @code
     * color c1 = color::from_name("red");
     * color c2 = color::from_name("SkyBlue");  // Case insensitive
     * color c3 = color::from_name("grey");     // British spelling
     * @endcode
     */
    static color from_name(const std::string& name) {
        std::string name_lower = name;
        for (char& c : name_lower) c = static_cast<char>(std::tolower(c));

        if (name_lower == "black")       return black();
        if (name_lower == "white")       return white();
        if (name_lower == "red")         return red();
        if (name_lower == "green")       return green();
        if (name_lower == "lime")        return lime();
        if (name_lower == "blue")        return blue();
        if (name_lower == "yellow")      return yellow();
        if (name_lower == "cyan")        return cyan();
        if (name_lower == "magenta")     return magenta();
        if (name_lower == "silver")      return silver();
        if (name_lower == "gray" || name_lower == "grey") return gray();
        if (name_lower == "maroon")      return maroon();
        if (name_lower == "olive")       return olive();
        if (name_lower == "purple")      return purple();
        if (name_lower == "teal")        return teal();
        if (name_lower == "navy")        return navy();
        if (name_lower == "orange")      return orange();
        if (name_lower == "pink")        return pink();
        if (name_lower == "brown")       return brown();
        if (name_lower == "transparent") return transparent();

        throw invalid_color_error("Unknown color name: '" + name + "'");
    }

    /**
     * @brief Create a color from a 32-bit integer value.
     * 
     * Parses a 32-bit integer in the format 0xRRGGBBAA (or 0xAABBGGRR depending
     * on system endianness) and creates a color instance.
     * 
     * @param value 32-bit integer representing the color.
     *              Expected format: 0xRRGGBBAA
     *              - Bits 24-31: Red component
     *              - Bits 16-23: Green component
     *              - Bits 8-15:  Blue component
     *              - Bits 0-7:   Alpha component
     * 
     * @return A color instance with the parsed RGBA components.
     * 
     * @par Example
     * @code
     * color c1 = color::from_value(0xFF0000FF);  // Red, opaque
     * color c2 = color::from_value(0x00000080);  // Black, 50% alpha
     * color c3 = color::from_value(0xFF00FF00);  // Green, opaque
     * @endcode
     * 
     * @note The expected format is 0xRRGGBBAA where:
     *       - RR = Red (bits 24-31)
     *       - GG = Green (bits 16-23)
     *       - BB = Blue (bits 8-15)
     *       - AA = Alpha (bits 0-7)
     */
    static color from_value(uint32_t value) {
        uint8_t r = static_cast<uint8_t>((value >> 24) & 0xFF);
        uint8_t g = static_cast<uint8_t>((value >> 16) & 0xFF);
        uint8_t b = static_cast<uint8_t>((value >> 8) & 0xFF);
        uint8_t a = static_cast<uint8_t>(value & 0xFF);
        return color(r, g, b, a);
    }

    // ==================== Predefined Colors ====================

    /**
     * @brief Returns the color black (0, 0, 0, 255).
     * @return color instance representing black.
     */
    static color black()       { return color(0, 0, 0); }

    /**
     * @brief Returns the color white (255, 255, 255, 255).
     * @return color instance representing white.
     */
    static color white()       { return color(255, 255, 255); }

    /**
     * @brief Returns the color red (255, 0, 0, 255).
     * @return color instance representing red.
     */
    static color red()         { return color(255, 0, 0); }

    /**
     * @brief Returns the color green (0, 128, 0, 255).
     * @return color instance representing green.
     */
    static color green()       { return color(0, 128, 0); }

    /**
     * @brief Returns the color lime (0, 255, 0, 255).
     * @return color instance representing lime.
     */
    static color lime()        { return color(0, 255, 0); }

    /**
     * @brief Returns the color blue (0, 0, 255, 255).
     * @return color instance representing blue.
     */
    static color blue()        { return color(0, 0, 255); }

    /**
     * @brief Returns the color yellow (255, 255, 0, 255).
     * @return color instance representing yellow.
     */
    static color yellow()      { return color(255, 255, 0); }

    /**
     * @brief Returns the color cyan (0, 255, 255, 255).
     * @return color instance representing cyan.
     */
    static color cyan()        { return color(0, 255, 255); }

    /**
     * @brief Returns the color magenta (255, 0, 255, 255).
     * @return color instance representing magenta.
     */
    static color magenta()     { return color(255, 0, 255); }

    /**
     * @brief Returns the color silver (192, 192, 192, 255).
     * @return color instance representing silver.
     */
    static color silver()      { return color(192, 192, 192); }

    /**
     * @brief Returns the color gray (128, 128, 128, 255).
     * @return color instance representing gray.
     */
    static color gray()        { return color(128, 128, 128); }

    /**
     * @brief Returns the color maroon (128, 0, 0, 255).
     * @return color instance representing maroon.
     */
    static color maroon()      { return color(128, 0, 0); }

    /**
     * @brief Returns the color olive (128, 128, 0, 255).
     * @return color instance representing olive.
     */
    static color olive()       { return color(128, 128, 0); }

    /**
     * @brief Returns the color purple (128, 0, 128, 255).
     * @return color instance representing purple.
     */
    static color purple()      { return color(128, 0, 128); }

    /**
     * @brief Returns the color teal (0, 128, 128, 255).
     * @return color instance representing teal.
     */
    static color teal()        { return color(0, 128, 128); }

    /**
     * @brief Returns the color navy (0, 0, 128, 255).
     * @return color instance representing navy.
     */
    static color navy()        { return color(0, 0, 128); }

    /**
     * @brief Returns the color orange (255, 165, 0, 255).
     * @return color instance representing orange.
     */
    static color orange()      { return color(255, 165, 0); }

    /**
     * @brief Returns the color pink (255, 192, 203, 255).
     * @return color instance representing pink.
     */
    static color pink()        { return color(255, 192, 203); }

    /**
     * @brief Returns the color brown (165, 42, 42, 255).
     * @return color instance representing brown.
     */
    static color brown()       { return color(165, 42, 42); }

    /**
     * @brief Returns the color transparent (0, 0, 0, 0).
     * @return color instance representing fully transparent.
     */
    static color transparent() { return color(0, 0, 0, 0); }
};

// ==================== color_blend ====================

/**
 * @class color_blend
 * @brief Lightweight helper class for color blending operations.
 * 
 * This class holds the intermediate state of a color blend operation
 * and allows chaining multiple blends in a fluent manner.
 * 
 * @note This class is primarily used internally by the blend operators.
 *       It supports implicit conversion to color when the blend is finalized.
 * 
 * @par Usage
 * @code
 * // Single blend with transparent (default)
 * color_blend b1 = "gray"_c | 50;
 * color c1 = b1;  // 50% gray + 50% transparent
 * 
 * // Chain with another color
 * color c2 = "gray"_c | 50 | "black"_c;
 * @endcode
 */
struct color_blend {
    color base_color;   ///< The base color being blended
    int percentage;    ///< Percentage for the blend operation (0-100)

    /**
     * @brief Construct a color_blend from a base color and percentage.
     * 
     * @param base The base color to blend.
     * @param percent The percentage of the blend (0-100).
     */
    color_blend(const color& base, int percent)
        : base_color(base), percentage(percent) {}

    /**
     * @brief Finalize the blend with a specific color.
     * 
     * Blends the base color with the specified color using the
     * stored percentage.
     * 
     * @param other The color to blend with.
     * @return The final blended color.
     * 
     * @par Example
     * @code
     * color c = ("gray"_c | 50) | "black"_c;
     * @endcode
     */
    color operator|(const color& other) const {
        return base_color.blend(other, percentage);
    }

    /**
     * @brief Chain multiple blend operations.
     * 
     * Allows chaining like: "gray"_c | 50 | "black"_c
     * 
     * @param other The color_blend to chain with.
     * @return A new color_blend representing the chained operation.
     */
    color_blend operator|(const color_blend& other) const {
        // First blend: base_color | percentage → transparent
        color intermediate = base_color.blend(color::transparent(), percentage);
        // Second blend: intermediate | other.percentage → other.base_color
        return color_blend(intermediate, other.percentage);
    }

    /**
     * @brief Implicit conversion to color.
     * 
     * Finalizes the blend with transparent (default behavior).
     * 
     * @return The final blended color.
     */
    operator color() const {
        return base_color.blend(color::transparent(), percentage);
    }
};

// Documented at its declaration in class color; defined here because color_blend is incomplete there.
inline color_blend color::operator|(int percentage) const {
    return color_blend(*this, percentage);
}

// ==================== Non-Member Operators ====================

/**
 * @brief Stream output operator for color.
 * 
 * Allows using color with std::cout and other output streams.
 * 
 * @param os Output stream (e.g., std::cout).
 * @param c color to output.
 * @return Reference to the output stream.
 * 
 * @par Example
 * @code
 * color c(255, 0, 0);
 * std::cout << c << std::endl;  // Prints: color(r=255, g=0, b=0, a=255)
 * @endcode
 */
inline std::ostream& operator<<(std::ostream& os, const color& c) {
    os << c.to_string();
    return os;
}

/**
 * @brief Stream output operator for color_blend.
 * 
 * Allows using color_blend with output streams. The blend is automatically
 * converted to a color before output.
 * 
 * @param os Output stream.
 * @param blend color_blend to output.
 * @return Reference to the output stream.
 */
inline std::ostream& operator<<(std::ostream& os, const color_blend& blend) {
    os << static_cast<color>(blend);
    return os;
}

} // namespace stb

// ==================== color Literals Namespace ====================

/**
 * @namespace stb::color_literals
 * @brief String literal operators for creating color instances.
 * 
 * This namespace provides the _c suffix operator for creating colors
 * from string literals at compile-time (when possible).
 * 
 * @par Usage
 * @code
 * using namespace stb::color_literals;
 * 
 * color c1 = "red"_c;           // From color name
 * color c2 = "#FF0000"_c;      // From hex
 * color c3 = "gray"_c | 50;    // Blend with transparent
 * color c4 = "gray"_c | 50 | "black"_c;  // Chain blends
 * @endcode
 * 
 * @note Always include "using namespace stb::color_literals;" or
 *       use explicit qualification to use these literals.
 */
namespace stb::color_literals {

/**
 * @brief String literal operator for creating color instances.
 * 
 * This operator allows creating colors using the _c suffix on string literals.
 * It automatically detects whether the string is a hex color code or a
 * color name.
 * 
 * @param str String literal (must include quotes).
 * @param len Length of the string literal.
 * 
 * @return color instance parsed from the string.
 * 
 * @throws invalid_color_error (a std::invalid_argument) If the format is invalid or 
 *         the color name is not recognized.
 * 
 * @par Example
 * @code
 * using namespace stb::color_literals;
 * 
 * color c1 = "red"_c;         // color::red()
 * color c2 = "#FF0000"_c;     // color::from_hash("#FF0000")
 * color c3 = "skyblue"_c;     // color::from_name("skyblue")
 * color c4 = "gray"_c | 50;   // Blend with 50% transparent
 * @endcode
 * 
 * @note The string must be a valid string literal. For runtime strings,
 *       use color::from_string() instead.
 */
inline stb::color operator""_c(const char* str, std::size_t len) {
    return stb::color::from_string(std::string(str, len));
}

} // namespace stb::color_literals

