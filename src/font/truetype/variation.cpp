// Variable fonts: the design axes (`fvar`, `avar`), glyph outline deltas (`gvar`) and advance deltas (`HVAR`).
// Written from the OpenType specification (Font Variations Overview, `gvar`, `HVAR`, Item Variation Store);
// the deltas and the interpolation of untouched points (IUP) follow the same rules as fontTools, which is what
// the tests compare against.
//
// A font is untrusted input: every read goes through otl::byte_view (out-of-range reads give 0), every loop is
// bounded by a 16-bit count, and a table that does not add up is ignored (the glyph keeps its default shape).
#include <algorithm>
#include <cmath>
#include <cstddef>

#include "font/truetype/truetype.hpp"

namespace stb::truetype {

using otl::byte_view;
using otl::make_tag;

namespace {

constexpr unsigned max_axes = 64;

float fixed_at(const byte_view& b, std::size_t off) { return static_cast<std::int32_t>(b.u32(off)) / 65536.0f; }
float f2dot14_at(const byte_view& b, std::size_t off) { return b.i16(off) / 16384.0f; }

// avar: the piecewise linear map of one axis (`axis` counts from 0), applied to a normalised coordinate.
float apply_avar(const byte_view& avar, unsigned axis_count, unsigned axis, float v) {
    if (avar.empty() || avar.u16(0) != 1) return v;  // only version 1 maps (version 2 adds a variation store)
    std::size_t p = 8;
    for (unsigned a = 0; a < axis_count && a < max_axes; ++a) {
        const unsigned n = avar.u16(p);
        if (a == axis) {
            if (n == 0) return v;
            float from_first = f2dot14_at(avar, p + 2), to_first = f2dot14_at(avar, p + 4);
            if (v < from_first) return v + to_first - from_first;
            for (unsigned k = 0; k < n; ++k) {
                const float from = f2dot14_at(avar, p + 2 + 4 * std::size_t(k));
                const float to = f2dot14_at(avar, p + 4 + 4 * std::size_t(k));
                if (v == from) return to;
                if (v < from) {
                    const float from_prev = f2dot14_at(avar, p + 2 + 4 * std::size_t(k - 1));
                    const float to_prev = f2dot14_at(avar, p + 4 + 4 * std::size_t(k - 1));
                    return from == from_prev ? to : to_prev + (to - to_prev) * (v - from_prev) / (from - from_prev);
                }
            }
            const float from_last = f2dot14_at(avar, p + 2 + 4 * std::size_t(n - 1));
            const float to_last = f2dot14_at(avar, p + 4 + 4 * std::size_t(n - 1));
            return v + to_last - from_last;
        }
        p += 2 + 4 * std::size_t(n);
    }
    return v;
}

// How much of a tuple (or item variation region) applies at `coords`: 0 for none, 1 for all of it.
// `peak`, `start` and `end` are read at `off`, `axes` F2Dot14 values apart; start / end are used when `intermediate`.
float tuple_scalar(const std::vector<float>& coords, const float* peak, const float* start, const float* end,
                   bool intermediate) {
    float scalar = 1.0f;
    for (std::size_t a = 0; a < coords.size(); ++a) {
        const float p = peak[a];
        if (p == 0.0f) continue;
        const float c = coords[a];
        if (c == 0.0f) return 0.0f;
        if (intermediate) {
            if (c < start[a] || c > end[a]) return 0.0f;
            if (c == p) continue;
            scalar *= c < p ? (c - start[a]) / (p - start[a]) : (end[a] - c) / (end[a] - p);
        } else {
            if (c == p) continue;
            if ((c < 0.0f) != (p < 0.0f) || std::fabs(c) > std::fabs(p)) return 0.0f;
            scalar *= c / p;
        }
    }
    return scalar;
}

// "Packed" point numbers of a tuple: a count (0 = every point), then runs of byte or word deltas. Returns the
// bytes used; `all` is set when the tuple moves every point.
std::size_t read_packed_points(const byte_view& b, std::size_t p, std::vector<int>& points, bool& all) {
    const std::size_t start = p;
    unsigned count = b.u8(p++);
    if (count & 0x80u) count = ((count & 0x7Fu) << 8) | b.u8(p++);
    points.clear();
    all = count == 0;
    if (all) return p - start;
    points.reserve(count);
    int current = 0;
    while (points.size() < count && p <= b.size()) {
        const unsigned control = b.u8(p++);
        const unsigned run = (control & 0x7Fu) + 1;
        const bool words = (control & 0x80u) != 0;
        for (unsigned k = 0; k < run && points.size() < count; ++k) {
            current += words ? int(b.u16(p)) : int(b.u8(p));
            p += words ? 2 : 1;
            points.push_back(current);
        }
    }
    return p - start;
}

// `n` packed deltas: runs of zeros, bytes or words.
void read_packed_deltas(const byte_view& b, std::size_t& p, int n, std::vector<float>& out) {
    out.assign(std::size_t(n), 0.0f);
    int i = 0;
    while (i < n && p <= b.size()) {
        const unsigned control = b.u8(p++);
        const int run = int(control & 0x3Fu) + 1;
        if (control & 0x80u) {  // zeros
            i += run;
        } else if (control & 0x40u) {  // words
            for (int k = 0; k < run && i < n; ++k, p += 2) out[std::size_t(i++)] = float(b.i16(p));
        } else {  // bytes
            for (int k = 0; k < run && i < n; ++k, p += 1) out[std::size_t(i++)] = float(static_cast<std::int8_t>(b.u8(p)));
        }
    }
}

// Interpolates the untouched points between two touched ones along one axis (the rule fontTools and FreeType share).
float iup_value(float c, float rc1, float rd1, float rc2, float rd2) {
    if (rc1 == rc2) return rd1 == rd2 ? rd1 : 0.0f;
    if (rc1 > rc2) {
        std::swap(rc1, rc2);
        std::swap(rd1, rd2);
    }
    if (c <= rc1) return rd1;
    if (c >= rc2) return rd2;
    return rd1 + (c - rc1) * (rd2 - rd1) / (rc2 - rc1);
}

// IUP of one axis over every contour. `orig` holds the default coordinates, `d` the deltas (only the touched ones
// are set on entry).
void iup_axis(const int* orig, std::vector<float>& d, const std::vector<char>& touched, const std::vector<int>& ends) {
    int first = 0;
    for (const int last : ends) {
        if (last < first || std::size_t(last) >= d.size()) break;
        std::vector<int> t;
        for (int i = first; i <= last; ++i)
            if (touched[std::size_t(i)]) t.push_back(i);
        if (t.size() == 1) {
            for (int i = first; i <= last; ++i)
                if (!touched[std::size_t(i)]) d[std::size_t(i)] = d[std::size_t(t[0])];
        } else if (t.size() > 1) {
            for (std::size_t k = 0; k < t.size(); ++k) {
                const int a = t[k], b = t[(k + 1) % t.size()];
                for (int i = a == last ? first : a + 1; i != b; i = i == last ? first : i + 1) {
                    d[std::size_t(i)] = iup_value(float(orig[i]), float(orig[a]), d[std::size_t(a)], float(orig[b]),
                                                  d[std::size_t(b)]);
                }
            }
        }
        first = last + 1;
    }
}

// The delta of one item of an Item Variation Store (HVAR, MVAR, ...) at `coords`.
float item_delta(const byte_view& store, const std::vector<float>& coords, std::uint32_t outer, std::uint32_t inner) {
    if (store.empty() || store.u16(0) != 1) return 0.0f;
    const unsigned data_count = store.u16(6);
    if (outer >= data_count) return 0.0f;
    const byte_view regions = store.from(store.u32(2));
    const byte_view data = store.from(store.u32(8 + 4 * std::size_t(outer)));
    const unsigned item_count = data.u16(0), word_field = data.u16(2), region_count = data.u16(4);
    if (inner >= item_count) return 0.0f;
    const bool long_words = (word_field & 0x8000u) != 0;
    const unsigned word_count = word_field & 0x7FFFu;
    if (word_count > region_count) return 0.0f;
    const std::size_t word_size = long_words ? 4 : 2, small_size = long_words ? 2 : 1;
    const std::size_t row_size = word_count * word_size + (region_count - word_count) * small_size;
    std::size_t p = 6 + 2 * std::size_t(region_count) + row_size * inner;

    const unsigned axis_count = regions.u16(0), total_regions = regions.u16(2);
    if (axis_count != coords.size() || axis_count > max_axes) return 0.0f;
    float sum = 0.0f;
    for (unsigned r = 0; r < region_count; ++r) {
        const unsigned index = data.u16(6 + 2 * std::size_t(r));
        float delta;
        if (r < word_count) {
            delta = long_words ? float(static_cast<std::int32_t>(data.u32(p))) : float(data.i16(p));
            p += word_size;
        } else {
            delta = long_words ? float(data.i16(p)) : float(static_cast<std::int8_t>(data.u8(p)));
            p += small_size;
        }
        if (delta == 0.0f || index >= total_regions) continue;
        float peak[max_axes], start[max_axes], end[max_axes];
        for (unsigned a = 0; a < axis_count; ++a) {
            const std::size_t at = 4 + (std::size_t(index) * axis_count + a) * 6;
            start[a] = f2dot14_at(regions, at);
            peak[a] = f2dot14_at(regions, at + 2);
            end[a] = f2dot14_at(regions, at + 4);
            // a malformed region axis does not constrain the scalar (as in the spec)
            if (start[a] > peak[a] || peak[a] > end[a] || (start[a] < 0.0f && end[a] > 0.0f && peak[a] != 0.0f)) peak[a] = 0.0f;
        }
        sum += delta * tuple_scalar(coords, peak, start, end, true);
    }
    return sum;
}

}  // namespace

std::vector<face::axis> read_fvar_axes(byte_view fvar) {
    std::vector<face::axis> out;
    if (fvar.empty()) return out;
    const unsigned offset = fvar.u16(4), count = fvar.u16(8), size = fvar.u16(10);
    if (size < 20 || count > max_axes) return out;
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t r = offset + std::size_t(i) * size;
        if (!fvar.has(r, 20)) break;
        face::axis a;
        a.tag = fvar.u32(r);
        a.min = fixed_at(fvar, r + 4);
        a.def = fixed_at(fvar, r + 8);
        a.max = fixed_at(fvar, r + 12);
        if (!(a.min <= a.def && a.def <= a.max)) a.min = a.max = a.def;  // damaged range: the axis does not move
        out.push_back(a);
    }
    return out;
}

