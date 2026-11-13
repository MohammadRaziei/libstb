#include <algorithm>
#include <climits>
#include <cstdint>
#include <stdexcept>

#include "stb/raster.hpp"

namespace stb {

void span_set::append(const span& s) noexcept {
    if (spans_.empty()) {
        x1_ = s.x;
        x2_ = s.x + s.len;
    } else {
        x1_ = STB_MIN(x1_, s.x);
        x2_ = STB_MAX(x2_, s.x + s.len);
    }
    spans_.push_back(s);
}

span_set span_set::from_rect(int x, int y, int w, int h) {
    span_set out;
    if (w <= 0 || h <= 0) return out;
    if (std::int64_t(x) + w > INT_MAX || std::int64_t(y) + h > INT_MAX) throw std::invalid_argument("span_set::from_rect: the rectangle does not fit an int");
    out.spans_.reserve(std::size_t(h));
    for (int i = 0; i < h; ++i) out.append(span{y + i, x, w, 255});
    return out;
}

void span_set::operator()(span_view v) {
    for (const span& s : v) {
        if (s.len < 1 || s.coverage == 0) throw std::invalid_argument("span_set: a span needs len >= 1 and a coverage of 1..255");
        if (std::int64_t(s.x) + s.len > INT_MAX) throw std::invalid_argument("span_set: the end of a span does not fit an int");
        if (!spans_.empty()) {
            const span& last = spans_.back();
            if (s.y < last.y || (s.y == last.y && s.x < last.x + last.len))
                throw std::invalid_argument("span_set: spans must come in order (top to bottom, left to right) without overlap");
        }
        append(s);
    }
}

void span_set::clear() noexcept {
    spans_.clear();
    x1_ = x2_ = 0;
}

std::optional<clip_rect> span_set::extents() const noexcept {
    if (spans_.empty()) return std::nullopt;
    return clip_rect{x1_, spans_.front().y, x2_ - x1_, spans_.back().y - spans_.front().y + 1};
}

bool span_set::contains(int x, int y) const noexcept {
    // the last span that starts at or before (y, x), if it is on this row and reaches x
    auto it = std::upper_bound(spans_.begin(), spans_.end(), span{y, x, 0, 0},
                               [](const span& a, const span& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    if (it == spans_.begin()) return false;
    --it;
    return it->y == y && x - it->x < it->len;
}

span_set span_set::intersect(const span_set& a, const span_set& b) {
    span_set out;
    out.spans_.reserve(STB_MAX(a.spans_.size(), b.spans_.size()));
    std::size_t i = 0, j = 0;
    while (i < a.spans_.size() && j < b.spans_.size()) {
        const span& sa = a.spans_[i];
        const span& sb = b.spans_[j];
        if (sa.y != sb.y) {  // the set that is further up catches up
            if (sa.y < sb.y) ++i;
            else ++j;
            continue;
        }
        const int x = STB_MAX(sa.x, sb.x);
        const int end_a = sa.x + sa.len, end_b = sb.x + sb.len;
        const int end = STB_MIN(end_a, end_b);
        if (x < end) {
            const int coverage = (int(sa.coverage) * int(sb.coverage)) / 255;
            if (coverage > 0) out.append(span{sa.y, x, end - x, std::uint8_t(coverage)});
        }
        // the span that ends first has nothing more to give; at a tie, both
        if (end_a <= end_b) ++i;
        if (end_b <= end_a) ++j;
    }
    return out;
}

}  // namespace stb
