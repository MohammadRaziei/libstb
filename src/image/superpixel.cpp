#include "stb/superpixel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

#include "image/lab.hpp"
#include "util/parallel.hpp"

// SLIC / SLICO (Achanta et al., TPAMI 2012) and SNIC (Achanta and Susstrunk, CVPR 2017), written for libstb from
// those papers; nothing is taken from any implementation of them. How it differs in shape from the textbook:
//  - the assignment step is a "gather": every pixel looks at the few centres around it and picks the nearest, so
//    pixels are independent and the step runs on all the cores (the textbook scatters from each centre);
//  - the sums for the new centres are fixed-point integers, so adding them up in any order and on any number of
//    threads gives the same numbers, and the result does not depend on the thread count.

namespace stb {
namespace {

using detail::parallel_for;

// Floors of what one core does per pixel (ns), see util/parallel.hpp. Not measured yet.
constexpr double lab_ns = 8.0;
constexpr double assign_ns = 10.0;

// ----------------------------------------------------------------------------------- the grid of starting cells

struct grid {
    int nx = 1, ny = 1;     // cells across and down
    float sx = 1, sy = 1;   // size of a cell in pixels
    float area = 1;         // sx * sy: the spatial scale S^2 of the distance
};

grid make_grid(int w, int h, int count) {
    grid g;
    const double n = double(w) * double(h);
    const double s = std::sqrt(n / double(std::min<double>(double(count), n)));
    g.nx = std::clamp(int(std::lround(double(w) / s)), 1, w);
    g.ny = std::clamp(int(std::lround(double(h) / s)), 1, h);
    g.sx = float(w) / float(g.nx);
    g.sy = float(h) / float(g.ny);
    g.area = g.sx * g.sy;
    return g;
}

// CIELAB of the whole image, 3 floats per pixel.
mat32f to_lab(const mat8u& src) {
    mat32f lab(src.width(), src.height(), 3);
    const detail::lab_table& t = detail::lab_lut();
    parallel_for(std::size_t(src.height()), double(src.width()) * lab_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t y = b; y < e; ++y)
            detail::row_to_lab(t, src.row(int(y)), src.channels(), lab.row(int(y)), std::size_t(src.width()));
    });
    return lab;
}

struct seed {
    int x, y;
};

// The grid cell centres, each moved to the lowest-gradient pixel of its 3x3 neighbourhood (so that a region
// does not start on an edge). Two seeds never share a pixel: a seed that would take a pixel already taken stays
// on its cell centre, and if that is taken too it is dropped.
std::vector<seed> make_seeds(const mat32f& lab, const grid& g) {
    const int w = lab.width(), h = lab.height();
    const auto gradient = [&](int x, int y) {
        const float* l = lab.row(y) + std::size_t(x) * 3;
        const float* xl = lab.row(y) + std::size_t(std::max(x - 1, 0)) * 3;
        const float* xr = lab.row(y) + std::size_t(std::min(x + 1, w - 1)) * 3;
        const float* yu = lab.row(std::max(y - 1, 0)) + std::size_t(x) * 3;
        const float* yd = lab.row(std::min(y + 1, h - 1)) + std::size_t(x) * 3;
        (void)l;
        float s = 0;
        for (int k = 0; k < 3; ++k) {
            const float a = xr[k] - xl[k], b = yd[k] - yu[k];
            s += a * a + b * b;
        }
        return s;
    };
    std::vector<char> taken(std::size_t(w) * std::size_t(h), 0);
    std::vector<seed> seeds;
    seeds.reserve(std::size_t(g.nx) * std::size_t(g.ny));
    for (int j = 0; j < g.ny; ++j) {
        for (int i = 0; i < g.nx; ++i) {
            const int cx = std::clamp(int(std::lround((float(i) + 0.5f) * g.sx - 0.5f)), 0, w - 1);
            const int cy = std::clamp(int(std::lround((float(j) + 0.5f) * g.sy - 0.5f)), 0, h - 1);
            int bx = cx, by = cy;
            float best = gradient(cx, cy);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = cx + dx, y = cy + dy;
                    if (x < 0 || y < 0 || x >= w || y >= h) continue;
                    const float v = gradient(x, y);
                    if (v < best) {
                        best = v;
                        bx = x;
                        by = y;
                    }
                }
            }
            if (taken[std::size_t(by) * std::size_t(w) + std::size_t(bx)]) {
                bx = cx;
                by = cy;
            }
            const std::size_t idx = std::size_t(by) * std::size_t(w) + std::size_t(bx);
            if (taken[idx]) continue;
            taken[idx] = 1;
            seeds.push_back({bx, by});
        }
    }
    return seeds;
}

