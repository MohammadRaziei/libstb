#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
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

stb::font_family parse_family(const std::string& name) {
    if (name == "sans") return stb::font_family::sans;
    if (name == "serif") return stb::font_family::serif;
    if (name == "mono") return stb::font_family::mono;
    throw std::invalid_argument("family must be \"sans\", \"serif\" or \"mono\"");
}

// A text colour: an int (gray), (r, g, b), (r, g, b, a) (a missing alpha is 255), or a string
// stb::color parses ("red", "#ff8800", "#f80", "ff8800").
stb::color text_colour(nb::handle obj) {
    if (nb::isinstance<nb::str>(obj)) return stb::color(nb::cast<std::string>(obj));
    if (PyLong_Check(obj.ptr())) {
        const std::uint8_t v = byte_of(obj);
        return stb::color(v, v, v);
    }
    if (nb::len(nb::cast<nb::sequence>(obj)) == 4) {
        const auto c = colour_of<4>(obj, 4, "color");
        return stb::color(c[0], c[1], c[2], c[3]);
    }
    const auto c = colour_of<3>(obj, 3, "color");
    return stb::color(c[0], c[1], c[2]);
}

// "left", "center" or "right".
stb::text_align text_alignment(const std::string& name) {
    if (name == "left") return stb::text_align::left;
    if (name == "center") return stb::text_align::center;
    if (name == "right") return stb::text_align::right;
    throw std::invalid_argument("align must be \"left\", \"center\" or \"right\", got \"" + name + "\"");
}

// A font argument: the native Font, or the Python Font wrapper that holds one in `_f`.
stb::font font_of(nb::handle h) {
    nb::object o = nb::borrow(h);
    if (nb::hasattr(o, "_f")) o = o.attr("_f");
    if (!nb::isinstance<stb::font>(o)) throw nb::type_error("font must be a libstb.Font");
    return nb::cast<stb::font>(o);
}

