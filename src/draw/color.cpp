#include "stb/color.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ostream>
#include <sstream>

namespace stb {
namespace {

struct named_color {
    const char* name;
    color (*make)();
};

// CSS names accepted by color::from_name(), all lower case. The values come from the factory
// functions in the header, so they are defined once.
constexpr std::array<named_color, 21> named_colors{{
    {"black", &color::black},
    {"white", &color::white},
    {"red", &color::red},
    {"green", &color::green},
    {"lime", &color::lime},
    {"blue", &color::blue},
    {"yellow", &color::yellow},
    {"cyan", &color::cyan},
    {"magenta", &color::magenta},
    {"silver", &color::silver},
    {"gray", &color::gray},
    {"grey", &color::gray},
    {"maroon", &color::maroon},
    {"olive", &color::olive},
    {"purple", &color::purple},
    {"teal", &color::teal},
    {"navy", &color::navy},
    {"orange", &color::orange},
    {"pink", &color::pink},
    {"brown", &color::brown},
    {"transparent", &color::transparent},
}};

bool is_hex_digit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

uint8_t hex_byte(const std::string& digits, std::size_t pos) {
    return static_cast<uint8_t>(std::stoi(digits.substr(pos, 2), nullptr, 16));
}

}  // namespace

color color::from_string(const std::string& str) {
    if (str.empty()) {
        throw invalid_color_error("Empty string provided");
    }
    if (str[0] == '#') {
        return from_hash(str);
    }
    return std::all_of(str.begin(), str.end(), is_hex_digit) ? from_hash("#" + str) : from_name(str);
}

color color::from_hash(const std::string& hex_code) {
    if (hex_code.empty() || hex_code[0] != '#') {
        throw invalid_color_error("Hex color string must start with '#'");
    }

    std::string hex_body = hex_code.substr(1);
    const std::size_t n = hex_body.length();
    if (n != 3 && n != 4 && n != 6 && n != 8) {
        throw invalid_color_error("Invalid hex color '" + hex_code +
                                  "': expected 3, 4, 6 or 8 hex digits after '#' (#RGB, #RGBA, #RRGGBB, #RRGGBBAA)");
    }

    // Check every digit up front: std::stoi would throw a bare "stoi" on a leading bad
    // character and silently stop at a trailing one ("1g" parses as 1).
    for (char c : hex_body) {
        if (!is_hex_digit(c)) {
            throw invalid_color_error("Invalid hex color '" + hex_code + "': '" + std::string(1, c) +
                                      "' is not a hex digit (0-9, a-f, A-F)");
        }
    }

    // Expand the shorthands: #RGB -> #RRGGBB and #RGBA -> #RRGGBBAA.
    if (n == 3 || n == 4) {
        std::string expanded;
        for (char c : hex_body) {
            expanded += c;
            expanded += c;
        }
        hex_body = expanded;
    }

    const uint8_t alpha = hex_body.length() == 8 ? hex_byte(hex_body, 6) : 255;
    return color(hex_byte(hex_body, 0), hex_byte(hex_body, 2), hex_byte(hex_body, 4), alpha);
}

color color::from_name(const std::string& name) {
    std::string lower = name;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const auto it = std::find_if(named_colors.begin(), named_colors.end(),
                                 [&](const named_color& entry) { return lower == entry.name; });
    if (it == named_colors.end()) {
        throw invalid_color_error("Unknown color name: '" + name + "'");
    }
    return it->make();
}

std::string color::to_string() const {
    std::ostringstream oss;
    oss << "color(r=" << static_cast<int>(r_) << ", g=" << static_cast<int>(g_) << ", b=" << static_cast<int>(b_)
        << ", a=" << static_cast<int>(a_) << ")";
    return oss.str();
}

color color::blend(const color& other, int percentage) const {
    const int pct = std::clamp(percentage, 0, 100);
    const double p = pct / 100.0;
    const double q = 1.0 - p;
    return color(static_cast<uint8_t>(std::ceil(r_ * q + other.r_ * p)),
                 static_cast<uint8_t>(std::ceil(g_ * q + other.g_ * p)),
                 static_cast<uint8_t>(std::ceil(b_ * q + other.b_ * p)),
                 static_cast<uint8_t>(std::ceil(a_ * q + other.a_ * p)));
}

std::ostream& operator<<(std::ostream& os, const color& c) { return os << c.to_string(); }

std::ostream& operator<<(std::ostream& os, const color_blend& blend) { return os << static_cast<color>(blend); }

}  // namespace stb