// ----------------------------------------------------------------------------------- SLIC and SLICO

// 16.16 fixed point: the sums of a region are integers, so the order they are added in does not matter.
constexpr float fixed_one = 65536.0f;
inline std::int64_t to_fixed(float v) { return std::int64_t(std::lround(double(v) * double(fixed_one))); }

struct sums {  // per region: l, a, b in fixed point, x, y, count
    std::vector<std::int64_t> v;
    explicit sums(std::size_t k) : v(k * 6, 0) {}
};

mat32i run_slic(const mat32f& lab, const grid& g, const std::vector<seed>& seeds, const superpixel_options& opt) {
    const int w = lab.width(), h = lab.height();
    const std::size_t K = seeds.size();
    const bool adaptive = opt.method == superpixel_method::slico;

    // the centres
    std::vector<float> cl(K), ca(K), cb(K), cx(K), cy(K);
    for (std::size_t k = 0; k < K; ++k) {
        const float* p = lab.row(seeds[k].y) + std::size_t(seeds[k].x) * 3;
        cl[k] = p[0];
        ca[k] = p[1];
        cb[k] = p[2];
        cx[k] = float(seeds[k].x);
        cy[k] = float(seeds[k].y);
    }
    // 1 / m^2 per region: the same everywhere for slic, learned for slico (its largest colour distance last time)
    std::vector<float> inv_m2(K, 1.0f / (opt.compactness * opt.compactness));
    const float inv_s2 = 1.0f / g.area;

    // which grid cell each column and row is in (a division per pixel is avoided)
    std::vector<int> cell_x(static_cast<std::size_t>(w)), cell_y(static_cast<std::size_t>(h));
    for (int x = 0; x < w; ++x) cell_x[std::size_t(x)] = std::min(int(float(x) / g.sx), g.nx - 1);
    for (int y = 0; y < h; ++y) cell_y[std::size_t(y)] = std::min(int(float(y) / g.sy), g.ny - 1);

    // every pixel starts in its own grid cell. (The seeds are one per cell, in cell order, unless some were dropped:
    // then a label can name a cell whose seed is gone, and that pixel moves to a real region in the first pass.)
    mat32i labels(w, h, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            labels(x, y) = std::int32_t(std::min<std::size_t>(std::size_t(cell_y[std::size_t(y)]) * std::size_t(g.nx) +
                                                                   std::size_t(cell_x[std::size_t(x)]),
                                                               K - 1));

    std::vector<int> bucket_start(std::size_t(g.nx) * std::size_t(g.ny) + 1), bucket_items(K);
    for (int iter = 0; iter < opt.iterations; ++iter) {
        // centres by the grid cell they are in now, so a pixel finds the ones within a cell of itself
        std::fill(bucket_start.begin(), bucket_start.end(), 0);
        const auto bucket_of = [&](std::size_t k) {
            const int bx = std::clamp(int(cx[k] / g.sx), 0, g.nx - 1), by = std::clamp(int(cy[k] / g.sy), 0, g.ny - 1);
            return std::size_t(by) * std::size_t(g.nx) + std::size_t(bx);
        };
        for (std::size_t k = 0; k < K; ++k) ++bucket_start[bucket_of(k) + 1];
        for (std::size_t i = 1; i < bucket_start.size(); ++i) bucket_start[i] += bucket_start[i - 1];
        {
            std::vector<int> fill(bucket_start.begin(), bucket_start.end() - 1);
            for (std::size_t k = 0; k < K; ++k) bucket_items[std::size_t(fill[bucket_of(k)]++)] = int(k);
        }

        sums total(K);
        std::vector<float> max_dc2(K, 0.0f);
        long long changed = 0;
        std::mutex merge;
        parallel_for(std::size_t(h), double(w) * assign_ns, [&](std::size_t y0, std::size_t y1) {
            sums mine(K);
            std::vector<float> my_max(K, 0.0f);
            long long my_changed = 0;
            for (std::size_t yy = y0; yy < y1; ++yy) {
                const int y = int(yy);
                const float* lrow = lab.row(y);
                std::int32_t* lab_out = labels.row(y);
                const int by0 = std::max(cell_y[yy] - 1, 0), by1 = std::min(cell_y[yy] + 1, g.ny - 1);
                for (int x = 0; x < w; ++x) {
                    const float l = lrow[x * 3], a = lrow[x * 3 + 1], b = lrow[x * 3 + 2];
                    const int bx0 = std::max(cell_x[std::size_t(x)] - 1, 0), bx1 = std::min(cell_x[std::size_t(x)] + 1, g.nx - 1);
                    float best = 3.0e38f, best_dc2 = 0.0f;
                    int best_k = -1;
                    for (int by = by0; by <= by1; ++by) {
                        for (int bx = bx0; bx <= bx1; ++bx) {
                            const std::size_t bk = std::size_t(by) * std::size_t(g.nx) + std::size_t(bx);
                            for (int i = bucket_start[bk]; i < bucket_start[bk + 1]; ++i) {
                                const std::size_t k = std::size_t(bucket_items[std::size_t(i)]);
                                const float dx = cx[k] - float(x), dy = cy[k] - float(y);
                                if (std::fabs(dx) > g.sx || std::fabs(dy) > g.sy) continue;  // outside its 2S x 2S window
                                const float dl = cl[k] - l, da = ca[k] - a, db = cb[k] - b;
                                const float dc2 = dl * dl + da * da + db * db;
                                const float d = dc2 * inv_m2[k] + (dx * dx + dy * dy) * inv_s2;
                                if (d < best) {
                                    best = d;
                                    best_k = int(k);
                                    best_dc2 = dc2;
                                }
                            }
                        }
                    }
                    if (best_k >= 0) {
                        if (lab_out[x] != best_k) ++my_changed;
                        lab_out[x] = best_k;
                        if (best_dc2 > my_max[std::size_t(best_k)]) my_max[std::size_t(best_k)] = best_dc2;
                    }
                    std::int64_t* s = &mine.v[std::size_t(lab_out[x]) * 6];
                    s[0] += to_fixed(l);
                    s[1] += to_fixed(a);
                    s[2] += to_fixed(b);
                    s[3] += x;
                    s[4] += y;
                    s[5] += 1;
                }
            }
            const std::lock_guard<std::mutex> lock(merge);
            for (std::size_t i = 0; i < total.v.size(); ++i) total.v[i] += mine.v[i];
            for (std::size_t k = 0; k < K; ++k) max_dc2[k] = std::max(max_dc2[k], my_max[k]);
            changed += my_changed;
        });

        for (std::size_t k = 0; k < K; ++k) {
            const std::int64_t* s = &total.v[k * 6];
            if (s[5] > 0) {
                const double n = double(s[5]);
                cl[k] = float(double(s[0]) / (double(fixed_one) * n));
                ca[k] = float(double(s[1]) / (double(fixed_one) * n));
                cb[k] = float(double(s[2]) / (double(fixed_one) * n));
                cx[k] = float(double(s[3]) / n);
                cy[k] = float(double(s[4]) / n);
            }
            if (adaptive && max_dc2[k] > 0.0f) inv_m2[k] = 1.0f / std::max(max_dc2[k], 1.0f);
        }
        if (changed == 0 && iter > 0) break;
    }
    return labels;
}

// ----------------------------------------------------------------------------------- SNIC

mat32i run_snic(const mat32f& lab, const grid& g, const std::vector<seed>& seeds, const superpixel_options& opt) {
    const int w = lab.width(), h = lab.height();
    const std::size_t N = std::size_t(w) * std::size_t(h);
    const std::size_t K = seeds.size();
    const float inv_m2 = 1.0f / (opt.compactness * opt.compactness);
    const float inv_s2 = 1.0f / g.area;

    struct sum {
        double l = 0, a = 0, b = 0, x = 0, y = 0, n = 0;
    };
    std::vector<sum> acc(K);

    struct entry {
        float d;
        std::uint32_t seq;
        std::uint32_t pixel;
        std::int32_t k;
    };
    // smallest distance first, the earlier push first among equals: the order is fully determined
    const auto later = [](const entry& p, const entry& q) { return p.d != q.d ? p.d > q.d : p.seq > q.seq; };
    std::priority_queue<entry, std::vector<entry>, decltype(later)> heap(later);

    mat32i labels(w, h, 1);
    std::int32_t* L = labels.data();
    std::fill(L, L + N, std::int32_t(-1));
    const float* lp = lab.data();
    std::uint32_t seq = 0;
    for (std::size_t k = 0; k < K; ++k)
        heap.push({0.0f, seq++, std::uint32_t(std::size_t(seeds[k].y) * std::size_t(w) + std::size_t(seeds[k].x)), std::int32_t(k)});

    while (!heap.empty()) {
        const entry e = heap.top();
        heap.pop();
        if (L[e.pixel] >= 0) continue;
        L[e.pixel] = e.k;
        const int x = int(e.pixel % std::uint32_t(w)), y = int(e.pixel / std::uint32_t(w));
        sum& s = acc[std::size_t(e.k)];
        const float* p = lp + std::size_t(e.pixel) * 3;
        s.l += double(p[0]);
        s.a += double(p[1]);
        s.b += double(p[2]);
        s.x += x;
        s.y += y;
        s.n += 1.0;
        const float ml = float(s.l / s.n), ma = float(s.a / s.n), mb = float(s.b / s.n);
        const float mx = float(s.x / s.n), my = float(s.y / s.n);
        const auto visit = [&](int nx, int ny) {
            const std::uint32_t q = std::uint32_t(std::size_t(ny) * std::size_t(w) + std::size_t(nx));
            if (L[q] >= 0) return;
            const float* c = lp + std::size_t(q) * 3;
            const float dl = c[0] - ml, da = c[1] - ma, db = c[2] - mb;
            const float dx = float(nx) - mx, dy = float(ny) - my;
            heap.push({(dl * dl + da * da + db * db) * inv_m2 + (dx * dx + dy * dy) * inv_s2, seq++, q, e.k});
        };
        if (x > 0) visit(x - 1, y);
        if (x + 1 < w) visit(x + 1, y);
        if (y > 0) visit(x, y - 1);
        if (y + 1 < h) visit(x, y + 1);
    }
    return labels;
}

// ----------------------------------------------------------------------------------- finishing

// Names the regions 0, 1, 2, ... in the order a raster scan first meets them (so empty labels vanish).
int relabel(mat32i& labels, std::size_t label_space) {
    std::vector<std::int32_t> remap(label_space, -1);
    std::int32_t next = 0;
    for (std::int32_t& v : labels) {
        std::int32_t& m = remap[std::size_t(v)];
        if (m < 0) m = next++;
        v = m;
    }
    return int(next);
}

// Makes every region one 4-connected piece: the connected pieces that are smaller than min_size are merged into the
// neighbouring piece they share the longest border with (smallest first). Returns the number of regions.
int enforce_connectivity(mat32i& labels, std::size_t min_size) {
    const std::size_t N = labels.size(), W = std::size_t(labels.width());
    std::int32_t* L = labels.data();

    // the connected pieces
    std::vector<std::int32_t> piece(N, -1);
    std::vector<std::size_t> size_of;
    std::vector<std::size_t> stack;
    for (std::size_t s = 0; s < N; ++s) {
        if (piece[s] >= 0) continue;
        const std::int32_t id = std::int32_t(size_of.size());
        size_of.push_back(0);
        piece[s] = id;
        stack.push_back(s);
        while (!stack.empty()) {
            const std::size_t p = stack.back();
            stack.pop_back();
            ++size_of[std::size_t(id)];
            const std::size_t x = p % W;
            const auto take = [&](std::size_t q) {
                if (piece[q] < 0 && L[q] == L[p]) {
                    piece[q] = id;
                    stack.push_back(q);
                }
            };
            if (x > 0) take(p - 1);
            if (x + 1 < W) take(p + 1);
            if (p >= W) take(p - W);
            if (p + W < N) take(p + W);
        }
    }

    const std::size_t pieces = size_of.size();
    std::vector<std::int32_t> parent(pieces);
    std::iota(parent.begin(), parent.end(), 0);
    const auto find = [&](std::int32_t i) {
        while (parent[std::size_t(i)] != i) {
            parent[std::size_t(i)] = parent[std::size_t(parent[std::size_t(i)])];
            i = parent[std::size_t(i)];
        }
        return i;
    };
    std::vector<char> small(pieces, 0);
    std::vector<std::int32_t> small_ids;
    for (std::size_t i = 0; i < pieces; ++i)
        if (size_of[i] < min_size) {
            small[i] = 1;
            small_ids.push_back(std::int32_t(i));
        }

    if (!small_ids.empty()) {
        // one entry per border pixel pair that touches a small piece: (small piece, the piece next to it)
        std::vector<std::pair<std::int32_t, std::int32_t>> border;
        const auto pair_at = [&](std::size_t p, std::size_t q) {
            const std::int32_t a = piece[p], b = piece[q];
            if (a == b) return;
            if (small[std::size_t(a)]) border.emplace_back(a, b);
            if (small[std::size_t(b)]) border.emplace_back(b, a);
        };
        for (std::size_t p = 0; p < N; ++p) {
            if (p % W + 1 < W) pair_at(p, p + 1);
            if (p + W < N) pair_at(p, p + W);
        }
        std::sort(border.begin(), border.end());
        std::sort(small_ids.begin(), small_ids.end(), [&](std::int32_t a, std::int32_t b) {
            return size_of[std::size_t(a)] != size_of[std::size_t(b)] ? size_of[std::size_t(a)] < size_of[std::size_t(b)] : a < b;
        });
        std::vector<std::pair<std::int32_t, std::size_t>> votes;  // (neighbouring piece, shared border length)
        for (const std::int32_t s : small_ids) {
            if (find(s) != s || size_of[std::size_t(s)] >= min_size) continue;  // already merged, or grown enough
            votes.clear();
            const auto lo = std::lower_bound(border.begin(), border.end(), std::make_pair(s, std::int32_t(-1)));
            for (auto it = lo; it != border.end() && it->first == s; ++it) {
                const std::int32_t r = find(it->second);
                if (r == s) continue;
                const auto v = std::find_if(votes.begin(), votes.end(), [&](const auto& e) { return e.first == r; });
                if (v == votes.end()) votes.emplace_back(r, 1);
                else ++v->second;
            }
            if (votes.empty()) continue;
            const auto best = std::max_element(votes.begin(), votes.end(), [](const auto& a, const auto& b) {
                return a.second != b.second ? a.second < b.second : a.first > b.first;
            });
            parent[std::size_t(s)] = best->first;
            size_of[std::size_t(best->first)] += size_of[std::size_t(s)];
        }
    }

    // the final names: the pieces in raster order of first appearance
    std::vector<std::int32_t> name(pieces, -1);
    std::int32_t next = 0;
    for (std::size_t p = 0; p < N; ++p) {
        std::int32_t& m = name[std::size_t(find(piece[p]))];
        if (m < 0) m = next++;
        L[p] = m;
    }
    return int(next);
}

}  // namespace

