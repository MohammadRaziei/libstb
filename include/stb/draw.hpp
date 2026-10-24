#pragma once

// Drawing on images: shapes (draw_shape) and the canvas that collects texts and shapes on one
// copy of an image.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "stb/color.hpp"
#include "stb/font.hpp"
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

// Several texts and shapes on ONE copy of an image. font::draw_text copies the image on every
// call; a canvas copies it once, collects what you add, and draws it all in finish(). The image
// you pass in is never modified. The Python class libstb.Canvas has exactly the same methods,
// and draws through this class, so both give identical pixels.
//
//   stb::canvas c(photo);
//   c.set_font(f, 48).set_color("white");                          // defaults for what is added next
//
//   c.add_rect(10, 10, 300, 90).fill("#222").radius(12).stroke(2, "white");   // a shape
//   c.add_text("Title", 20, 20);                                   // uses the defaults
//   c.add_text("Sub", mid, 90).align(stb::text_align::center).color("#ffcc00")
//       .font(other).bg_color("red").padding(10);                  // this text only
//   c.add_text("small", 20, 200).size(24);
//   c.add_line(0, 240, 400, 240).stroke(3, "red");
//
//   stb::image out = c.finish();                                   // drawn here, in order
//
// add_text and the add_rect, add_ellipse and add_line shapes return the new item, so its own
// settings can be chained onto it; every item starts again from `c`. An item takes the canvas
// defaults as they are when it is added: changing a default later does not touch items that
// already exist. Items are drawn in the order they were added, so a later one lands on top.
//
// Nothing is drawn (and nothing is checked) before finish(). It throws invalid_argument if a
// text has no font, plus whatever font::draw_text_in_place and draw_shape throw. If it throws,
// the canvas is unusable. A canvas keeps a reference to the font's shared data (fonts are cheap
// to copy), so a font may go out of scope after it was set.
//
// After finish() the canvas is finished: add_*, the set_* calls, an item's setters and a second
// finish() throw invalid_argument. The items stay valid objects until the canvas is destroyed,
// so a reference kept from add_text never dangles. A canvas can be neither copied nor moved
// (its items point back to it).
class canvas {
public:
    // One text on a canvas, as returned by add_text. It belongs to the canvas.
    class text_item {
    public:
        // Font for this text. The pixel height stays what it was; pass it too to change both.
        text_item& font(const stb::font& f) {
            check();
            font_ = f;
            return *this;
        }
        text_item& font(const stb::font& f, float pixel_height) {
            check();
            font_ = f;
            px_ = pixel_height;
            return *this;
        }
        // Pixel height only (the font stays the one this text got from the canvas).
        text_item& size(float pixel_height) {
            check();
            px_ = pixel_height;
            return *this;
        }
        text_item& color(const stb::color& c) {
            check();
            color_ = c;
            return *this;
        }
        // How this text lines up with its x (see text_align).
        text_item& align(text_align a) {
            check();
            align_ = a;
            return *this;
        }
        // Fill the text's layout box (grown by padding() on every side) with `c` before the
        // text is drawn. The alpha of `c` is honoured. No background by default.
        text_item& bg_color(const stb::color& c) {
            check();
            bg_ = c;
            return *this;
        }
        // Pixels added on every side of the layout box for the background. Throws
        // invalid_argument if negative. 0 by default.
        text_item& padding(int pixels) {
            check();
            if (pixels < 0) throw std::invalid_argument("canvas: padding must be >= 0");
            padding_ = pixels;
            return *this;
        }
    private:
        friend class canvas;
        text_item(canvas* owner, std::string utf8, int x, int y, std::optional<stb::font> f, float px,
                  const stb::color& c, text_align a)
            : owner_(owner), text_(std::move(utf8)), x_(x), y_(y), font_(std::move(f)), px_(px), color_(c),
              align_(a) {}
        void check() const { owner_->check(); }

        canvas* owner_;
        std::string text_;
        int x_, y_;
        std::optional<stb::font> font_;
        float px_;
        stb::color color_;
        text_align align_;
        std::optional<stb::color> bg_;
        int padding_ = 0;
    };

    // One rectangle, ellipse or line on a canvas, as returned by add_rect, add_ellipse and
    // add_line. It belongs to the canvas, like a text_item.
    class shape_item {
    public:
        // Fill colour (rect and ellipse). They start filled with the canvas colour.
        shape_item& fill(const stb::color& c) {
            check();
            need_closed("fill");
            s_.fill = c;
            return *this;
        }
        shape_item& no_fill() {
            check();
            need_closed("no_fill");
            s_.fill.reset();
            return *this;
        }
        // Border of `width` pixels, drawn inside the box (rect and ellipse), in the canvas colour
        // or in `c`. For a line: its width and colour (a line starts with width 1 in the canvas
        // colour). Throws invalid_argument if width <= 0.
        shape_item& stroke(float width) { return stroke(width, default_); }
        shape_item& stroke(float width, const stb::color& c) {
            check();
            if (!(width > 0)) throw std::invalid_argument("canvas: stroke width must be > 0");
            s_.stroke_color = c;
            s_.stroke_width = width;
            return *this;
        }
        // Corner radius in pixels (rect only; clamped to half the short side). Throws
        // invalid_argument if negative or if this is not a rect.
        shape_item& radius(float px) {
            check();
            if (s_.type != shape::kind::rect) throw std::invalid_argument("canvas: radius() is for rectangles only");
            if (px < 0) throw std::invalid_argument("canvas: radius must be >= 0");
            s_.radius = px;
            return *this;
        }