std::vector<face::axis> face::axes() const { return read_fvar_axes(fvar_); }

float face::axis_value(std::uint32_t tag, float fallback) const {
    const std::vector<axis> all = axes();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].tag == tag) return i < values_.size() ? values_[i] : all[i].def;
    return fallback;
}

bool face::set_variation(std::uint32_t tag, float value) {
    const std::vector<axis> all = axes();
    std::size_t index = all.size();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].tag == tag) index = i;
    if (index == all.size()) return false;
    if (values_.size() != all.size()) {
        values_.resize(all.size());
        for (std::size_t i = 0; i < all.size(); ++i) values_[i] = all[i].def;
    }
    values_[index] = std::min(std::max(value, all[index].min), all[index].max);
    refresh_coords();
    return true;
}

void face::refresh_coords() {
    const std::vector<axis> all = axes();
    coords_.assign(all.size(), 0.0f);
    varied_ = false;
    for (std::size_t i = 0; i < all.size() && i < values_.size(); ++i) {
        const float v = values_[i];
        float norm = 0.0f;
        if (v < all[i].def) norm = all[i].def == all[i].min ? 0.0f : -(all[i].def - v) / (all[i].def - all[i].min);
        else if (v > all[i].def) norm = all[i].max == all[i].def ? 0.0f : (v - all[i].def) / (all[i].max - all[i].def);
        norm = apply_avar(avar_, unsigned(all.size()), unsigned(i), std::min(std::max(norm, -1.0f), 1.0f));
        coords_[i] = std::min(std::max(norm, -1.0f), 1.0f);
        if (coords_[i] != 0.0f) varied_ = true;
    }
}

