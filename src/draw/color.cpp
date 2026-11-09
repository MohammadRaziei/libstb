#include "stb/color.hpp"
#include "stb/util.hpp"
#include "draw/css_colors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ostream>
#include <string_view>

namespace stb {
namespace {

// A CSS keyword (all lower case) -> colour, or nullopt. The 148 keywords are in css_colors.hpp.
std::optional<color> keyword(std::string_view lower) {
    if (lower == "transparent") return color(0, 0, 0, 0);
    const auto it = std::lower_bound(detail::css_colors.begin(), detail::css_colors.end(), lower,
                                     [](const detail::css_color& e, std::string_view n) { return std::string_view(e.name) < n; });
    if (it == detail::css_colors.end() || std::string_view(it->name) != lower) return std::nullopt;
    return color(uint8_t(it->rgb >> 16), uint8_t(it->rgb >> 8), uint8_t(it->rgb));
}

bool is_hex_digit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

uint8_t hex_byte(const std::string& digits, std::size_t pos) {
    return static_cast<uint8_t>(std::stoi(digits.substr(pos, 2), nullptr, 16));
}

// ---------------------------------------------------------------- try_parse

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

void skip_space(std::string_view s, std::size_t& i) {
    while (i < s.size() && is_space(s[i])) ++i;
}

// One number of a CSS function: 12, -3.5, .5, 1e2, with a '%' or (hue only) a unit after it.
struct css_number {
    double value = 0;
    bool percent = false;
    std::string unit;  // lower case, empty if none
};

bool read_number(std::string_view s, std::size_t& i, css_number& out, bool allow_unit) {
    std::size_t j = i;
    bool negative = false;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) negative = s[j++] == '-';
    double mantissa = 0;
    int digits = 0, scale = 0;
    for (; j < s.size() && is_digit(s[j]); ++j, ++digits) mantissa = mantissa * 10 + (s[j] - '0');
    if (j < s.size() && s[j] == '.') {
        ++j;
        for (; j < s.size() && is_digit(s[j]); ++j, ++digits, --scale) mantissa = mantissa * 10 + (s[j] - '0');
    }
    if (digits == 0) return false;
    if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {  // an exponent only if digits follow (else "e" starts a unit)
        std::size_t k = j + 1;
        bool exp_negative = false;
        if (k < s.size() && (s[k] == '+' || s[k] == '-')) exp_negative = s[k++] == '-';
        if (k < s.size() && is_digit(s[k])) {
            int e = 0;
            for (; k < s.size() && is_digit(s[k]); ++k) e = std::min(e * 10 + (s[k] - '0'), 400);
            scale += exp_negative ? -e : e;
            j = k;
        }
    }
    double v = mantissa;
    if (scale != 0) v *= std::pow(10.0, scale);  // locale-independent, unlike strtod
    if (!std::isfinite(v)) v = 1e300;
    out = css_number{negative ? -v : v, false, {}};
    if (j < s.size() && s[j] == '%') {
        out.percent = true;
        ++j;
    } else if (j < s.size() && is_alpha(s[j])) {
        if (!allow_unit) return false;
        while (j < s.size() && is_alpha(s[j])) out.unit += lower_ascii(s[j++]);
    }
    i = j;
    return true;
}

struct css_args {
    css_number v[4];
    int count = 0;       // 3 or 4
    bool comma = false;  // the comma style (otherwise white space and '/')
};

// "(" already consumed. Reads the components up to and including ')'.
bool read_args(std::string_view s, std::size_t& i, css_args& a, bool hue_first) {
    skip_space(s, i);
    if (!read_number(s, i, a.v[0], hue_first)) return false;
    a.count = 1;
    std::size_t before = i;
    skip_space(s, i);
    bool separated = i > before;  // white space after the first value (the space form needs it between values)
    a.comma = i < s.size() && s[i] == ',';
    if (a.comma) {
        while (a.count < 4) {
            skip_space(s, i);
            if (i < s.size() && s[i] == ')') break;
            if (i >= s.size() || s[i] != ',') return false;
            ++i;
            skip_space(s, i);
            if (!read_number(s, i, a.v[a.count], false)) return false;
            ++a.count;
        }
    } else {
        while (a.count < 3) {
            if (a.count > 1) {
                before = i;
                skip_space(s, i);
                separated = i > before;
            }
            if (!separated || i >= s.size() || s[i] == ')' || s[i] == '/') return false;  // needs white space, then a value
            if (!read_number(s, i, a.v[a.count], false)) return false;
            ++a.count;
        }
        skip_space(s, i);
        if (i < s.size() && s[i] == '/') {
            ++i;
            skip_space(s, i);
            if (!read_number(s, i, a.v[3], false)) return false;
            a.count = 4;
        }
    }
    skip_space(s, i);
    if (a.count < 3 || i >= s.size() || s[i] != ')') return false;
    ++i;
    return true;
}

uint8_t to_byte(double v) { return uint8_t(std::floor(std::clamp(v, 0.0, 255.0) + 0.5)); }

uint8_t alpha_byte(const css_number& n) { return to_byte((n.percent ? n.value / 100.0 : n.value) * 255.0); }

std::optional<color> rgb_function(const css_args& a) {
    const bool all_percent = a.v[0].percent && a.v[1].percent && a.v[2].percent;
    const bool any_percent = a.v[0].percent || a.v[1].percent || a.v[2].percent;
    if (a.comma && any_percent && !all_percent) return std::nullopt;  // the comma form does not mix
    uint8_t c[3];
    for (int k = 0; k < 3; ++k) c[k] = to_byte(a.v[k].percent ? a.v[k].value * 255.0 / 100.0 : a.v[k].value);  // not * 2.55: 50% must be 127.5
    return color(c[0], c[1], c[2], a.count == 4 ? alpha_byte(a.v[3]) : uint8_t(255));
}

std::optional<color> hsl_function(const css_args& a) {
    if (!a.v[1].percent || !a.v[2].percent || a.v[1].unit.size() || a.v[2].unit.size()) return std::nullopt;
    constexpr double pi = 3.14159265358979323846;
    double h = a.v[0].value;
    if (a.v[0].percent) return std::nullopt;
    const std::string& u = a.v[0].unit;
    if (u == "grad") h *= 0.9;
    else if (u == "rad") h *= 180.0 / pi;
    else if (u == "turn") h *= 360.0;
    else if (!u.empty() && u != "deg") return std::nullopt;
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360.0;
    const double sat = std::clamp(a.v[1].value, 0.0, 100.0) / 100.0;
    const double lig = std::clamp(a.v[2].value, 0.0, 100.0) / 100.0;
    const double chroma = sat * std::min(lig, 1.0 - lig);
    auto channel = [&](double n) {
        const double k = std::fmod(n + h / 30.0, 12.0);
        return (lig - chroma * std::max(-1.0, std::min(std::min(k - 3.0, 9.0 - k), 1.0))) * 255.0;
    };
    return color(to_byte(channel(0)), to_byte(channel(8)), to_byte(channel(4)),
                 a.count == 4 ? alpha_byte(a.v[3]) : uint8_t(255));
}

}  // namespace

