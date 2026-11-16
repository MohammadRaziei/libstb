#include "stb/components.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace stb {
namespace {

// The pieces of the values for which fg(x, y) is true, two touching ones being in one piece when same(p, q) (p and q
// positions as y * width + x). Two passes with a union-find over the provisional labels, whose roots are the smallest
// label of their set; the final numbers follow the order of the roots, which is the order of the first value of each piece.
template <class Fg, class Same>
components label_pieces(int width, int height, connectivity conn, Fg fg, Same same) {
    const std::size_t w = std::size_t(width), h = std::size_t(height), n = w * h;
    std::vector<std::int32_t> lab(n, 0);
    std::vector<std::int32_t> parent(1, 0);  // parent[0] is the background
    const auto find = [&](std::int32_t i) {
        while (parent[std::size_t(i)] != i) {
            parent[std::size_t(i)] = parent[std::size_t(parent[std::size_t(i)])];
            i = parent[std::size_t(i)];
        }
        return i;
    };
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t p = y * w + x;
            if (!fg(p)) continue;
            std::int32_t best = 0;  // the smallest root among the labelled neighbours seen so far
            const auto look = [&](std::size_t q) {
                if (lab[q] == 0 || !same(p, q)) return;
                const std::int32_t r = find(lab[q]);
                if (best == 0) {
                    best = r;
                } else if (r != best) {
                    if (r < best) {
                        parent[std::size_t(best)] = r;
                        best = r;
                    } else {
                        parent[std::size_t(r)] = best;
                    }
                }
            };
            if (x > 0) look(p - 1);
            if (y > 0) {
                look(p - w);
                if (conn == connectivity::eight) {
                    if (x > 0) look(p - w - 1);
                    if (x + 1 < w) look(p - w + 1);
                }
            }
            if (best == 0) {
                best = std::int32_t(parent.size());
                parent.push_back(best);
            }
            lab[p] = best;
        }
    }

    // the final numbers, in the order of the roots
    std::vector<std::int32_t> number(parent.size(), 0);
    std::int32_t count = 0;
    for (std::size_t l = 1; l < parent.size(); ++l)
        number[l] = parent[l] == std::int32_t(l) ? ++count : number[std::size_t(find(std::int32_t(l)))];

    components out;
    out.count = count;
    out.labels = mat32i(width, height, 1);
    out.stats.assign(std::size_t(count), component_stat());
    const std::size_t pieces = std::size_t(count);
    std::vector<double> sum_x(pieces, 0.0);
    std::vector<double> sum_y(pieces, 0.0);
    std::vector<int> right(pieces);
    std::vector<int> bottom(pieces);
    for (std::size_t y = 0; y < h; ++y) {
        std::int32_t* dst = out.labels.row(int(y));
        for (std::size_t x = 0; x < w; ++x) {
            const std::int32_t l = lab[y * w + x];
            if (l == 0) {
                dst[x] = 0;
                continue;
            }
            const std::int32_t id = number[std::size_t(find(l))];
            dst[x] = id;
            const std::size_t i = std::size_t(id - 1);
            component_stat& s = out.stats[i];
            if (s.area == 0) {
                s.x = int(x);
                s.y = int(y);  // rows come in order: the first value is in the topmost row
                right[i] = int(x);
                bottom[i] = int(y);
            } else {
                if (int(x) < s.x) s.x = int(x);
                if (int(x) > right[i]) right[i] = int(x);
                bottom[i] = int(y);
            }
            ++s.area;
            sum_x[i] += double(x);
            sum_y[i] += double(y);
        }
    }
    for (std::size_t i = 0; i < out.stats.size(); ++i) {
        component_stat& s = out.stats[i];
        s.width = right[i] - s.x + 1;
        s.height = bottom[i] - s.y + 1;
        s.cx = float(sum_x[i] / double(s.area));
        s.cy = float(sum_y[i] / double(s.area));
    }
    return out;
}

}  // namespace

components connected_components(const mat8u& mask, connectivity c) {
    if (!mask.empty() && mask.channels() != 1) throw std::invalid_argument("expected a mask of one channel");
    if (mask.empty()) return components();
    const std::size_t w = std::size_t(mask.width());
    const auto at = [&](std::size_t p) { return mask.row(int(p / w))[p % w]; };
    return label_pieces(
        mask.width(), mask.height(), c, [&](std::size_t p) { return at(p) != 0; }, [](std::size_t, std::size_t) { return true; });
}

components connected_components(const mat32i& labels, connectivity c) {
    if (!labels.empty() && labels.channels() != 1) throw std::invalid_argument("expected a map of one channel");
    if (labels.empty()) return components();
    const std::size_t w = std::size_t(labels.width());
    const auto at = [&](std::size_t p) { return labels.row(int(p / w))[p % w]; };
    return label_pieces(
        labels.width(), labels.height(), c, [&](std::size_t p) { return at(p) >= 0; },
        [&](std::size_t p, std::size_t q) { return at(p) == at(q); });
}

}  // namespace stb
