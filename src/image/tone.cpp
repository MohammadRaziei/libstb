#include "stb/tone.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "stb/parallel.hpp"

namespace stb {
namespace {

using utils::parallel_for;

// Floors of what one core does (ns) per value, so that a doubtful case stays on one thread (util/parallel.hpp).
constexpr double table_ns = 0.4;  // one table look-up or one clamp-and-multiply, per value
constexpr double float_ns = 1.0;  // one float interpolation or pow, per value

// ----------------------------------------------------------------------------------------------- shape

// Which channels are colour and which one is alpha: 1 gray, 2 gray + alpha, 3 RGB, 4 RGBA. In the luma mode
// an RGB(A) image has a single "plane", its brightness; otherwise every colour channel is a plane.
struct plan {
    int channels = 0;
    int colour = 0;  // channels that are not alpha
    int planes = 0;
    bool luma_plane = false;  // the one plane is the brightness of RGB, not a channel
};

plan make_plan(int channels, tone_mode mode) {
    if (channels < 1 || channels > 4)
        throw std::invalid_argument("expected an image of 1 to 4 channels (gray, gray + alpha, RGB, RGBA)");
    plan p;
    p.channels = channels;
    p.colour = (channels == 2 || channels == 4) ? channels - 1 : channels;
    p.luma_plane = mode == tone_mode::luma && p.colour == 3;
    p.planes = p.luma_plane ? 1 : p.colour;
    return p;
}

// 0.299 R + 0.587 G + 0.114 B in 16-bit fixed point (19595 + 38470 + 7471 == 65536), rounded: the formula of
// the gray conversion elsewhere in libstb.
inline int luma8(int r, int g, int b) noexcept { return (r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16; }
inline int clamp255(int v) noexcept { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline bool is_nan(float v) noexcept { return v != v; }
inline float luma_f(float r, float g, float b) noexcept { return 0.299f * r + 0.587f * g + 0.114f * b; }

void check_gamma(float g) {
    if (!(g > 0.0f) || !std::isfinite(g)) throw std::invalid_argument("gamma must be a positive finite number");
}
void check_levels(float in_black, float in_white, float g, float out_black, float out_white) {
    check_gamma(g);
    const auto unit = [](float v) { return v >= 0.0f && v <= 1.0f; };  // false for NaN too
    if (!unit(in_black) || !unit(in_white) || !(in_black < in_white))
        throw std::invalid_argument("levels need 0 <= in_black < in_white <= 1");
    if (!unit(out_black) || !unit(out_white)) throw std::invalid_argument("levels need out_black and out_white in [0, 1]");
}

// ---------------------------------------------------------------------------------------- 8-bit tables

using hist8 = std::array<std::uint64_t, 256>;
using lut8 = std::array<std::uint8_t, 256>;

lut8 identity_lut() {
    lut8 t{};
    for (int i = 0; i < 256; ++i) t[std::size_t(i)] = std::uint8_t(i);
    return t;
}

std::vector<hist8> histograms8(const mat8u& m, const plan& p) {
    std::vector<hist8> h(std::size_t(p.planes), hist8{});
    const int c = p.channels;
    for (int y = 0; y < m.height(); ++y) {
        const std::uint8_t* s = m.row(y);
        for (int x = 0; x < m.width(); ++x, s += c) {
            if (p.luma_plane) {
                ++h[0][std::size_t(luma8(s[0], s[1], s[2]))];
            } else {
                for (int k = 0; k < p.colour; ++k) ++h[std::size_t(k)][s[k]];
            }
        }
    }
    return h;
}

std::uint64_t total(const hist8& h) {
    std::uint64_t n = 0;
    for (std::uint64_t v : h) n += v;
    return n;
}

// The cumulative histogram scaled to the levels, the classic equalisation: (cdf - cdf_min) / (n - cdf_min).
lut8 equalize_lut(const hist8& h) {
    const std::uint64_t n = total(h);
    int vmin = 0;
    while (vmin < 256 && h[std::size_t(vmin)] == 0) ++vmin;
    if (vmin >= 256) return identity_lut();
    const std::uint64_t c0 = h[std::size_t(vmin)];
    if (n == c0) return identity_lut();  // a single value
    const std::uint64_t span = n - c0;
    lut8 t{};
    std::uint64_t cum = 0;
    for (int v = 0; v < 256; ++v) {
        cum += h[std::size_t(v)];
        t[std::size_t(v)] = v < vmin ? std::uint8_t(0) : std::uint8_t(((cum - c0) * 255 + span / 2) / span);
    }
    return t;
}

// Pillow's autocontrast: `cutoff` percent of the values are cut off at each end, the rest is stretched.
lut8 auto_contrast_lut(hist8 h, float cutoff_percent) {
    const std::uint64_t n = total(h);
    const std::uint64_t cut = std::uint64_t(double(cutoff_percent) * double(n) / 100.0);
    const auto trim = [&](int from, int step) {
        std::uint64_t c = cut;
        int i = from;
        for (; i >= 0 && i < 256; i += step) {
            std::uint64_t& v = h[std::size_t(i)];
            if (c > v) {
                c -= v;
                v = 0;
            } else {
                v -= c;
                c = 0;
            }
            if (v) break;
        }
        return i;
    };
    const int lo = trim(0, 1);
    const int hi = trim(255, -1);
    if (lo >= 256 || hi < 0 || hi <= lo) return identity_lut();
    lut8 t{};
    const int span = hi - lo;
    for (int v = 0; v < 256; ++v)
        t[std::size_t(v)] = v <= lo ? std::uint8_t(0) : (v >= hi ? std::uint8_t(255) : std::uint8_t(((v - lo) * 255 + span / 2) / span));
    return t;
}

// Each level goes to the lowest level of the reference whose cumulative share is at least its own.
lut8 match_lut(const hist8& src, const hist8& ref) {
    const double ns = double(total(src)), nr = double(total(ref));
    lut8 t{};
    std::uint64_t cs = 0, cr = ref[0];
    int u = 0;
    for (int v = 0; v < 256; ++v) {
        cs += src[std::size_t(v)];
        const double p = double(cs) / ns;
        while (u < 255 && double(cr) / nr < p) cr += ref[std::size_t(++u)];
        t[std::size_t(v)] = std::uint8_t(u);
    }
    return t;
}

lut8 gamma_table(float g) {
    const double inv = 1.0 / double(g);
    lut8 t{};
    for (int i = 0; i < 256; ++i) t[std::size_t(i)] = std::uint8_t(clamp255(int(std::lround(255.0 * std::pow(double(i) / 255.0, inv)))));
    return t;
}

lut8 levels_table(float in_black, float in_white, float g, float out_black, float out_white) {
    const double ib = in_black, iw = in_white, ob = out_black, ow = out_white, inv = 1.0 / double(g);
    lut8 t{};
    for (int i = 0; i < 256; ++i) {
        double s = (double(i) / 255.0 - ib) / (iw - ib);
        s = s < 0.0 ? 0.0 : (s > 1.0 ? 1.0 : s);
        const double v = ob + std::pow(s, inv) * (ow - ob);
        t[std::size_t(i)] = std::uint8_t(clamp255(int(std::lround(255.0 * v))));
    }
    return t;
}

// `applied` channels of every pixel go through a table each: tables + k * table_stride for channel k
// (table_stride 0: the same table for all).
void apply_tables(mat8u& img, const std::uint8_t* tables, std::size_t table_stride, int applied) {
    const int c = img.channels(), w = img.width();
    parallel_for(std::size_t(img.height()), double(w) * double(c) * table_ns, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t y = y0; y < y1; ++y) {
            std::uint8_t* s = img.row(int(y));
            for (int x = 0; x < w; ++x, s += c)
                for (int k = 0; k < applied; ++k) s[k] = tables[std::size_t(k) * table_stride + s[k]];
        }
    });
}

// Applies the table of each plane (one per colour channel, or the one of the brightness).
void apply_plan8(mat8u& img, const plan& p, const std::vector<lut8>& luts) {
    if (!p.luma_plane) {
        std::vector<std::uint8_t> flat;
        flat.reserve(luts.size() * 256);
        for (const lut8& t : luts) flat.insert(flat.end(), t.begin(), t.end());
        apply_tables(img, flat.data(), 256, p.colour);
        return;
    }
    const lut8& t = luts[0];
    const int c = p.channels, w = img.width();
    parallel_for(std::size_t(img.height()), double(w) * double(c) * table_ns, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t y = y0; y < y1; ++y) {
            std::uint8_t* s = img.row(int(y));
            for (int x = 0; x < w; ++x, s += c) {
                const int yy = luma8(s[0], s[1], s[2]);
                const int d = int(t[std::size_t(yy)]) - yy;
                s[0] = std::uint8_t(clamp255(s[0] + d));
                s[1] = std::uint8_t(clamp255(s[1] + d));
                s[2] = std::uint8_t(clamp255(s[2] + d));
            }
        }
    });
}

