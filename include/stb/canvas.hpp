#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "stb/color.hpp"
#include "stb/font.hpp"
#include "stb/image.hpp"

namespace stb {

// Several texts on ONE copy of an image. font::draw_text copies the image on every call; a
// canvas copies it once, collects the texts you add, and draws them all in finish(). The
// image you pass in is never modified.
//
//   stb::canvas c(photo);
//   c.set_font(f, 48).set_color("white");                          // defaults
//
//   c.add_text("Title", 20, 20);                                   // uses the defaults
//   c.add_text("Sub", mid, 90).align(stb::text_align::center).color("#ffcc00")
//       .font(other).bg_color("red").padding(10);                  // this text only
//   c.add_text("small", 20, 200).size(24);
//
//   stb::image out = std::move(c).finish();                        // drawn here, in order
//
// add_text returns the new text, so its own font, size, colour, alignment and background can
// be set. A text takes the canvas defaults as they are when it is added: changing a default
// later does not touch texts that already exist. Texts are drawn in the order they were
// added, so a later one lands on top of an earlier one.
//
// Nothing is drawn (and nothing is checked) before finish(). It throws invalid_argument if a
// text has no font, plus whatever font::draw_text_in_place throws (bad size, empty text). If
// it throws, the canvas is unusable. A canvas keeps a reference to the font's shared data
// (fonts are cheap to copy), so a font may go out of scope after it was set.
//
// After finish() the canvas is finished: add_text, the set_* calls, a text's setters and a
// second finish() throw invalid_argument. The texts stay valid objects until the canvas is
// destroyed, so a reference kept from add_text never dangles. A canvas can be neither copied
// nor moved (its texts point back to it).
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
        // The canvas this text belongs to (to continue a chain: t.owner().add_text(...)).
        canvas& owner() const { return *owner_; }

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
        items_.push_back(text_item(this, std::string(utf8), x, y, font_, px_, color_, align_));
        return items_.back();
    }

    bool finished() const { return finished_; }

    // Draws every text in order onto the copy and hands the image over (no further copy).
    image finish() && {
        check();
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (!items_[i].font_) {
                throw std::invalid_argument("canvas: text #" + std::to_string(i) +
                                            " has no font (call font() on the canvas or on the text)");
            }
        }
        finished_ = true;
        for (const text_item& t : items_) {
            if (t.bg_) fill_box(t);
            t.font_->draw_text_in_place(img_, t.text_, t.px_, t.x_, t.y_, t.color_, t.align_);
        }
        return std::move(img_);
    }

private:
    void check() const {
        if (finished_) throw std::invalid_argument("canvas: already finished");
    }

    // The text's layout box grown by its padding, clipped to the image, as one solid layer.
    void fill_box(const text_item& t) {
        const text_size s = t.font_->measure(t.text_, t.px_);
        const long w = std::lround(s.width);
        const long h = std::lround(s.height);
        long left = t.x_;
        if (t.align_ == text_align::center) left -= std::lround(s.width * 0.5f);
        if (t.align_ == text_align::right) left -= w;
        const long pad = t.padding_;
        const long x0 = std::max(left - pad, 0L);
        const long y0 = std::max(long(t.y_) - pad, 0L);
        const long x1 = std::min(left + w + pad, long(img_.width()));
        const long y1 = std::min(long(t.y_) + h + pad, long(img_.height()));
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
    std::deque<text_item> items_;  // a deque: add_text's references stay valid
    std::optional<font> font_;
    float px_ = 0;
    stb::color color_;
    text_align align_ = text_align::left;
    bool finished_ = false;
};

}  // namespace stb
