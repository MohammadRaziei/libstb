#include "stb/morphology.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "plane.hpp"
#include "stb/parallel.hpp"

namespace stb {
namespace {

using utils::parallel_for;

constexpr double member_ns = 0.15;  // one member of the element, per value: a floor of what one core does (stb/parallel.hpp)

// An element as the offsets of its members from the anchor.
struct element {
    int rx = 0, ry = 0;  // the reach of the element to each side
    bool full = false;   // every value of the rectangle is a member
    std::vector<std::pair<int, int>> members;  // (dx, dy)
};

element read_element(const mat8u& se) {
    if (se.empty() || se.channels() != 1) throw std::invalid_argument("the structuring element must have one channel");
    if (se.width() % 2 == 0 || se.height() % 2 == 0) throw std::invalid_argument("the structuring element must have an odd width and height");
    element e;
    e.rx = se.width() / 2;
    e.ry = se.height() / 2;
    for (int y = 0; y < se.height(); ++y)
        for (int x = 0; x < se.width(); ++x)
            if (se.row(y)[x] != 0) e.members.emplace_back(x - e.rx, y - e.ry);
    if (e.members.empty()) throw std::invalid_argument("the structuring element has no member");
    e.full = e.members.size() == std::size_t(se.width()) * std::size_t(se.height());
    return e;
}

// The smallest (or largest) value under the members for every position, each channel on its own.
template <class T>
mat<T> morph(const mat<T>& src, const element& e, edge_mode edge, bool take_max) {
    const int w = src.width(), h = src.height(), c = src.channels();
    mat<T> out(w, h, c);
    const std::size_t pw = std::size_t(w) + 2 * std::size_t(e.rx);
    for (int k = 0; k < c; ++k) {
        const std::vector<T> plane = detail_plane::padded_plane(src, k, e.rx, e.ry, edge);
        parallel_for(std::size_t(h), double(w) * double(e.members.size()) * member_ns, [&](std::size_t b, std::size_t en) {
            const std::size_t row_values = std::size_t(w);
            std::vector<T> acc(row_values);
            for (std::size_t y = b; y < en; ++y) {
                bool first = true;
                for (const auto& m : e.members) {
                    const T* line = plane.data() + (y + std::size_t(m.second + e.ry)) * pw + std::size_t(m.first + e.rx);
                    if (first) {
                        std::memcpy(acc.data(), line, std::size_t(w) * sizeof(T));
                        first = false;
                    } else if (take_max) {
                        for (int x = 0; x < w; ++x) acc[std::size_t(x)] = line[x] > acc[std::size_t(x)] ? line[x] : acc[std::size_t(x)];
                    } else {
                        for (int x = 0; x < w; ++x) acc[std::size_t(x)] = line[x] < acc[std::size_t(x)] ? line[x] : acc[std::size_t(x)];
                    }
                }
                T* d = out.row(int(y)) + k;
                for (int x = 0; x < w; ++x) d[std::size_t(x) * std::size_t(c)] = acc[std::size_t(x)];
            }
        });
    }
    return out;
}

// A full rectangle is a row of values and then a column of them: width + height comparisons instead of width * height.
template <class T>
mat<T> apply(const mat<T>& src, const mat8u& se, edge_mode edge, bool take_max) {
    const element e = read_element(se);
    if (src.empty()) return mat<T>();
    if (!e.full || (e.members.size() == 1)) return morph(src, e, edge, take_max);
    element row, column;
    row.rx = e.rx;
    column.ry = e.ry;
    for (int dx = -e.rx; dx <= e.rx; ++dx) row.members.emplace_back(dx, 0);
    for (int dy = -e.ry; dy <= e.ry; ++dy) column.members.emplace_back(0, dy);
    return morph(morph(src, row, edge, take_max), column, edge, take_max);
}

template <class T>
void store(mat<T>& img, const mat<T>& result) {
    std::memcpy(img.data(), result.data(), img.size() * sizeof(T));
}

// a - b where a >= b is expected; never below 0 (at the borders the opening can be a little above the picture)
template <class T>
inline T minus(T a, T b) noexcept {
    return a > b ? T(a - b) : T(0);
}

template <class T>
void subtract_into(mat<T>& img, const mat<T>& a, const mat<T>& b) {
    for (std::size_t i = 0; i < img.size(); ++i) img.data()[i] = minus(a.data()[i], b.data()[i]);
}

}  // namespace

mat8u structuring_element(morph_shape shape, int width, int height) {
    if (height == 0) height = width;
    if (width < 1 || height < 1 || width % 2 == 0 || height % 2 == 0)
        throw std::invalid_argument("the width and height of a structuring element must be odd and positive");
    mat8u se(width, height, 1);
    const int rx = width / 2, ry = height / 2;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int dx = x - rx, dy = y - ry;
            bool member = true;
            switch (shape) {
                case morph_shape::rect: break;
                case morph_shape::cross: member = dx == 0 || dy == 0; break;
                case morph_shape::ellipse: {
                    // the ellipse through the middle of the four sides: (dx / rx)^2 + (dy / ry)^2 <= 1
                    const double a = rx == 0 ? (dx == 0 ? 0.0 : 2.0) : double(dx) / double(rx);
                    const double b = ry == 0 ? (dy == 0 ? 0.0 : 2.0) : double(dy) / double(ry);
                    member = a * a + b * b <= 1.0 + 1e-9;
                    break;
                }
            }
            se.row(y)[x] = std::uint8_t(member ? 255 : 0);
        }
    return se;
}

template <class T>
traits::if_image_t<mat<T>, void> detail::erode_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    const mat<T> r = apply(img, se, edge, false);
    if (!img.empty()) store(img, r);
}

template <class T>
traits::if_image_t<mat<T>, void> detail::dilate_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    const mat<T> r = apply(img, se, edge, true);
    if (!img.empty()) store(img, r);
}

template <class T>
traits::if_image_t<mat<T>, void> detail::morph_gradient_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    const mat<T> d = apply(img, se, edge, true), e = apply(img, se, edge, false);
    if (!img.empty()) subtract_into(img, d, e);
}

template <class T>
traits::if_image_t<mat<T>, void> detail::top_hat_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    const mat<T> opened = apply(apply(img, se, edge, false), se, edge, true);
    if (!img.empty()) {
        const mat<T> original = img.copy();
        subtract_into(img, original, opened);
    }
}

template <class T>
traits::if_image_t<mat<T>, void> detail::black_hat_inplace_mat(mat<T>& img, const mat8u& se, edge_mode edge) {
    const mat<T> closed = apply(apply(img, se, edge, true), se, edge, false);
    if (!img.empty()) {
        const mat<T> original = img.copy();
        subtract_into(img, closed, original);
    }
}

#define STB_MORPH_INSTANTIATE(name)                                                                          \
    template STB_API void name<std::uint8_t>(mat<std::uint8_t>&, const mat8u&, edge_mode);                   \
    template STB_API void name<float>(mat<float>&, const mat8u&, edge_mode);
STB_MORPH_INSTANTIATE(detail::erode_inplace_mat)
STB_MORPH_INSTANTIATE(detail::dilate_inplace_mat)
STB_MORPH_INSTANTIATE(detail::morph_gradient_inplace_mat)
STB_MORPH_INSTANTIATE(detail::top_hat_inplace_mat)
STB_MORPH_INSTANTIATE(detail::black_hat_inplace_mat)
#undef STB_MORPH_INSTANTIATE

}  // namespace stb