// ------------------------------------------------------------------------------------- float mappings

constexpr int fbins = 256;

struct fstats {
    std::array<std::uint64_t, fbins> h{};
    std::uint64_t n = 0;
    float lo = 1.0f, hi = 0.0f;  // the smallest and the largest value (clamped to [0, 1]); lo > hi while n == 0
    void add(float v) noexcept {  // v is clamped and not NaN
        int b = int(v * float(fbins));
        if (b > fbins - 1) b = fbins - 1;
        ++h[std::size_t(b)];
        if (n == 0 || v < lo) lo = v;
        if (n == 0 || v > hi) hi = v;
        ++n;
    }
};

std::vector<fstats> stats_f(const mat32f& m, const plan& p) {
    std::vector<fstats> st(std::size_t(p.planes));
    const int c = p.channels;
    for (int y = 0; y < m.height(); ++y) {
        const float* s = m.row(y);
        for (int x = 0; x < m.width(); ++x, s += c) {
            if (p.luma_plane) {
                if (is_nan(s[0]) || is_nan(s[1]) || is_nan(s[2])) continue;
                st[0].add(luma_f(clamp01(s[0]), clamp01(s[1]), clamp01(s[2])));
            } else {
                for (int k = 0; k < p.colour; ++k)
                    if (!is_nan(s[k])) st[std::size_t(k)].add(clamp01(s[k]));
            }
        }
    }
    return st;
}

