#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/vector.h>

#include <cstring>
#include <optional>
#include <tuple>

#include "binding.hpp"

using namespace nb::literals;

namespace {

constexpr auto keep = nb::rv_policy::reference_internal;  // a method returning `self` hands back the same Python object

stb::fill_rule to_rule(const std::string& n) {
    if (n == "non_zero") return stb::fill_rule::non_zero;
    if (n == "even_odd") return stb::fill_rule::even_odd;
    throw std::invalid_argument("rule must be \"non_zero\" or \"even_odd\", got \"" + n + "\"");
}

stb::line_cap to_cap(const std::string& n) {
    if (n == "butt") return stb::line_cap::butt;
    if (n == "round") return stb::line_cap::round;
    if (n == "square") return stb::line_cap::square;
    throw std::invalid_argument("cap must be \"butt\", \"round\" or \"square\", got \"" + n + "\"");
}

stb::line_join to_join(const std::string& n) {
    if (n == "miter") return stb::line_join::miter;
    if (n == "round") return stb::line_join::round;
    if (n == "bevel") return stb::line_join::bevel;
    throw std::invalid_argument("join must be \"miter\", \"round\" or \"bevel\", got \"" + n + "\"");
}

stb::stroke_style make_style(float width, const std::string& cap, const std::string& join, float miter_limit,
                             const std::optional<std::vector<float>>& dashes, float dash_offset) {
    if (!(width > 0)) throw std::invalid_argument("stroke width must be > 0");
    if (!(miter_limit >= 1.0f)) throw std::invalid_argument("miter_limit must be >= 1");
    stb::stroke_style s;
    s.width = width;
    s.cap = to_cap(cap);
    s.join = to_join(join);
    s.miter_limit = miter_limit;
    if (dashes) s.dashes = *dashes;
    s.dash_offset = dash_offset;
    return s;
}

stb::matrix matrix_or_identity(const std::optional<stb::matrix>& m) { return m ? *m : stb::matrix{}; }

stb::draw_options make_options(const std::optional<stb::matrix>& m, nb::handle blend, float opacity) {
    stb::draw_options o;
    o.transform = matrix_or_identity(m);
    o.blend = to_blend_mode(blend);
    o.opacity = opacity;
    return o;
}

// One command of an outline as a Python tuple ("M", x, y), ("L", x, y), ("Q", cx, cy, x, y),
// ("C", c1x, c1y, c2x, c2y, x, y) or ("Z",).
nb::tuple command_tuple(const stb::outline_command& c) {
    switch (c.op) {
        case stb::outline_op::move_to: return nb::make_tuple("M", c.p[0].x, c.p[0].y);
        case stb::outline_op::line_to: return nb::make_tuple("L", c.p[0].x, c.p[0].y);
        case stb::outline_op::quad_to: return nb::make_tuple("Q", c.p[0].x, c.p[0].y, c.p[1].x, c.p[1].y);
        case stb::outline_op::cubic_to:
            return nb::make_tuple("C", c.p[0].x, c.p[0].y, c.p[1].x, c.p[1].y, c.p[2].x, c.p[2].y);
        case stb::outline_op::close: break;
    }
    return nb::make_tuple("Z");
}

void add_command(stb::outline& p, nb::handle h) {
    nb::sequence seq = nb::cast<nb::sequence>(h);
    const std::size_t n = nb::len(seq);
    if (n == 0) throw std::invalid_argument("an outline command is a tuple like (\"M\", x, y)");
    const std::string op = nb::cast<std::string>(seq[0]);
    auto num = [&](std::size_t i) { return nb::cast<float>(seq[i]); };
    auto need = [&](std::size_t count) {
        if (n != count + 1) {
            throw std::invalid_argument("outline command \"" + op + "\" needs " + std::to_string(count) + " numbers, got " +
                                        std::to_string(n - 1));
        }
    };
    if (op == "M") {
        need(2);
        p.move_to(num(1), num(2));
    } else if (op == "L") {
        need(2);
        p.line_to(num(1), num(2));
    } else if (op == "Q") {
        need(4);
        p.quad_to(num(1), num(2), num(3), num(4));
    } else if (op == "C") {
        need(6);
        p.cubic_to(num(1), num(2), num(3), num(4), num(5), num(6));
    } else if (op == "Z") {
        need(0);
        p.close();
    } else {
        throw std::invalid_argument("unknown outline command \"" + op + "\": use \"M\", \"L\", \"Q\", \"C\" or \"Z\"");
    }
}

// The coverage of an outline as a gray image: 0 where nothing is covered, 255 where everything is.
template <class Rasterize>
stb::image coverage_image(int width, int height, Rasterize&& rasterize) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("width and height must be > 0");
    stb::image mask(width, height, stb::pixel_format::gray);
    mask.fill(stb::color(0, 0, 0));
    rasterize(stb::clip_rect{0, 0, width, height}, [&](stb::span_view v) {
        for (const stb::span& s : v) {
            std::memset(mask.data() + std::size_t(s.y) * mask.stride() + std::size_t(s.x), s.coverage, std::size_t(s.len));
        }
    });
    return mask;
}

}  // namespace