// ----------------------------------------------------------------------------------- the map

superpixel_map::superpixel_map(mat32i labels, int count) : labels_(std::move(labels)), count_(count) {
    if (!labels_.empty() && labels_.channels() != 1) throw std::invalid_argument("labels must have one channel");
    if (count < 1) throw std::invalid_argument("count must be >= 1");
    for (const std::int32_t v : labels_)
        if (v < 0 || v >= count) throw std::invalid_argument("every label must be in [0, count)");
}

superpixel_map superpixel_map::copy() const {
    superpixel_map m;
    m.labels_ = labels_.copy();
    m.count_ = count_;
    return m;
}

image8u superpixel_map::boundaries(const color& c) const {
    if (empty()) return image8u();
    const int w = width(), h = height();
    image8u out(w, h, pixel_format::rgba);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::int32_t l = labels_(x, y);
            if ((x + 1 < w && labels_(x + 1, y) != l) || (y + 1 < h && labels_(x, y + 1) != l)) {
                out(x, y, 0) = c.r();
                out(x, y, 1) = c.g();
                out(x, y, 2) = c.b();
                out(x, y, 3) = c.a();
            }
        }
    }
    return out;
}

image8u superpixel_map::overlay(const mat8u& src, const color& c) const {
    if (src.width() != width() || src.height() != height())
        throw std::invalid_argument("src must have the size of the label map");
    if (empty()) return image8u();
    const int ch = src.channels();
    if (ch > 4) throw std::invalid_argument("src must have at most 4 channels");
    const bool alpha = ch == 2 || ch == 4;
    image8u out(width(), height(), alpha ? pixel_format::rgba : pixel_format::rgb);
    const int oc = out.channels();
    for (int y = 0; y < height(); ++y) {
        for (int x = 0; x < width(); ++x) {
            const std::uint8_t* s = &src(x, y);
            std::uint8_t* d = &out(x, y);
            if (ch < 3) {
                d[0] = d[1] = d[2] = s[0];
                if (alpha) d[3] = s[1];
            } else {
                for (int k = 0; k < ch; ++k) d[k] = s[k];
            }
            const std::int32_t l = labels_(x, y);
            if ((x + 1 < width() && labels_(x + 1, y) != l) || (y + 1 < height() && labels_(x, y + 1) != l)) {
                d[0] = c.r();
                d[1] = c.g();
                d[2] = c.b();
            }
            (void)oc;
        }
    }
    return out;
}