    private:
        friend class canvas;
        shape_item(canvas* owner, shape s, const stb::color& c) : owner_(owner), s_(std::move(s)), default_(c) {}
        void check() const { owner_->check(); }
        void need_closed(const char* what) const {
            if (s_.type == shape::kind::line) {
                throw std::invalid_argument(std::string("canvas: a line has no fill (") + what + "); use stroke()");
            }
        }

        canvas* owner_;
        shape s_;
        stb::color default_;
    };

    // Copies `base` once.
    explicit canvas(const image& base) : img_(base.copy()) {}
    canvas(const canvas&) = delete;
    canvas& operator=(const canvas&) = delete;

    // Defaults for the texts added from now on. Default colour is black, alignment left.
    canvas& set_font(const font& f) {
        check();
        font_ = f;
        return *this;
    }
    canvas& set_font(const font& f, float pixel_height) {
        check();
        font_ = f;
        px_ = pixel_height;
        return *this;
    }
    canvas& set_size(float pixel_height) {
        check();
        px_ = pixel_height;
        return *this;
    }
    canvas& set_color(const stb::color& c) {
        check();
        color_ = c;
        return *this;
    }
    canvas& set_align(text_align a) {
        check();
        align_ = a;
        return *this;
    }

    // Adds a text (UTF-8, copied) at (x, y) with the current defaults.
    text_item& add_text(std::string_view utf8, int x, int y) {
        check();
        items_.emplace_back(text_item(this, std::string(utf8), x, y, font_, px_, color_, align_));
        return std::get<text_item>(items_.back());
    }

    // Adds a rectangle / ellipse with the box x, y, w, h (filled with the canvas colour until
    // told otherwise), or a line from (x1, y1) to (x2, y2). Throws invalid_argument if w or h is
    // not positive.
    shape_item& add_rect(int x, int y, int w, int h) {
        return add_shape(shape::make_rect(x, y, w, h), true);
    }
    shape_item& add_ellipse(int x, int y, int w, int h) {
        return add_shape(shape::make_ellipse(x, y, w, h), true);
    }
    shape_item& add_line(int x1, int y1, int x2, int y2) {
        return add_shape(shape::make_line(x1, y1, x2, y2), false);
    }

    bool finished() const { return finished_; }

    // Draws every text in order onto the copy and hands the image over (no further copy).
    image finish() {
        check();
        std::size_t n = 0;  // texts so far, for the message
        for (const item& it : items_) {
            if (const text_item* t = std::get_if<text_item>(&it)) {
                if (!t->font_) {
                    throw std::invalid_argument("canvas: text #" + std::to_string(n) +
                                                " has no font (call set_font() on the canvas or font() on the text)");
                }
                ++n;
            }
        }
        finished_ = true;
        for (const item& it : items_) {
            if (const text_item* t = std::get_if<text_item>(&it)) {
                if (t->bg_) fill_box(*t);
                t->font_->draw_text_in_place(img_, t->text_, t->px_, t->x_, t->y_, t->color_, t->align_);
            } else {
                draw_shape(img_, std::get<shape_item>(it).s_);
            }
        }
        return std::move(img_);
    }

private:
    void check() const {
        if (finished_) throw std::invalid_argument("canvas: already finished");
    }

    shape_item& add_shape(shape s, bool filled) {
        check();
        if (filled) s.fill = color_;
        else s.stroke_color = color_;  // a line: its colour (the width stays 1 until stroke())
        items_.emplace_back(shape_item(this, std::move(s), color_));
        return std::get<shape_item>(items_.back());
    }

    // The text's layout box grown by its padding, clipped to the image, as one solid layer.
    void fill_box(const text_item& t) {
        const text_size s = t.font_->measure(t.text_, t.px_);
        // 64-bit: `long` is 32 bits on Windows, and padding() may be any int.
        const std::int64_t w = std::llround(s.width);
        const std::int64_t h = std::llround(s.height);
        std::int64_t left = t.x_;
        if (t.align_ == text_align::center) left -= std::llround(s.width * 0.5f);
        if (t.align_ == text_align::right) left -= w;
        const std::int64_t pad = t.padding_;
        const std::int64_t x0 = (std::max)(left - pad, std::int64_t(0));
        const std::int64_t y0 = (std::max)(std::int64_t(t.y_) - pad, std::int64_t(0));
        const std::int64_t x1 = (std::min)(left + w + pad, std::int64_t(img_.width()));
        const std::int64_t y1 = (std::min)(std::int64_t(t.y_) + h + pad, std::int64_t(img_.height()));
        if (x1 <= x0 || y1 <= y0) return;  // entirely outside the image
        image layer(int(x1 - x0), int(y1 - y0), 4);
        for (int row = 0; row < layer.height(); ++row) {
            std::uint8_t* d = layer.data() + std::size_t(row) * layer.stride();
            for (int col = 0; col < layer.width(); ++col, d += 4) {
                d[0] = t.bg_->r();
                d[1] = t.bg_->g();
                d[2] = t.bg_->b();
                d[3] = t.bg_->a();
            }
        }
        img_.composite_in_place(layer, int(x0), int(y0));
    }

    image img_;
    using item = std::variant<text_item, shape_item>;
    std::deque<item> items_;  // a deque: the references add_* returns stay valid
    std::optional<font> font_;
    float px_ = 0;
    stb::color color_;
    text_align align_ = text_align::left;
    bool finished_ = false;
};

}  // namespace stb
