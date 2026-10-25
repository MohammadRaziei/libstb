// Packing glyph bitmaps into one image. The packer is a skyline (bottom-left) rectangle packer,
// written from stb_rect_pack.h v1.01 (public domain / MIT, Sean Barrett and contributors); the
// glyph placement follows stb_truetype.h v1.26. Same layouts as stb.

#include <algorithm>
#include <cstring>
#include <numeric>

#include "font/truetype/truetype.hpp"

namespace stb::truetype {
namespace {

constexpr int unpacked = 0x7fffffff;  // the position of a rectangle that did not fit

struct rect {
    int w = 0, h = 0;
    int x = 0, y = 0;
};

// The "skyline" is a chain of nodes, left to right: node i says that from x = nodes[i].x up to the
// next node's x the used area reaches up to height y. A rectangle goes where the skyline is lowest.
class skyline_packer {
public:
    skyline_packer(int width, int height) : width_(width), height_(height), nodes_(std::size_t(width)) {
        for (std::size_t i = 0; i + 1 < nodes_.size(); ++i) nodes_[i].next = &nodes_[i + 1];
        nodes_.back().next = nullptr;
        free_head_ = &nodes_[0];
        active_head_ = &extra_[0];
        extra_[0] = {0, 0, &extra_[1]};
        extra_[1] = {width, 1 << 30, nullptr};  // the end of the skyline
    }

    // Packs the rectangles, tallest first; rect.x / rect.y become `unpacked` for those that do not
    // fit. Returns whether all fitted.
    bool pack(std::vector<rect>& rects) {
        std::vector<std::size_t> order(rects.size());
        std::iota(order.begin(), order.end(), std::size_t(0));
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (rects[a].h != rects[b].h) return rects[a].h > rects[b].h;
            return rects[a].w > rects[b].w;
        });
        bool all = true;
        for (std::size_t i : order) {
            rect& r = rects[i];
            if (r.w == 0 || r.h == 0) {
                r.x = r.y = 0;  // an empty rectangle needs no space
                continue;
            }
            node** at = nullptr;
            int x = 0, y = 0;
            if (place(r.w, r.h, at, x, y)) {
                r.x = x;
                r.y = y;
            } else {
                r.x = r.y = unpacked;
                all = false;
            }
        }
        return all;
    }

private:
    struct node {
        int x, y;
        node* next;
    };

    // The lowest y a rectangle of `width` can rest at when its left side is at x0, starting from
    // the node `first` that contains x0.
    int lowest_y(node* first, int x0, int width) const {
        node* n = first;
        const int x1 = x0 + width;
        int min_y = 0;
        while (n->x < x1) {
            if (n->y > min_y) min_y = n->y;
            n = n->next;
        }
        return min_y;
    }

    // Finds the bottom-left-most spot; `at` is the link that points to the node where it starts.
    bool find(int width, int height, node**& at, int& best_x, int& best_y) {
        if (width > width_ || height > height_) return false;
        int best = 1 << 30;
        at = nullptr;
        node* n = active_head_;
        node** prev = &active_head_;
        while (n->x + width <= width_) {
            const int y = lowest_y(n, n->x, width);
            if (y < best) {
                best = y;
                at = prev;
            }
            prev = &n->next;
            n = n->next;
        }
        best_x = at ? (*at)->x : 0;
        best_y = best;
        return true;
    }

    bool place(int width, int height, node**& at, int& x, int& y) {
        if (!find(width, height, at, x, y) || !at || y + height > height_ || !free_head_) return false;

        // a new node for the top of the rectangle
        node* top = free_head_;
        top->x = x;
        top->y = y + height;
        free_head_ = top->next;

        // splice it in; nodes the rectangle now covers go back to the free list
        node* cur = *at;
        if (cur->x < x) {
            node* next = cur->next;
            cur->next = top;
            cur = next;
        } else {
            *at = top;
        }
        while (cur->next && cur->next->x <= x + width) {
            node* next = cur->next;
            cur->next = free_head_;
            free_head_ = cur;
            cur = next;
        }
        top->next = cur;
        if (cur->x < x + width) cur->x = x + width;  // the part of the node right of the rectangle remains
        return true;
    }

    int width_, height_;
    std::vector<node> nodes_;
    node extra_[2];
    node* active_head_ = nullptr;
    node* free_head_ = nullptr;
};

}  // namespace

bool pack_glyphs(const face& font, std::uint8_t* pixels, int width, int height, int padding, float pixel_height,
                 const std::vector<int>& codepoints, std::vector<packed_glyph>& out) {
    out.assign(codepoints.size(), packed_glyph{});
    std::memset(pixels, 0, std::size_t(width) * std::size_t(height));
    const float scale = font.scale_for_pixel_height(pixel_height);

    // The size every glyph needs. Missing glyphs all draw the same .notdef, so only the first one
    // gets room.
    std::vector<rect> rects(codepoints.size());
    bool missing_glyph_added = false;
    for (std::size_t k = 0; k < codepoints.size(); ++k) {
        const int glyph = font.glyph_index(codepoints[k]);
        if (glyph == 0 && missing_glyph_added) continue;  // w = h = 0
        const box b = font.bitmap_box(glyph, scale, scale);
        rects[k].w = b.x1 - b.x0 + padding;
        rects[k].h = b.y1 - b.y0 + padding;
        if (glyph == 0) missing_glyph_added = true;
    }

    skyline_packer(width - padding, height - padding).pack(rects);

    bool all_fitted = true;
    int missing_index = -1;
    for (std::size_t k = 0; k < codepoints.size(); ++k) {
        rect& r = rects[k];
        const bool placed = !(r.x == unpacked && r.y == unpacked);
        if (placed && r.w != 0 && r.h != 0) {
            const int glyph = font.glyph_index(codepoints[k]);
            r.x += padding;
            r.y += padding;
            r.w -= padding;
            r.h -= padding;
            int advance = 0, left_bearing = 0;
            font.h_metrics(glyph, advance, left_bearing);
            const box b = font.bitmap_box(glyph, scale, scale);
            font.render_into(glyph, pixels + std::size_t(r.x) + std::size_t(r.y) * std::size_t(width), r.w, r.h, width,
                             scale, scale);
            packed_glyph& g = out[k];
            g.x0 = static_cast<std::int16_t>(r.x);
            g.y0 = static_cast<std::int16_t>(r.y);
            g.x1 = static_cast<std::int16_t>(r.x + r.w);
            g.y1 = static_cast<std::int16_t>(r.y + r.h);
            g.x_advance = scale * advance;
            g.x_offset = static_cast<float>(b.x0);
            g.y_offset = static_cast<float>(b.y0);
            g.x_offset2 = static_cast<float>(b.x0 + r.w);
            g.y_offset2 = static_cast<float>(b.y0 + r.h);
            if (glyph == 0) missing_index = int(k);
        } else if (placed && r.w == 0 && r.h == 0 && missing_index >= 0) {
            out[k] = out[std::size_t(missing_index)];  // a missing glyph after the first
        } else {
            all_fitted = false;
        }
    }
    return all_fitted;
}

}  // namespace stb::truetype