// A map of [0, 1] onto [0, 1]: either a straight line (v - lo) * scale, clamped, or the 257 values at the bin
// edges i / 256, interpolated linearly between them.
struct fmap {
    bool linear = true;
    float lo = 0.0f, scale = 1.0f;
    std::array<float, fbins + 1> knots{};
    float operator()(float v) const noexcept {
        if (linear) return clamp01((v - lo) * scale);
        const float x = v * float(fbins);
        int k = int(x);
        if (k > fbins - 1) k = fbins - 1;
        const float f = x - float(k);
        return knots[std::size_t(k)] + f * (knots[std::size_t(k) + 1] - knots[std::size_t(k)]);
    }
};

// The cumulative share of the values below each bin edge: 257 numbers from 0 to 1.
std::array<double, fbins + 1> cdf_of(const fstats& s) {
    std::array<double, fbins + 1> c{};
    std::uint64_t cum = 0;
    for (int k = 0; k < fbins; ++k) {
        cum += s.h[std::size_t(k)];
        c[std::size_t(k) + 1] = double(cum) / double(s.n);
    }
    return c;
}

// The value below which the share p of the values lies: the inverse of the cdf, inside the first bin that has
// values and reaches p.
float quantile(const fstats& s, const std::array<double, fbins + 1>& c, double p) {
    for (int k = 0; k < fbins; ++k) {
        if (s.h[std::size_t(k)] == 0 || c[std::size_t(k) + 1] < p) continue;
        const double a = c[std::size_t(k)], b = c[std::size_t(k) + 1];
        const double t = (p - a) / (b - a);
        return float((double(k) + (t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t))) / double(fbins));
    }
    return 1.0f;
}

fmap equalize_map(const fstats& s) {
    fmap m;  // identity
    if (s.n == 0) return m;
    int occupied = 0;
    for (std::uint64_t v : s.h) occupied += v != 0;
    if (occupied < 2) return m;  // a single bin: nothing to spread
    const auto c = cdf_of(s);
    m.linear = false;
    for (int k = 0; k <= fbins; ++k) m.knots[std::size_t(k)] = float(c[std::size_t(k)]);
    return m;
}