// The glyph's point deltas at the current coordinates. `n_points` counts the outline points plus the four phantom
// points; `dx` / `dy` come back with that size. With `orig_x` / `orig_y` / `contour_ends` the points a tuple does not
// list are interpolated (IUP), without them they stay 0 (composite glyphs, whose "points" are the components).
bool face::gvar_deltas(int glyph, int n_points, const int* orig_x, const int* orig_y, const std::vector<int>* contour_ends,
                       std::vector<float>& dx, std::vector<float>& dy) const {
    dx.assign(std::size_t(n_points), 0.0f);
    dy.assign(std::size_t(n_points), 0.0f);
    if (gvar_.empty() || glyph < 0 || n_points < 0 || coords_.empty()) return false;
    const unsigned axis_count = gvar_.u16(4), shared_count = gvar_.u16(6), glyph_count = gvar_.u16(12);
    if (axis_count != coords_.size() || axis_count > max_axes || unsigned(glyph) >= glyph_count) return false;
    const bool long_offsets = (gvar_.u16(14) & 1u) != 0;
    const std::size_t entry = 20 + std::size_t(glyph) * (long_offsets ? 4 : 2);
    const std::uint32_t o1 = long_offsets ? gvar_.u32(entry) : gvar_.u16(entry) * 2u;
    const std::uint32_t o2 = long_offsets ? gvar_.u32(entry + 4) : gvar_.u16(entry + 2) * 2u;
    if (o2 <= o1) return true;  // no variation data: the glyph does not change
    const byte_view data = gvar_.sub(std::size_t(gvar_.u32(16)) + o1, o2 - o1);
    if (data.empty()) return false;

    const unsigned tuple_field = data.u16(0);
    const unsigned tuple_count = tuple_field & 0x0FFFu;
    std::size_t serialized = data.u16(2);
    std::vector<int> shared_points;
    bool shared_all = true;
    if (tuple_field & 0x8000u) serialized += read_packed_points(data, serialized, shared_points, shared_all);

    const byte_view shared_tuples = gvar_.from(gvar_.u32(8));
    std::size_t header = 4;
    std::vector<float> tdx, tdy, px_all, py_all;
    for (unsigned t = 0; t < tuple_count; ++t) {
        const unsigned size = data.u16(header), index = data.u16(header + 2);
        header += 4;
        float peak[max_axes] = {}, start[max_axes] = {}, end[max_axes] = {};
        if (index & 0x8000u) {
            for (unsigned a = 0; a < axis_count; ++a) peak[a] = f2dot14_at(data, header + 2 * std::size_t(a));
            header += 2 * std::size_t(axis_count);
        } else {
            const unsigned shared_index = index & 0x0FFFu;
            if (shared_index >= shared_count) return false;
            for (unsigned a = 0; a < axis_count; ++a)
                peak[a] = f2dot14_at(shared_tuples, (std::size_t(shared_index) * axis_count + a) * 2);
        }
        const bool intermediate = (index & 0x4000u) != 0;
        if (intermediate) {
            for (unsigned a = 0; a < axis_count; ++a) {
                start[a] = f2dot14_at(data, header + 2 * std::size_t(a));
                end[a] = f2dot14_at(data, header + 2 * std::size_t(axis_count) + 2 * std::size_t(a));
            }
            header += 4 * std::size_t(axis_count);
        }
        const std::size_t tuple_start = serialized;
        serialized += size;
        const float scalar = tuple_scalar(coords_, peak, start, end, intermediate);
        if (scalar == 0.0f) continue;

        std::size_t p = tuple_start;
        std::vector<int> private_points;
        bool all = shared_all;
        const std::vector<int>* points = &shared_points;
        if (index & 0x2000u) {
            p += read_packed_points(data, p, private_points, all);
            points = &private_points;
        }
        const int listed = all ? n_points : int(points->size());
        read_packed_deltas(data, p, listed, px_all);
        read_packed_deltas(data, p, listed, py_all);

        if (all) {
            for (int i = 0; i < n_points; ++i) {
                dx[std::size_t(i)] += scalar * px_all[std::size_t(i)];
                dy[std::size_t(i)] += scalar * py_all[std::size_t(i)];
            }
            continue;
        }
        tdx.assign(std::size_t(n_points), 0.0f);
        tdy.assign(std::size_t(n_points), 0.0f);
        std::vector<char> touched(std::size_t(n_points), 0);
        for (std::size_t k = 0; k < points->size(); ++k) {
            const int pt = (*points)[k];
            if (pt < 0 || pt >= n_points) continue;
            tdx[std::size_t(pt)] = px_all[k];
            tdy[std::size_t(pt)] = py_all[k];
            touched[std::size_t(pt)] = 1;
        }
        if (orig_x && orig_y && contour_ends) {
            iup_axis(orig_x, tdx, touched, *contour_ends);
            iup_axis(orig_y, tdy, touched, *contour_ends);
        }
        for (int i = 0; i < n_points; ++i) {
            dx[std::size_t(i)] += scalar * tdx[std::size_t(i)];
            dy[std::size_t(i)] += scalar * tdy[std::size_t(i)];
        }
    }
    return true;
}

