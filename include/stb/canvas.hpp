#pragma once

#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "stb/color.hpp"
#include "stb/font.hpp"
#include "stb/image.hpp"

namespace stb {

// Draw several texts on ONE copy of an image. font::draw_text copies the image on every call;
// a canvas copies it once, keeps the current font, size and colour, and blends each text into
// that copy. The image you pass in is never modified.
//
//   stb::canvas c(photo);
//   c.set_font(f, 48).set_color("white"_c);
//   c.draw_text("Title", 20, 20).draw_text("Subtitle", 20, 90, "gray"_c);
//   stb::image out = std::move(c).finish();
//
// A canvas keeps a reference to the font's shared data (fonts are cheap to copy), so the font
// may go out of scope. draw_text throws invalid_argument if no font was set, plus whatever
// font::draw_text throws.
class canvas {
public:
    // Copies `base` once.
    explicit canvas(const image& base) : img_(base.copy()) {}

    canvas& set_font(const font& f, float pixel_height) {
        font_ = f;
        px_ = pixel_height;
        return *this;
    }
    canvas& set_color(const color& c) {
        color_ = c;
        return *this;
    }

    // How the texts that follow line up with their x (see text_align). Default: left.
    canvas& set_align(text_align a) {
        align_ = a;
        return *this;
    }

    // Text with the current colour and alignment, or with `c` (and `a`) for this call only.
    canvas& draw_text(std::string_view utf8, int x, int y) { return draw_text(utf8, x, y, color_, align_); }
    canvas& draw_text(std::string_view utf8, int x, int y, const color& c) {
        return draw_text(utf8, x, y, c, align_);
    }
    canvas& draw_text(std::string_view utf8, int x, int y, const color& c, text_align a) {
        if (!font_) throw std::invalid_argument("canvas: call set_font() before draw_text()");
        font_->draw_text_in_place(img_, utf8, px_, x, y, c, a);
        return *this;
    }

    // What has been drawn so far. Valid until the canvas is changed or finished.
    const image& result() const noexcept { return img_; }
    // Hands the image over (no copy). The canvas is empty afterwards.
    image finish() && { return std::move(img_); }

private:
    image img_;
    std::optional<font> font_;
    float px_ = 0;
    color color_;
    text_align align_ = text_align::left;
};

}  // namespace stb