fmap auto_contrast_map(const fstats& s, float cutoff_percent) {
    fmap m;
    if (s.n == 0) return m;
    float lo = s.lo, hi = s.hi;
    if (cutoff_percent > 0.0f) {
        const auto c = cdf_of(s);
        const double cut = double(cutoff_percent) / 100.0;
        lo = quantile(s, c, cut);
        hi = quantile(s, c, 1.0 - cut);
    }
    if (!(hi > lo)) return m;
    m.lo = lo;
    m.scale = 1.0f / (hi - lo);
    return m;
}

fmap match_map(const fstats& s, const fstats& ref) {
    fmap m;
    if (s.n == 0) return m;
    const auto cs = cdf_of(s);
    const auto cr = cdf_of(ref);
    m.linear = false;
    for (int k = 0; k <= fbins; ++k) m.knots[std::size_t(k)] = quantile(ref, cr, cs[std::size_t(k)]);
    return m;
}

void apply_plan_f(mat32f& img, const plan& p, const std::vector<fmap>& maps) {
    const int c = p.channels, w = img.width();
    parallel_for(std::size_t(img.height()), double(w) * double(c) * float_ns, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t y = y0; y < y1; ++y) {
            float* s = img.row(int(y));
            for (int x = 0; x < w; ++x, s += c) {
                if (p.luma_plane) {
                    if (is_nan(s[0]) || is_nan(s[1]) || is_nan(s[2])) continue;
                    const float r = clamp01(s[0]), g = clamp01(s[1]), b = clamp01(s[2]);
                    const float yy = luma_f(r, g, b);
                    const float d = maps[0](yy) - yy;
                    s[0] = clamp01(r + d);
                    s[1] = clamp01(g + d);
                    s[2] = clamp01(b + d);
                } else {
                    for (int k = 0; k < p.colour; ++k)
                        if (!is_nan(s[k])) s[k] = maps[std::size_t(k)](clamp01(s[k]));
                }
            }
        }
    });
}

void check_cutoff(float cutoff_percent) {
    if (!(cutoff_percent >= 0.0f && cutoff_percent < 50.0f))
        throw std::invalid_argument("cutoff_percent must be at least 0 and below 50");
}

// Gamma and levels on floats: the same formula for every colour channel.
template <class F>
void map_colour_values(mat32f& img, F&& f) {
    if (img.empty()) return;
    const plan p = make_plan(img.channels(), tone_mode::per_channel);
    const int c = p.channels, w = img.width();
    parallel_for(std::size_t(img.height()), double(w) * double(c) * float_ns, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t y = y0; y < y1; ++y) {
            float* s = img.row(int(y));
            for (int x = 0; x < w; ++x, s += c)
                for (int k = 0; k < p.colour; ++k)
                    if (!is_nan(s[k])) s[k] = f(clamp01(s[k]));
        }
    });
}

// ------------------------------------------------------------------------------- one pipeline, two element types

// What differs between 8 bits and floats: how the statistics of a plane are kept (a histogram of 256 levels, or a
// histogram over [0, 1] with the smallest and the largest value), what a mapping is (a 256-entry table, or a line or a
// table of knots to interpolate) and how it is applied. The pipeline below is written once over T; only the types
// listed here are accepted (any other T does not compile: the primary template has no definition).
template <class T>
struct tone_kernel;

template <>
struct tone_kernel<std::uint8_t> {
    using stats = hist8;
    using map = lut8;

    static std::vector<stats> analyse(const mat8u& m, const plan& p) { return histograms8(m, p); }
    static void apply(mat8u& m, const plan& p, const std::vector<map>& maps) { apply_plan8(m, p, maps); }

    static map equalize(const stats& s) { return equalize_lut(s); }
    static map auto_contrast(const stats& s, float cutoff_percent) { return auto_contrast_lut(s, cutoff_percent); }
    static map match(const stats& s, const stats& ref) { return match_lut(s, ref); }
    static void check_reference(const std::vector<stats>&) {}  // a histogram of a non-empty image is never empty

    static void gamma(mat8u& img, float g) {
        if (img.empty()) return;
        const plan p = make_plan(img.channels(), tone_mode::per_channel);
        const lut8 t = gamma_table(g);
        apply_tables(img, t.data(), 0, p.colour);
    }
    static void levels(mat8u& img, float in_black, float in_white, float g, float out_black, float out_white) {
        if (img.empty()) return;
        const plan p = make_plan(img.channels(), tone_mode::per_channel);
        const lut8 t = levels_table(in_black, in_white, g, out_black, out_white);
        apply_tables(img, t.data(), 0, p.colour);
    }
};