image8u superpixel_map::mean_color(const mat8u& src) const {
    if (src.width() != width() || src.height() != height())
        throw std::invalid_argument("src must have the size of the label map");
    if (src.channels() > 4) throw std::invalid_argument("src must have at most 4 channels");
    if (empty()) return image8u();
    const std::size_t ch = std::size_t(src.channels());
    std::vector<std::uint64_t> sum(std::size_t(count_) * ch, 0), n(std::size_t(count_), 0);
    const std::size_t N = labels_.size();
    const std::int32_t* L = labels_.data();
    const std::uint8_t* s = src.data();
    for (std::size_t p = 0; p < N; ++p) {
        const std::size_t l = std::size_t(L[p]);
        ++n[l];
        for (std::size_t k = 0; k < ch; ++k) sum[l * ch + k] += s[p * ch + k];
    }
    image8u out(width(), height(), pixel_format_from_channels(src.channels()));
    std::uint8_t* d = out.data();
    for (std::size_t p = 0; p < N; ++p) {
        const std::size_t l = std::size_t(L[p]);
        for (std::size_t k = 0; k < ch; ++k) d[p * ch + k] = std::uint8_t((sum[l * ch + k] + n[l] / 2) / n[l]);
    }
    return out;
}

// ----------------------------------------------------------------------------------- the entry point

superpixel_map superpixels(const mat8u& src, const superpixel_options& opt) {
    if (src.empty()) throw std::invalid_argument("superpixels needs a non-empty image");
    if (src.channels() > 4) throw std::invalid_argument("superpixels needs an image of 1 to 4 channels");
    if (opt.count < 1) throw std::invalid_argument("count must be >= 1");
    if (!(opt.compactness > 0.0f) || !std::isfinite(opt.compactness))
        throw std::invalid_argument("compactness must be a positive finite number");
    if (opt.iterations < 1) throw std::invalid_argument("iterations must be >= 1");

    const mat32f lab = to_lab(src);
    const grid g = make_grid(src.width(), src.height(), opt.count);
    const std::vector<seed> seeds = make_seeds(lab, g);

    const bool snic = opt.method == superpixel_method::snic;
    mat32i labels = snic ? run_snic(lab, g, seeds, opt) : run_slic(lab, g, seeds, opt);
    int count;
    if (!snic && opt.enforce_connectivity)
        count = enforce_connectivity(labels, std::max<std::size_t>(1, std::size_t(0.25f * g.area)));
    else
        count = relabel(labels, seeds.size());
    return superpixel_map(std::move(labels), count);
}

}  // namespace stb