void bind_outline(nb::module_& m) {
    using stb::canvas;
    using stb::matrix;
    using stb::outline;

    // --- Matrix ---------------------------------------------------------------------------------------
    nb::class_<matrix>(
        m, "Matrix",
        "A 2D affine transform in the SVG / canvas convention:\n\n"
        "    x' = a * x + c * y + e\n"
        "    y' = b * x + d * y + f\n\n"
        "The default is the identity. translate(), scale(), rotate() and shear() read like the SVG transform \n"
        "attribute: each applies to a point FIRST, before what the matrix already does, and returns the \n"
        "matrix, so calls chain: ``Matrix().translate(100, 50).rotate(0.3).scale(2)`` scales, then rotates, then \n"
        "moves. This is the C++ class stb::matrix.")
        .def(
            "__init__",
            [](matrix* self, float a, float b, float c, float d, float e, float f) { new (self) matrix{a, b, c, d, e, f}; },
            "a"_a = 1.f, "b"_a = 0.f, "c"_a = 0.f, "d"_a = 1.f, "e"_a = 0.f, "f"_a = 0.f)
        .def_rw("a", &matrix::a)
        .def_rw("b", &matrix::b)
        .def_rw("c", &matrix::c)
        .def_rw("d", &matrix::d)
        .def_rw("e", &matrix::e)
        .def_rw("f", &matrix::f)
        .def_static("translation", &matrix::translation, "tx"_a, "ty"_a, "The matrix that moves by (tx, ty).")
        .def_static(
            "scaling", [](float sx, std::optional<float> sy) { return matrix::scaling(sx, sy.value_or(sx)); }, "sx"_a,
            "sy"_a = nb::none(), "The matrix that scales by sx and sy (sy defaults to sx).")
        .def_static("rotation", &matrix::rotation, "radians"_a,
                    "A rotation turning x towards y (clockwise on a screen whose y grows downwards).")
        .def_static("shearing", &matrix::shearing, "shx"_a, "shy"_a, "x += tan(shx) * y and y += tan(shy) * x.")
        .def("translate", [](matrix& t, float tx, float ty) -> matrix& { return t.translate(tx, ty); }, "tx"_a, "ty"_a,
             keep, "Moves by (tx, ty) before what the matrix already does. Returns the matrix.")
        .def(
            "scale", [](matrix& t, float sx, std::optional<float> sy) -> matrix& { return t.scale(sx, sy.value_or(sx)); },
            "sx"_a, "sy"_a = nb::none(), keep, "Scales (sy defaults to sx) before what the matrix already does.")
        .def("rotate", [](matrix& t, float radians) -> matrix& { return t.rotate(radians); }, "radians"_a, keep,
             "Rotates before what the matrix already does.")
        .def("shear", [](matrix& t, float shx, float shy) -> matrix& { return t.shear(shx, shy); }, "shx"_a, "shy"_a,
             keep, "Shears before what the matrix already does.")
        .def("then", &matrix::then, "next"_a, "A new matrix that applies this one first and `next` second.")
        .def(
            "map", [](const matrix& t, float x, float y) { return std::make_tuple(t.map(x, y).x, t.map(x, y).y); }, "x"_a,
            "y"_a, "The point (x, y) after the transform, as a tuple.")
        .def("inverse", &matrix::inverse, "The inverse matrix, or None if there is none (the determinant is 0).")
        .def_prop_ro("is_identity", &matrix::is_identity)
        .def("__eq__", [](const matrix& x, const matrix& y) { return x == y; }, nb::is_operator())
        .def("__iter__",
             [](const matrix& t) { return nb::iter(nb::make_tuple(t.a, t.b, t.c, t.d, t.e, t.f)); },
             "The six numbers a, b, c, d, e, f, so ``a, b, c, d, e, f = matrix`` works.")
        .def("__repr__", [](const matrix& t) {
            return stb::sformat("Matrix(%g, %g, %g, %g, %g, %g)", double(t.a), double(t.b), double(t.c), double(t.d),
                                double(t.e), double(t.f));
        });

    // --- Outline -----------------------------------------------------------------------------------------
    nb::class_<outline>(
        m, "Outline",
        "A vector shape: any number of contours of straight lines and quadratic or cubic curves, in float \n"
        "coordinates (x to the right, y down, like pixels). Build it with chained calls::\n\n"
        "    p = libstb.Outline().move_to(10, 10).line_to(90, 10).quad_to(120, 50, 90, 90).close()\n"
        "    p.add_rounded_rect(20, 20, 60, 40, 8)                  # whole shapes in one call\n"
        "    p.add_outline(other, libstb.Matrix.translation(200, 0))   # another outline, moved\n\n"
        "move_to starts a contour; line_to, quad_to and cubic_to continue it; close joins its end to its \n"
        "start. Drawing on an empty outline starts at (0, 0), and after close() a new contour begins where \n"
        "the closed one started. Draw an outline with Image.fill_outline(), Image.stroke_outline() or \n"
        "Canvas.add_outline(). This is the C++ class stb::outline.")
        .def(nb::init<>())
        .def(
            "__init__",
            [](outline* self, nb::iterable commands) {
                new (self) outline();
                for (nb::handle h : commands) add_command(*self, h);
            },
            "commands"_a,
            "An outline from the list `commands` returns, e.g. ``Outline([(\"M\", 0, 0), (\"L\", 10, 0), (\"Z\",)])``.")
        .def("move_to", [](outline& p, float x, float y) -> outline& { return p.move_to(x, y); }, "x"_a, "y"_a, keep,
             "Start a new contour at (x, y). Returns the outline.")
        .def("line_to", [](outline& p, float x, float y) -> outline& { return p.line_to(x, y); }, "x"_a, "y"_a, keep,
             "A straight line to (x, y). Returns the outline.")
        .def("quad_to", [](outline& p, float cx, float cy, float x, float y) -> outline& { return p.quad_to(cx, cy, x, y); },
             "cx"_a, "cy"_a, "x"_a, "y"_a, keep, "A quadratic curve with control point (cx, cy) to (x, y). Returns the outline.")
        .def(
            "cubic_to",
            [](outline& p, float c1x, float c1y, float c2x, float c2y, float x, float y) -> outline& {
                return p.cubic_to(c1x, c1y, c2x, c2y, x, y);
            },
            "c1x"_a, "c1y"_a, "c2x"_a, "c2y"_a, "x"_a, "y"_a, keep,
            "A cubic curve with control points (c1x, c1y) and (c2x, c2y) to (x, y). Returns the outline.")
        .def("close", [](outline& p) -> outline& { return p.close(); }, keep,
             "Join the end of the contour to its start. Returns the outline.")
        .def("add_rect", [](outline& p, float x, float y, float w, float h) -> outline& { return p.add_rect(x, y, w, h); },
             "x"_a, "y"_a, "w"_a, "h"_a, keep,
             "Add a closed rectangle (clockwise on screen). Nothing is added if w or h is 0. Returns the outline.")
        .def(
            "add_rounded_rect",
            [](outline& p, float x, float y, float w, float h, float rx, std::optional<float> ry) -> outline& {
                return p.add_rounded_rect(x, y, w, h, rx, ry.value_or(rx));
            },
            "x"_a, "y"_a, "w"_a, "h"_a, "rx"_a, "ry"_a = nb::none(), keep,
            "Add a closed rectangle with rounded corners (ry defaults to rx; radii are limited to half the side).")
        .def("add_ellipse",
             [](outline& p, float cx, float cy, float rx, float ry) -> outline& { return p.add_ellipse(cx, cy, rx, ry); },
             "cx"_a, "cy"_a, "rx"_a, "ry"_a, keep, "Add a closed ellipse. Nothing is added if a radius is 0.")
        .def("add_circle", [](outline& p, float cx, float cy, float r) -> outline& { return p.add_circle(cx, cy, r); },
             "cx"_a, "cy"_a, "r"_a, keep, "Add a closed circle.")
        .def(
            "add_outline",
            [](outline& p, const outline& other, const std::optional<matrix>& t) -> outline& {
                return p.add_outline(other, matrix_or_identity(t));
            },
            "other"_a, "transform"_a = nb::none(), keep, "Append the commands of another outline, optionally moved by a Matrix.")
        .def("transform", [](outline& p, const matrix& t) -> outline& { return p.transform(t); }, "matrix"_a, keep,
             "Move every point through `matrix`, in place. Returns the outline.")
        .def("transformed", &outline::transformed, "matrix"_a, "A copy moved through `matrix`.")
        .def(
            "dashed",
            [](const outline& p, const std::vector<float>& dashes, float offset) { return p.dashed(dashes, offset); },
            "dashes"_a, "offset"_a = 0.f,
            "A copy cut into dashes: alternating \"on\" and \"off\" lengths (an odd count repeats the list once \n"
            "more with on and off swapped). The result has only straight lines, one contour per dash. A pattern \n"
            "with a negative entry or a sum of 0 gives an unchanged copy.")
        .def("clear", &outline::clear, "Remove everything.")
        .def_prop_ro(
            "current_point",
            [](const outline& p) -> std::optional<std::tuple<float, float>> {
                const auto c = p.current_point();
                if (!c) return std::nullopt;
                return std::make_tuple(c->x, c->y);
            },
            "(x, y) where the next line_to would start, or None for an empty outline.")
        .def_prop_ro(
            "bounds",
            [](const outline& p) -> std::optional<std::tuple<float, float, float, float>> {
                const auto b = p.bounds();
                if (!b) return std::nullopt;
                return std::make_tuple(b->x0, b->y0, b->x1, b->y1);
            },
            "(x0, y0, x1, y1): the box around all the points, control points included, or None when empty.")
        .def_prop_ro(
            "commands",
            [](const outline& p) {
                nb::list out;
                for (const stb::outline_command& c : p) out.append(command_tuple(c));
                return out;
            },
            "The commands as tuples: (\"M\", x, y), (\"L\", x, y), (\"Q\", cx, cy, x, y), \n"
            "(\"C\", c1x, c1y, c2x, c2y, x, y) and (\"Z\",). Outline(outline.commands) is an equal outline.")
        .def("__len__", &outline::size)
        .def("__eq__", [](const outline& a, const outline& b) { return a == b; }, nb::is_operator())
        .def("__repr__", [](const outline& p) { return stb::sformat("<libstb.Outline: %zu commands>", p.size()); })
        .def(
            "fill_mask",
            [](const outline& p, int width, int height, const std::string& rule, const std::optional<matrix>& t) {
                const stb::fill_rule r = to_rule(rule);
                const matrix mt = matrix_or_identity(t);
                nb::gil_scoped_release unlock;
                return coverage_image(width, height, [&](const stb::clip_rect& clip, stb::span_sink sink) {
                    stb::rasterize_fill(p, r, mt, clip, sink);
                });
            },
            "width"_a, "height"_a, nb::kw_only(), "rule"_a = "non_zero", "matrix"_a = nb::none(),
            "The coverage of the inside as a gray Image of width x height: 0 outside, 255 inside, in between at \n"
            "the anti-aliased edge. Use it as a mask (Image.with_alpha) for your own colours or gradients.")
        .def(
            "stroke_mask",
            [](const outline& p, int width, int height, float line_width, const std::string& cap, const std::string& join,
               float miter_limit, const std::optional<std::vector<float>>& dashes, float dash_offset,
               const std::optional<matrix>& t) {
                const stb::stroke_style st = make_style(line_width, cap, join, miter_limit, dashes, dash_offset);
                const matrix mt = matrix_or_identity(t);
                nb::gil_scoped_release unlock;
                return coverage_image(width, height, [&](const stb::clip_rect& clip, stb::span_sink sink) {
                    stb::rasterize_stroke(p, st, mt, clip, sink);
                });
            },
            "width"_a, "height"_a, nb::kw_only(), "line_width"_a = 1.f, "cap"_a = "butt", "join"_a = "miter",
            "miter_limit"_a = 4.f, "dashes"_a = nb::none(), "dash_offset"_a = 0.f, "matrix"_a = nb::none(),
            "The coverage of the outline drawn with the given line style, as a gray Image of width x height (see \n"
            "fill_mask and Image.stroke_outline for the arguments).");

    // --- Image.fill_outline / stroke_outline -----------------------------------------------------------------
    nb::setattr(
        nb::type<stb::image>(), "fill_outline",
        nb::cpp_function(
            [](const stb::image& self, const outline& p, nb::handle color, const std::string& rule,
               const std::optional<matrix>& t, nb::handle blend, float opacity) {
                const stb::color c = text_colour(color);
                const stb::fill_rule r = to_rule(rule);
                const stb::draw_options o = make_options(t, blend, opacity);
                nb::gil_scoped_release unlock;
                stb::image out = self.copy();
                stb::fill(out, p, c, r, o);
                return out;
            },
            nb::name("fill_outline"), nb::scope(nb::type<stb::image>()), nb::is_method(), "outline"_a, "color"_a, nb::kw_only(),
            "rule"_a = "non_zero", "matrix"_a = nb::none(), "blend"_a = "normal", "opacity"_a = 1.0f,
            "A copy of this Image with the inside of `outline` filled with `color` (an int, (r, g, b), (r, g, b, a) or a \n"
            "string such as \"red\"), anti-aliased and blended like Image.blend. `rule` is \"non_zero\" or \"even_odd\"; \n"
            "`matrix` (a Matrix) moves the outline to pixel positions; `blend` is a BlendMode or its name; `opacity` \n"
            "(0..1) scales the colour's alpha. Parts outside the image are ignored. ValueError for a bad rule, \n"
            "blend or opacity.\n\n"
            ">>> img.fill_outline(libstb.Outline().add_circle(50, 50, 30), \"#09f\")"));

    nb::setattr(
        nb::type<stb::image>(), "stroke_outline",
        nb::cpp_function(
            [](const stb::image& self, const outline& p, nb::handle color, float width, const std::string& cap,
               const std::string& join, float miter_limit, const std::optional<std::vector<float>>& dashes,
               float dash_offset, const std::optional<matrix>& t, nb::handle blend, float opacity) {
                const stb::color c = text_colour(color);
                const stb::stroke_style st = make_style(width, cap, join, miter_limit, dashes, dash_offset);
                const stb::draw_options o = make_options(t, blend, opacity);
                nb::gil_scoped_release unlock;
                stb::image out = self.copy();
                stb::stroke(out, p, c, st, o);
                return out;
            },
            nb::name("stroke_outline"), nb::scope(nb::type<stb::image>()), nb::is_method(), "outline"_a, "color"_a, "width"_a = 1.f,
            nb::kw_only(), "cap"_a = "butt", "join"_a = "miter", "miter_limit"_a = 4.f, "dashes"_a = nb::none(),
            "dash_offset"_a = 0.f, "matrix"_a = nb::none(), "blend"_a = "normal", "opacity"_a = 1.0f,
            "A copy of this Image with the outline of `outline` drawn in `color`, `width` wide (in outline units; a \n"
            "`matrix` scales it). `cap` is \"butt\", \"round\" or \"square\" (the ends of an open contour); `join` is \n"
            "\"miter\", \"round\" or \"bevel\" (the corners); a miter longer than `miter_limit` * width / 2 becomes a \n"
            "bevel; `dashes` is a list of alternating on and off lengths (an odd count repeats with on and off \n"
            "swapped) and `dash_offset` how far into the pattern to start. The other arguments are those of \n"
            "fill_outline. ValueError for a bad width, cap, join, miter_limit, blend or opacity.\n\n"
            ">>> img.stroke_outline(outline, \"white\", 3, cap=\"round\", dashes=[8, 5])"));

    // --- Canvas.add_outline ---------------------------------------------------------------------------------
    using outline_item = canvas::outline_item;
    nb::class_<outline_item>(
        m, "CanvasOutline",
        "One vector outline on a Canvas, as returned by Canvas.add_outline(). It starts filled with the Canvas colour and \n"
        "without an outline; the inside is drawn first, then the outline on top. Each method changes this outline only \n"
        "and returns it, so calls chain. Every method raises ValueError once the Canvas was finished.")
        .def("fill", [](outline_item& t, nb::handle c) -> outline_item& { return t.fill(text_colour(c)); }, "color"_a, keep,
             "Fill colour (same values as CanvasText.color()).")
        .def("no_fill", [](outline_item& t) -> outline_item& { return t.no_fill(); }, keep, "Do not fill.")
        .def("rule", [](outline_item& t, const std::string& r) -> outline_item& { return t.rule(to_rule(r)); }, "rule"_a, keep,
             "\"non_zero\" (default) or \"even_odd\": how overlapping contours decide what is inside.")
        .def(
            "stroke",
            [](outline_item& t, float width, nb::handle c) -> outline_item& {
                return c.is_none() ? t.stroke(width) : t.stroke(width, text_colour(c));
            },
            "width"_a, "color"_a = nb::none(), keep,
            "An outline of `width` (in outline units), centred on the outline's line, in `color` or the Canvas colour. \n"
            "ValueError if width <= 0.")
        .def("no_stroke", [](outline_item& t) -> outline_item& { return t.no_stroke(); }, keep, "No outline.")
        .def("cap", [](outline_item& t, const std::string& c) -> outline_item& { return t.cap(to_cap(c)); }, "cap"_a, keep,
             "\"butt\", \"round\" or \"square\": the ends of an open contour's outline.")
        .def("join", [](outline_item& t, const std::string& j) -> outline_item& { return t.join(to_join(j)); }, "join"_a, keep,
             "\"miter\", \"round\" or \"bevel\": the corners of the outline.")
        .def("miter_limit", [](outline_item& t, float l) -> outline_item& { return t.miter_limit(l); }, "limit"_a, keep,
             "A miter longer than limit * width / 2 becomes a bevel. ValueError if below 1.")
        .def(
            "dash", [](outline_item& t, std::vector<float> d, float offset) -> outline_item& { return t.dash(std::move(d), offset); },
            "lengths"_a, "offset"_a = 0.f, keep, "Dash the outline: alternating on and off lengths, and the start offset.")
        .def("transform", [](outline_item& t, const matrix& mt) -> outline_item& { return t.transform(mt); }, "matrix"_a, keep,
             "Move the outline to where it is drawn (a Matrix).")
        .def("blend", [](outline_item& t, nb::handle mode) -> outline_item& { return t.blend(to_blend_mode(mode)); }, "mode"_a,
             keep, "How the colours mix: a BlendMode or its name.")
        .def("opacity", [](outline_item& t, float o) -> outline_item& { return t.opacity(o); }, "opacity"_a, keep,
             "Scales the alpha, 0..1. ValueError outside 0..1.");

    nb::class_<canvas>(nb::borrow<nb::class_<canvas>>(m.attr("Canvas")))
        .def(
            "add_outline", [](canvas& c, const outline& p) -> outline_item& { return c.add_outline(p); }, "outline"_a, keep,
            "Add a copy of a Outline, filled with the current colour, and return it (a CanvasOutline): its fill, outline, \n"
            "dashes, matrix, blend mode and opacity can be set.");
}