template <>
struct tone_kernel<float> {
    using stats = fstats;
    using map = fmap;

    static std::vector<stats> analyse(const mat32f& m, const plan& p) { return stats_f(m, p); }
    static void apply(mat32f& m, const plan& p, const std::vector<map>& maps) { apply_plan_f(m, p, maps); }

    static map equalize(const stats& s) { return equalize_map(s); }
    static map auto_contrast(const stats& s, float cutoff_percent) { return auto_contrast_map(s, cutoff_percent); }
    static map match(const stats& s, const stats& ref) { return match_map(s, ref); }
    static void check_reference(const std::vector<stats>& refs) {
        for (const stats& r : refs)
            if (r.n == 0) throw std::invalid_argument("the reference has no values (all NaN)");
    }

    static void gamma(mat32f& img, float g) {
        const float inv = 1.0f / g;
        map_colour_values(img, [inv](float v) { return inv == 1.0f ? v : std::pow(v, inv); });
    }
    static void levels(mat32f& img, float in_black, float in_white, float g, float out_black, float out_white) {
        const float inv = 1.0f / g, span = in_white - in_black, out_span = out_white - out_black;
        map_colour_values(img, [=](float v) {
            float s = clamp01((v - in_black) / span);
            if (inv != 1.0f) s = std::pow(s, inv);
            return clamp01(out_black + s * out_span);
        });
    }
};

// The shape of equalize, auto_contrast and match_histogram: take the statistics of each plane of the image, make a
// mapping out of each (`make_map(plane, stats)`: the one thing that differs between them), apply the mappings.
template <class T, class MakeMap>
void remap_from_histogram(mat<T>& img, tone_mode mode, MakeMap&& make_map) {
    using kernel = tone_kernel<T>;
    if (img.empty()) return;
    const plan p = make_plan(img.channels(), mode);
    const std::vector<typename kernel::stats> st = kernel::analyse(img, p);
    std::vector<typename kernel::map> maps;
    maps.reserve(st.size());
    for (std::size_t i = 0; i < st.size(); ++i) maps.push_back(make_map(i, st[i]));
    kernel::apply(img, p, maps);
}

}  // namespace

// ============================================================================================ histograms

mat32i histogram(const mat8u& src) {
    if (src.empty()) return mat32i();
    if (std::uint64_t(src.width()) * std::uint64_t(src.height()) > std::uint64_t(std::numeric_limits<std::int32_t>::max()))
        throw std::invalid_argument("too many values for a histogram of 32-bit counts");
    const int c = src.channels();
    mat32i h(256, 1, c);
    std::int32_t* out = h.data();
    for (int y = 0; y < src.height(); ++y) {
        const std::uint8_t* s = src.row(y);
        for (int x = 0; x < src.width(); ++x, s += c)
            for (int k = 0; k < c; ++k) ++out[std::size_t(s[k]) * std::size_t(c) + std::size_t(k)];
    }
    return h;
}

mat32i histogram(const mat32f& src, int bins) {
    if (bins < 1 || bins > 65536) throw std::invalid_argument("bins must be between 1 and 65536");
    if (src.empty()) return mat32i();
    if (std::uint64_t(src.width()) * std::uint64_t(src.height()) > std::uint64_t(std::numeric_limits<std::int32_t>::max()))
        throw std::invalid_argument("too many values for a histogram of 32-bit counts");
    const int c = src.channels();
    mat32i h(bins, 1, c);
    std::int32_t* out = h.data();
    for (int y = 0; y < src.height(); ++y) {
        const float* s = src.row(y);
        for (int x = 0; x < src.width(); ++x, s += c)
            for (int k = 0; k < c; ++k) {
                if (is_nan(s[k])) continue;
                int b = int(clamp01(s[k]) * float(bins));
                if (b > bins - 1) b = bins - 1;
                ++out[std::size_t(b) * std::size_t(c) + std::size_t(k)];
            }
    }
    return h;
}

// ============================================================================================ tables

mat8u gamma_lut(float g) {
    check_gamma(g);
    const lut8 t = gamma_table(g);
    return mat8u(256, 1, 1, std::vector<std::uint8_t>(t.begin(), t.end()));
}

