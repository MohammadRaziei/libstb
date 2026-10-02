#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <tuple>

#include "binding.hpp"

using namespace nb::literals;

namespace {

// Python passes single characters as 1-char str (UTF-8 over the wire).
char32_t one_char(const std::string& s) {
    const std::u32string cps = stb::utf8_decode(s);
    if (cps.size() != 1) throw std::invalid_argument("expected exactly one character");
    return cps[0];
}

std::string to_utf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += char(cp);
    } else if (cp < 0x800) {
        s += char(0xC0 | (cp >> 6));
        s += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += char(0xE0 | (cp >> 12));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    } else {
        s += char(0xF0 | (cp >> 18));
        s += char(0x80 | ((cp >> 12) & 0x3F));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    }
    return s;
}

}  // namespace

// Native tuples here; the Python package (font.py) wraps them in NamedTuples
// and Image objects.
void bind_font(nb::module_& m) {
    using stb::font;

    nb::class_<font>(m, "Font", "A TrueType/OpenType font. Only load fonts you trust.")
        // Path -> font without the file ever passing through a Python bytes object
        // (that read plus the copy into the C++ buffer is what made open() slow).
        .def_static(
            "open",
            [](const std::filesystem::path& path, int index) { return font::open(path, index); },
            "path"_a, "index"_a = 0, nb::call_guard<nb::gil_scoped_release>())
        .def_static(
            "from_bytes",
            [](nb::bytes data, int index) {
                const auto* p = reinterpret_cast<const std::uint8_t*>(data.c_str());
                return font::from_memory(std::vector<std::uint8_t>(p, p + data.size()), index);
            },
            "data"_a, "index"_a = 0)
        .def("metrics",
             [](const font& f, float px) {
                 const auto v = f.metrics(px);
                 return std::make_tuple(v.ascent, v.descent, v.line_gap);
             },
             "pixel_height"_a)
        .def("has_glyph", [](const font& f, const std::string& ch) { return f.has_glyph(one_char(ch)); },
             "char"_a)
        .def("advance",
             [](const font& f, const std::string& ch, float px) { return f.advance(one_char(ch), px); },
             "char"_a, "pixel_height"_a)
        .def("kerning",
             [](const font& f, const std::string& l, const std::string& r, float px) {
                 return f.kerning(one_char(l), one_char(r), px);
             },
             "left"_a, "right"_a, "pixel_height"_a)
        .def("render_glyph",
             [](const font& f, const std::string& ch, float px) {
                 stb::glyph g = f.render_glyph(one_char(ch), px);
                 nb::object bitmap = nb::none();  // blank glyphs (space) have no bitmap
                 if (!g.bitmap.empty()) bitmap = nb::cast(std::move(g.bitmap));
                 return std::make_tuple(bitmap, g.x_offset, g.y_offset, g.advance);
             },
             "char"_a, "pixel_height"_a)
        .def("measure",
             [](const font& f, const std::string& text, float px) {
                 const auto s = f.measure(text, px);
                 return std::make_tuple(s.width, s.height, s.lines);
             },
             "text"_a, "pixel_height"_a)
        .def("render",
             [](const font& f, const std::string& text, float px, std::size_t max_bytes) {
                 stb::text_bitmap t;
                 {
                     nb::gil_scoped_release release;
                     t = f.render(text, px, max_bytes);
                 }
                 return std::make_tuple(std::move(t.bitmap), t.origin_x, t.origin_y);
             },
             "text"_a, "pixel_height"_a, "max_bytes"_a = std::size_t(1) << 28)
        .def("make_atlas",
             [](const font& f, const std::string& chars, float px, int width, int height, int padding) {
                 stb::atlas a = [&] {
                     const std::u32string cps = stb::utf8_decode(chars);
                     nb::gil_scoped_release release;
                     return f.make_atlas(cps, px, width, height, padding);
                 }();
                 using entry = std::tuple<std::string, int, int, int, int, float, float, float, float, float>;
                 std::vector<entry> glyphs;
                 for (const auto& g : a.glyphs())
                     glyphs.emplace_back(to_utf8(g.codepoint), g.x0, g.y0, g.x1, g.y1, g.xoff, g.yoff,
                                         g.xoff2, g.yoff2, g.advance);
                 stb::image bitmap = a.bitmap().copy();  // explicit copy: the atlas dies with this lambda
                 return std::make_tuple(std::move(bitmap), std::move(glyphs));
             },
             "chars"_a, "pixel_height"_a, "width"_a, "height"_a, "padding"_a = 1);
}