// HVAR: the advance width delta of `glyph`.
float face::hvar_advance_delta(int glyph) const {
    if (hvar_.empty() || glyph < 0) return 0.0f;
    const std::uint32_t store_offset = hvar_.u32(4), map_offset = hvar_.u32(8);
    std::uint32_t outer = 0, inner = std::uint32_t(glyph);
    if (map_offset) {  // DeltaSetIndexMap: glyph -> (outer, inner)
        const byte_view m = hvar_.from(map_offset);
        const unsigned format = m.u8(0), entry_format = m.u8(1);
        const std::uint32_t map_count = format == 0 ? m.u16(2) : m.u32(2);
        const std::size_t map_data = format == 0 ? 4 : 6;
        if (map_count == 0) return 0.0f;
        const std::uint32_t i = std::min<std::uint32_t>(std::uint32_t(glyph), map_count - 1);
        const unsigned entry_size = ((entry_format >> 4) & 3u) + 1, inner_bits = (entry_format & 0xFu) + 1;
        std::uint32_t entry = 0;
        for (unsigned k = 0; k < entry_size; ++k) entry = (entry << 8) | m.u8(map_data + std::size_t(i) * entry_size + k);
        outer = entry >> inner_bits;
        inner = entry & ((1u << inner_bits) - 1u);
    }
    return item_delta(hvar_.from(store_offset), coords_, outer, inner);
}