mat8u levels_lut(float in_black, float in_white, float g, float out_black, float out_white) {
    check_levels(in_black, in_white, g, out_black, out_white);
    const lut8 t = levels_table(in_black, in_white, g, out_black, out_white);
    return mat8u(256, 1, 1, std::vector<std::uint8_t>(t.begin(), t.end()));
}

image8u apply_lut(const mat8u& src, const mat8u& lut) {
    if (lut.width() != 256 || lut.height() != 1 || lut.empty())
        throw std::invalid_argument("a lut is 256 x 1 (with one channel, or one per channel of the image)");
    if (src.empty()) return image8u();
    if (lut.channels() != 1 && lut.channels() != src.channels())
        throw std::invalid_argument("a lut has one channel or as many as the image");
    image8u out(src.copy());  // throws invalid_argument for more than 4 channels
    const int lc = lut.channels();
    std::vector<std::uint8_t> tables(std::size_t(lc) * 256);  // one contiguous table per channel
    for (int k = 0; k < lc; ++k)
        for (int v = 0; v < 256; ++v) tables[std::size_t(k) * 256 + std::size_t(v)] = lut.data()[std::size_t(v) * std::size_t(lc) + std::size_t(k)];
    apply_tables(out, tables.data(), lc == 1 ? 0 : 256, out.channels());
    return out;
}

// ============================================================================================ adjustments

template <class T>
traits::if_image_t<mat<T>, void> equalize_inplace(mat<T>& img, tone_mode mode) {
    remap_from_histogram(img, mode, [](std::size_t, const auto& s) { return tone_kernel<T>::equalize(s); });
}

template <class T>
traits::if_image_t<mat<T>, void> auto_contrast_inplace(mat<T>& img, float cutoff_percent, tone_mode mode) {
    check_cutoff(cutoff_percent);
    remap_from_histogram(img, mode, [&](std::size_t, const auto& s) { return tone_kernel<T>::auto_contrast(s, cutoff_percent); });
}

template <class T>
traits::if_image_t<mat<T>, void> match_histogram_inplace(mat<T>& img, const mat<T>& reference, tone_mode mode) {
    using kernel = tone_kernel<T>;
    if (reference.empty()) throw std::invalid_argument("the reference is empty");
    if (img.empty()) return;
    if (reference.channels() != img.channels())
        throw std::invalid_argument("the reference must have the same number of channels as the image");
    const plan p = make_plan(img.channels(), mode);
    const std::vector<typename kernel::stats> refs = kernel::analyse(reference, p);
    kernel::check_reference(refs);
    remap_from_histogram(img, mode, [&](std::size_t i, const auto& s) { return kernel::match(s, refs[i]); });
}

template <class T>
traits::if_image_t<mat<T>, void> gamma_inplace(mat<T>& img, float g) {
    check_gamma(g);
    tone_kernel<T>::gamma(img, g);
}

template <class T>
traits::if_image_t<mat<T>, void> levels_inplace(mat<T>& img, float in_black, float in_white, float g, float out_black,
                                                float out_white) {
    check_levels(in_black, in_white, g, out_black, out_white);
    tone_kernel<T>::levels(img, in_black, in_white, g, out_black, out_white);
}

// The element types libstb is built for (tone_kernel<T> is defined for these two). The templates are declared in
// stb/tone.hpp without a body, so these lines are what the rest of the program links to.
template void equalize_inplace<std::uint8_t>(mat<std::uint8_t>&, tone_mode);
template void equalize_inplace<float>(mat<float>&, tone_mode);
template void auto_contrast_inplace<std::uint8_t>(mat<std::uint8_t>&, float, tone_mode);
template void auto_contrast_inplace<float>(mat<float>&, float, tone_mode);
template void match_histogram_inplace<std::uint8_t>(mat<std::uint8_t>&, const mat<std::uint8_t>&, tone_mode);
template void match_histogram_inplace<float>(mat<float>&, const mat<float>&, tone_mode);
template void gamma_inplace<std::uint8_t>(mat<std::uint8_t>&, float);
template void gamma_inplace<float>(mat<float>&, float);
template void levels_inplace<std::uint8_t>(mat<std::uint8_t>&, float, float, float, float, float);
template void levels_inplace<float>(mat<float>&, float, float, float, float, float);

}  // namespace stb
