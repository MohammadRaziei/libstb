#pragma once

#include <optional>
#include <stdexcept>

#include "stb/color.hpp"
#include "stb/image.hpp"

namespace stb {

// A rectangle (optionally with rounded corners), an ellipse or a line, drawn with smooth edges.
// Most code uses these through stb::canvas (add_rect, add_ellipse, add_line); draw_shape is the
// plain function underneath.
//
// Pixel (px, py) is the square [px, px + 1) x [py, py + 1). A rectangle or ellipse with integer
// x, y, w, h therefore covers whole pixels with sharp edges; a rounded corner, an ellipse or a
// slanted line gets partial coverage on its edge pixels, and that coverage scales the alpha of
// the colour. A line goes through the CENTRES of its end pixels, so an odd width (1, 3, ...) stays
// crisp on whole pixel rows and an even width straddles them (width 2 covers half of the row above
// and below). Everything is blended over the image with the usual "over" operator, so translucent
// colours and images with alpha work.
struct shape {
    enum class kind { rect, ellipse, line };

    kind type = kind::rect;
    // rect and ellipse: the box (ax, ay) to (bx, by). line: the two end points, as the centres
    // of their pixels, so a horizontal line of width 1 at y covers exactly row y.
    double ax = 0, ay = 0, bx = 0, by = 0;
    double radius = 0;                  // rect only: corner radius in pixels (clamped to half the short side)
    std::optional<color> fill;          // rect and ellipse; none = not filled
    std::optional<color> stroke_color;  // rect and ellipse: the border, drawn INSIDE the box. line: the line itself
    double stroke_width = 0;            // border width / line width in pixels

    // The box x, y, w, h. Throws invalid_argument if w or h is not positive.
    static shape make_rect(int x, int y, int w, int h) { return box(kind::rect, x, y, w, h); }
    static shape make_ellipse(int x, int y, int w, int h) { return box(kind::ellipse, x, y, w, h); }
    // A line of width 1 and no colour yet.
    static shape make_line(int x1, int y1, int x2, int y2) {
        shape s;
        s.type = kind::line;
        s.ax = x1 + 0.5;
        s.ay = y1 + 0.5;
        s.bx = x2 + 0.5;
        s.by = y2 + 0.5;
        s.stroke_width = 1;
        return s;
    }

private:
    static shape box(kind k, int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) throw std::invalid_argument("shape: width and height must be positive");
        shape s;
        s.type = k;
        s.ax = x;
        s.ay = y;
        s.bx = double(x) + w;
        s.by = double(y) + h;
        return s;
    }
};

// Blends `s` onto `dst` in place (fill first, then the border). Parts outside the image are
// clipped without allocating for them, so a huge shape is cheap. Throws invalid_argument for a
// negative radius, a stroke width <= 0, or a line without a colour.
void draw_shape(image& dst, const shape& s);

}  // namespace stb