color color::from_string(const std::string& str) {
    if (str.empty()) {
        throw invalid_color_error("Empty string provided");
    }
    if (str[0] == '#') {
        return from_hash(str);
    }
    if (str.find('(') != std::string::npos) {  // rgb(), rgba(), hsl(), hsla(): the whole string, white space around it allowed
        std::size_t used = 0;
        const std::optional<color> c = try_parse(str, &used);
        while (c && used < str.size() && is_space(str[used])) ++used;
        if (!c || used != str.size()) throw invalid_color_error("Invalid color '" + str + "'");
        return *c;
    }
    return std::all_of(str.begin(), str.end(), is_hex_digit) ? from_hash("#" + str) : from_name(str);
}

std::optional<color> color::try_parse(std::string_view text, std::size_t* used) {
    std::size_t i = 0;
    skip_space(text, i);
    if (i >= text.size()) return std::nullopt;

    if (text[i] == '#') {  // a hash token: every name character belongs to it, so "#ff00zz" is not "#ff00"
        std::size_t j = i + 1;
        while (j < text.size() && (is_digit(text[j]) || is_alpha(text[j]) || text[j] == '_' || text[j] == '-')) ++j;
        const std::size_t n = j - i - 1;
        if (n != 3 && n != 4 && n != 6 && n != 8) return std::nullopt;
        for (std::size_t k = i + 1; k < j; ++k)
            if (!is_hex_digit(text[k])) return std::nullopt;
        const color c = from_hash(std::string(text.substr(i, j - i)));
        if (used) *used = j;
        return c;
    }
    if (!is_alpha(text[i])) return std::nullopt;

    std::size_t j = i;
    std::string word;  // keywords and function names are short; anything longer is no colour
    while (j < text.size() && (is_alpha(text[j]) || is_digit(text[j]) || text[j] == '-')) {
        if (word.size() >= 32) return std::nullopt;
        word += lower_ascii(text[j++]);
    }
    if (j < text.size() && text[j] == '(') {
        const bool rgb = word == "rgb" || word == "rgba", hsl = word == "hsl" || word == "hsla";
        if (!rgb && !hsl) return std::nullopt;
        css_args args;
        std::size_t k = j + 1;
        if (!read_args(text, k, args, /*hue_first=*/hsl)) return std::nullopt;
        const std::optional<color> c = rgb ? rgb_function(args) : hsl_function(args);
        if (c && used) *used = k;
        return c;
    }
    const std::optional<color> c = keyword(word);
    if (c && used) *used = j;
    return c;
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
    for (char& c : lower) c = lower_ascii(c);
    const std::optional<color> c = keyword(lower);
    if (!c) throw invalid_color_error("Unknown color name: '" + name + "'");
    return *c;
}

std::string color::to_string() const {
    return sformat("color(r=%d, g=%d, b=%d, a=%d)", int(r_), int(g_), int(b_), int(a_));
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