// Draws the texts on the canvas's copy without holding the GIL; the Image is made after it returns.
stb::image finish_canvas(stb::canvas& c) {
    nb::gil_scoped_release release;
    return c.finish();
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

    // Font-free estimates (font.py wraps them in NamedTuples)
    m.def("estimate_text",
          [](const std::string& text, float px, const std::string& family) {
              const auto s = stb::estimate_text(text, px, parse_family(family));
              return std::make_tuple(s.width, s.height, s.lines);
          },
          "text"_a, "pixel_height"_a, "family"_a = "sans");
    m.def("estimate_metrics",
          [](float px, const std::string& family) {
              const auto v = stb::estimate_metrics(px, parse_family(family));
              return std::make_tuple(v.ascent, v.descent, v.line_gap);
          },
          "pixel_height"_a, "family"_a = "sans");

    // ---- Canvas: several texts on one copy of an Image (stb::canvas). Same methods as C++. ----
    using stb::canvas;
    using text_item = stb::canvas::text_item;
    constexpr auto keep = nb::rv_policy::reference_internal;  // a text keeps its canvas alive

    nb::class_<text_item>(
        m, "CanvasText",
        "One text on a Canvas, as returned by Canvas.add_text(). Its settings start from the Canvas "
        "defaults of that moment. Each method changes this text only and returns it, so calls "
        "chain; the next text starts again from the Canvas. Every method raises ValueError once "
        "the Canvas was finished.")
        .def(
            "font",
            [](text_item& t, nb::handle f, std::optional<float> px) -> text_item& {
                if (px) t.font(font_of(f), *px);
                else t.font(font_of(f));
                return t;
            },
            "font"_a, "pixel_height"_a = nb::none(), keep,
            "Font for this text. The pixel height stays what it was unless `pixel_height` is given.")
        .def(
            "size", [](text_item& t, float px) -> text_item& { return t.size(px); }, "pixel_height"_a, keep,
            "Pixel height for this text (the font stays the one it already has).")
        .def(
            "color", [](text_item& t, nb::handle c) -> text_item& { return t.color(text_colour(c)); },
            "color"_a, keep,
            "Colour for this text: an int (gray), (r, g, b), (r, g, b, a) or a string such as "
            "\"red\" or \"#ff8800\".")
        .def(
            "align",
            [](text_item& t, const std::string& a) -> text_item& { return t.align(text_alignment(a)); },
            "align"_a, keep,
            "\"left\", \"center\" or \"right\": how this text lines up with its x (see Image.draw_text).")
        .def(
            "bg_color",
            [](text_item& t, nb::handle c) -> text_item& { return t.bg_color(text_colour(c)); },
            "color"_a, keep,
            "Fill this text's layout box (grown by padding() on every side) with `color` before the "
            "text is drawn. Takes the same values as color(); its alpha is honoured. No background by default.")
        .def(
            "padding", [](text_item& t, int px) -> text_item& { return t.padding(px); }, "pixels"_a, keep,
            "Pixels added on every side of the background box (default 0). ValueError if negative.");

    using shape_item = stb::canvas::shape_item;
    nb::class_<shape_item>(
        m, "CanvasShape",
        "One rectangle, ellipse or line on a Canvas, as returned by Canvas.add_rect(), "
        "add_ellipse() and add_line(). Each method changes this shape only and returns it, so calls "
        "chain; the next item starts again from the Canvas. Every method raises ValueError once the "
        "Canvas was finished.")
        .def(
            "fill", [](shape_item& t, nb::handle c) -> shape_item& { return t.fill(text_colour(c)); },
            "color"_a, keep,
            "Fill colour (rectangles and ellipses; they start filled with the Canvas colour). "
            "Takes the same values as CanvasText.color(). ValueError for a line.")
        .def(
            "no_fill", [](shape_item& t) -> shape_item& { return t.no_fill(); }, keep,
            "Do not fill (rectangles and ellipses). ValueError for a line.")
        .def(
            "stroke",
            [](shape_item& t, float width, nb::handle c) -> shape_item& {
                return c.is_none() ? t.stroke(width) : t.stroke(width, text_colour(c));
            },
            "width"_a, "color"_a = nb::none(), keep,
            "Border of `width` pixels drawn inside the box (rectangles and ellipses), in `color` or "
            "the Canvas colour. For a line: its width and colour (it starts with width 1). "
            "ValueError if width <= 0.")
        .def(
            "radius", [](shape_item& t, float px) -> shape_item& { return t.radius(px); }, "pixels"_a, keep,
            "Corner radius in pixels (rectangles only; at most half the short side is used). "
            "ValueError if negative or not a rectangle.");

    nb::class_<canvas>(
        m, "Canvas",
        "Several texts and shapes on ONE copy of an Image.\n\n"
        "Image.draw_text copies the image on every call; a Canvas copies it once, collects the texts "
        "you add and draws them all in finish(). The Image you pass in is never modified. The "
        "set_*() methods set the defaults for what is added after them; add_text() adds a text and "
        "returns it (a CanvasText), so that text's own font, size, colour, alignment and background "
        "can be set; add_rect(), add_ellipse() and add_line() add shapes (a CanvasShape). Every item "
        "starts again from the Canvas::\n\n"
        "    c = stb.Canvas(img)\n"
        "    c.set_font(font, 48).set_color(\"white\")\n\n"
        "    c.add_text(\"Title\", 20, 20)\n"
        "    c.add_text(\"Sub\", 200, 90).align(\"center\").color(\"#ffcc00\").bg_color(\"red\").padding(10)\n"
        "    c.add_text(\"small\", 20, 200).size(24)\n"
        "    c.add_rect(10, 240, 300, 40).fill(\"#222\").radius(8).stroke(2, \"white\")\n"
        "    c.add_line(0, 300, 400, 300).stroke(3, \"red\")\n\n"
        "    out = c.finish()\n\n"
        "An item takes the defaults as they are when it is added: changing a default later does not "
        "touch items that already exist. Items are drawn in the order they were added (a later one "
        "lands on top). Nothing is drawn or checked before finish(), which raises "
        "ValueError for a text without a font, a bad colour or alignment, plus whatever "
        "Image.draw_text raises. It hands the Image over without a copy; the Canvas and all its "
        "texts raise ValueError afterwards. This is the C++ class stb::canvas, with the same "
        "methods and the same pixels.")
        .def(nb::init<const stb::image&>(), "image"_a)
        .def(
            "set_font",
            [](canvas& c, nb::handle f, std::optional<float> px) -> canvas& {
                if (px) c.set_font(font_of(f), *px);
                else c.set_font(font_of(f));
                return c;
            },
            "font"_a, "pixel_height"_a = nb::none(), nb::rv_policy::none,
            "Default font (and `pixel_height`, if given) for the texts added from now on.")
        .def(
            "set_size", [](canvas& c, float px) -> canvas& { return c.set_size(px); }, "pixel_height"_a,
            nb::rv_policy::none, "Default pixel height for the texts added from now on.")
        .def(
            "set_color", [](canvas& c, nb::handle col) -> canvas& { return c.set_color(text_colour(col)); },
            "color"_a, nb::rv_policy::none,
            "Default colour (int, (r, g, b), (r, g, b, a) or a string) for the texts added from now on.")
        .def(
            "set_align",
            [](canvas& c, const std::string& a) -> canvas& { return c.set_align(text_alignment(a)); },
            "align"_a, nb::rv_policy::none,
            "Default \"left\", \"center\" or \"right\" for the texts added from now on.")
        .def(
            "add_text",
            [](canvas& c, const std::string& text, int x, int y) -> text_item& { return c.add_text(text, x, y); },
            "text"_a, "x"_a = 0, "y"_a = 0, keep,
            "Add `text` at (x, y) with the current defaults and return it (a CanvasText).")
        .def(
            "add_rect",
            [](canvas& c, int x, int y, int w, int h) -> shape_item& { return c.add_rect(x, y, w, h); },
            "x"_a, "y"_a, "w"_a, "h"_a, keep,
            "Add a rectangle with the box x, y, w, h, filled with the current colour. Smooth edges, "
            "so a rounded corner (CanvasShape.radius) is not jagged. ValueError if w or h <= 0.")
        .def(
            "add_ellipse",
            [](canvas& c, int x, int y, int w, int h) -> shape_item& { return c.add_ellipse(x, y, w, h); },
            "x"_a, "y"_a, "w"_a, "h"_a, keep,
            "Add an ellipse that fits the box x, y, w, h, filled with the current colour. "
            "ValueError if w or h <= 0.")
        .def(
            "add_line",
            [](canvas& c, int x1, int y1, int x2, int y2) -> shape_item& { return c.add_line(x1, y1, x2, y2); },
            "x1"_a, "y1"_a, "x2"_a, "y2"_a, keep,
            "Add a line of width 1 in the current colour from (x1, y1) to (x2, y2); CanvasShape.stroke() "
            "sets its width and colour. It goes through the centres of the end pixels, so an odd "
            "width stays crisp.")
        .def(
            "finish", [](canvas& c) { return finish_canvas(c); },
            "Draw every text in order and return the Image (no further copy).");

    // Image.draw_text(text, font, pixel_height, x=0, y=0, *, color=0, align="left")
    nb::setattr(
        nb::type<stb::image>(), "draw_text",
        nb::cpp_function(
            [](const stb::image& self, const std::string& text, nb::handle f, float px, int x, int y,
               nb::handle color, const std::string& align) {
                const stb::font fo = font_of(f);
                const stb::color c = text_colour(color);
                const stb::text_align a = text_alignment(align);
                nb::gil_scoped_release release;
                return fo.draw_text(self, text, px, x, y, c, a);
            },
            nb::is_method(), "text"_a, "font"_a, "pixel_height"_a, "x"_a = 0, "y"_a = 0, nb::kw_only(),
            "color"_a = 0, "align"_a = "left",
            "A copy of this Image with `text` drawn on it (multi-line, UTF-8).\n\n"
            "(x, y) is where the text's layout box starts (its top-left corner). With "
            "``align=\"center\"`` or ``\"right\"``, x is instead the horizontal centre or right edge of "
            "every line, so ``img.draw_text(\"a\\nlonger line\", font, 32, 200, 10, align=\"center\")`` "
            "centres both lines on x = 200. Text that hangs over the edges is clipped.\n\n"
            "`color` is an int (gray), (r, g, b), (r, g, b, a) or a string such as \"red\" or "
            "\"#ff8800\" (parsed like stb::color does); the alpha scales the glyph coverage, so a < 255 "
            "gives translucent text. The result has this Image's channel count; on an image with "
            "alpha the text is blended with the usual \"over\" operator.\n\n"
            "Raises ValueError (empty text, bad pixel_height, color or align), LimitError.\n\n"
            ">>> img.draw_text(\"Hello\", font, 32, 10, 10, color=(255, 255, 255))"));
}