// The change of a glyph's advance width at the current coordinates, in font units.
float face::advance_delta(int glyph) const {
    if (!varied_) return 0.0f;
    if (!hvar_.empty()) return hvar_advance_delta(glyph);
    // No HVAR: the advance is the distance between the first two phantom points, which `gvar` moves.
    int points = 0;
    const byte_view g = glyph_data(glyph);
    if (!g.empty()) {
        const int contours = g.i16(0);
        if (contours > 0) {
            points = 1 + g.u16(10 + std::size_t(contours) * 2 - 2);
        } else if (contours < 0) {  // composite: one "point" per component
            std::size_t p = 10;
            for (bool more = true; more && points < 0xFFFF;) {
                const unsigned flags = g.u16(p);
                p += 4 + ((flags & 1u) ? 4 : 2);
                if (flags & (1u << 3)) p += 2;
                else if (flags & (1u << 6)) p += 4;
                else if (flags & (1u << 7)) p += 8;
                ++points;
                more = (flags & (1u << 5)) != 0 && g.has(p, 4);
            }
        }
    }
    std::vector<float> dx, dy;
    if (!gvar_deltas(glyph, points + 4, nullptr, nullptr, nullptr, dx, dy)) return 0.0f;
    return dx[std::size_t(points) + 1] - dx[std::size_t(points)];
}

// The exact bounding box of the outline at the current coordinates (the glyph header holds the default instance's).
box face::varied_glyph_box(int glyph) const {
    const std::vector<vertex> v = outline_truetype(glyph, 0);
    if (v.empty()) return {};
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    const auto add = [&](float x, float y) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    };
    // The extreme of a quadratic curve p0 -> p1 with control c on one axis, if it lies inside the segment.
    const auto extreme = [](float p0, float c, float p1, float& out) {
        const float denom = p0 - 2.0f * c + p1;
        if (denom == 0.0f) return false;
        const float t = (p0 - c) / denom;
        if (t <= 0.0f || t >= 1.0f) return false;
        const float u = 1.0f - t;
        out = u * u * p0 + 2.0f * u * t * c + t * t * p1;
        return true;
    };
    float cx = 0, cy = 0;
    for (const vertex& p : v) {
        if (p.type == vertex::curve) {
            float e;
            if (extreme(cx, p.cx, p.x, e)) add(e, cy);
            if (extreme(cy, p.cy, p.y, e)) add(cx, e);
        }
        add(p.x, p.y);
        cx = p.x;
        cy = p.y;
    }
    return box{int(std::floor(x0)), int(std::floor(y0)), int(std::ceil(x1)), int(std::ceil(y1))};
}

}  // namespace stb::truetype
