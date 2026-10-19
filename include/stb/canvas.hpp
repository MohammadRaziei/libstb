#pragma once

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
//   c.set_font(f, 48).set_color("white").set_align(stb::text_align::left);   // defaults
//
//   c.add_text("Title", 20, 20);                                   // uses the defaults
//   c.add_text("Sub", mid, 90).set_align(stb::text_align::center).set_color("#ffcc00");
//   c.add_text("small", 20, 200).set_size(24);
//
//   stb::image out = std::move(c).finish();                        // drawn here, in order
//
// add_text returns the new text, so its own font, size, colour and alignment can be set. A
// text takes the canvas defaults as they are when it is added: changing a default later
// does not touch texts that already exist. Texts are drawn in the order they were added, so
// a later one lands on top of an earlier one.
//
// Nothing is drawn (and nothing is checked) before finish(). It throws invalid_argument if
// a text has no font, plus whatever font::draw_text_in_place throws (bad size, empty text).
// A canvas keeps a reference to the font's shared data (fonts are cheap to copy), so a font
// may go out of scope after it was set.
class canvas {
public:
    // One text on a canvas, as returned by add_text. It is owned by the canvas: use it right
    // away or keep the reference, but not after finish() (the texts are gone by then).
    class text_item {
    public:
        // Font and pixel height for this text only.
        text_item& set_font(const font& f, float pixel_height) {
            font_ = f;
            px_ = pixel_height;
            return *this;
        }
        // Pixel height only (the font stays the one this text got from the canvas).
        text_item& set_size(float pixel_height) {
            px_ = pixel_height;
            return *this;
        }
        text_item& set_color(const color& c) {
            color_ = c;
            return *this;
        }
        // How this text lines up with its x (see text_align).
        text_item& set_align(text_align a) {
            align_ = a;
            return *this;
        }

    private:
        friend class canvas;
        text_item(std::string utf8, int x, int y, std::optional<font> f, float px, const color& c, text_align a)
            : text_(std::move(utf8)), x_(x), y_(y), font_(std::move(f)), px_(px), color_(c), align_(a) {}

        std::string text_;
        int x_, y_;
        std::optional<font> font_;
        float px_;
        color color_;
        text_align align_;
    };

    // Copies `base` once.
    explicit canvas(const image& base) : img_(base.copy()) {}

    // Defaults for the texts added from now on. Default colour is black, alignment left.
    canvas& set_font(const font& f, float pixel_height) {
        font_ = f;
        px_ = pixel_height;
        return *this;
    }
    canvas& set_color(const color& c) {
        color_ = c;
        return *this;
    }
    canvas& set_align(text_align a) {
        align_ = a;
        return *this;
    }

    // Adds a text (UTF-8, copied) at (x, y) with the current defaults.
    text_item& add_text(std::string_view utf8, int x, int y) {
        items_.push_back(text_item(std::string(utf8), x, y, font_, px_, color_, align_));
        return items_.back();
    }

    // Draws every text in order onto the copy and hands the image over (no further copy).
    // The canvas is empty afterwards.
    image finish() && {
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (!items_[i].font_) {
                throw std::invalid_argument("canvas: text #" + std::to_string(i) +
                                            " has no font (call set_font() on the canvas or on the text)");
            }
        }
        for (const text_item& t : items_) {
            t.font_->draw_text_in_place(img_, t.text_, t.px_, t.x_, t.y_, t.color_, t.align_);
        }
        items_.clear();
        return std::move(img_);
    }

private:
    image img_;
    std::deque<text_item> items_;  // a deque: add_text's references stay valid
    std::optional<font> font_;
    float px_ = 0;
    color color_;
    text_align align_ = text_align::left;
};

}  // namespace stb
