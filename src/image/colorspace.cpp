#include "stb/colorspace.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "stb/parallel.hpp"

namespace stb {
namespace {

using utils::parallel_for;

// A floor of what one core does (ns) per pixel, so that a doubtful case stays on one thread (stb/parallel.hpp).
constexpr double pixel_ns = 4.0;

inline float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }  // NaN passes
inline float minf(float a, float b) noexcept { return a < b ? a : b; }
inline float maxf(float a, float b) noexcept { return a > b ? a : b; }

// ------------------------------------------------------------------------------------------------ the maths
// Every function turns three floats (in 0 to 1, the hue in turns) into three floats.

using conversion = void (*)(const float* in, float* out);

void copy3(const float* in, float* out) noexcept {
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
}

// The hue (in turns, [0, 1)) of a colour whose largest channel is mx and whose range (largest - smallest) is d > 0.
inline float hue_of(float r, float g, float b, float mx, float d) noexcept {
    float h;
    if (mx == r) {
        h = (g - b) / d;
        if (h < 0.0f) h += 6.0f;
    } else if (mx == g) {
        h = (b - r) / d + 2.0f;
    } else {
        h = (r - g) / d + 4.0f;
    }
    h /= 6.0f;
    return h >= 1.0f ? 0.0f : h;
}

// hue, saturation, value from red, green, blue
void rgb_to_hsv(const float* in, float* out) noexcept {
    const float r = in[0], g = in[1], b = in[2];
    const float mx = maxf(r, maxf(g, b)), mn = minf(r, minf(g, b));
    const float d = mx - mn;
    out[0] = d > 0.0f ? hue_of(r, g, b, mx, d) : 0.0f;
    out[1] = mx > 0.0f ? d / mx : 0.0f;
    out[2] = mx;
}

void hsv_to_rgb(const float* in, float* out) noexcept {
    float h = in[0];
    h -= std::floor(h);  // the hue wraps
    const float s = clamp01(in[1]), v = clamp01(in[2]);
    const float h6 = h * 6.0f;
    const int sector = int(h6) % 6;  // h6 is in [0, 6]: 6 (a rounding of a hue just below 1) is red again
    const float f = h6 - std::floor(h6);
    const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (sector) {
        case 0: out[0] = v, out[1] = t, out[2] = p; break;
        case 1: out[0] = q, out[1] = v, out[2] = p; break;
        case 2: out[0] = p, out[1] = v, out[2] = t; break;
        case 3: out[0] = p, out[1] = q, out[2] = v; break;
        case 4: out[0] = t, out[1] = p, out[2] = v; break;
        default: out[0] = v, out[1] = p, out[2] = q; break;
    }
}

// hue, saturation, lightness from red, green, blue
void rgb_to_hsl(const float* in, float* out) noexcept {
    const float r = in[0], g = in[1], b = in[2];
    const float mx = maxf(r, maxf(g, b)), mn = minf(r, minf(g, b));
    const float d = mx - mn;
    const float l = (mx + mn) / 2.0f;
    const float denom = 1.0f - std::fabs(2.0f * l - 1.0f);
    out[0] = d > 0.0f ? hue_of(r, g, b, mx, d) : 0.0f;
    out[1] = (d > 0.0f && denom > 0.0f) ? clamp01(d / denom) : 0.0f;
    out[2] = l;
}

void hsl_to_rgb(const float* in, float* out) noexcept {
    const float s = clamp01(in[1]), l = clamp01(in[2]);
    const float v = l + s * minf(l, 1.0f - l);
    const float hsv[3] = {in[0], v > 0.0f ? 2.0f * (1.0f - l / v) : 0.0f, v};
    hsv_to_rgb(hsv, out);
}

void rgb_to_ycbcr(const float* in, float* out) noexcept {
    const float r = in[0], g = in[1], b = in[2];
    const float y = 0.299f * r + 0.587f * g + 0.114f * b;
    out[0] = clamp01(y);
    out[1] = clamp01(0.5f + (b - y) / 1.772f);
    out[2] = clamp01(0.5f + (r - y) / 1.402f);
}

void ycbcr_to_rgb(const float* in, float* out) noexcept {
    const float y = in[0], cb = in[1] - 0.5f, cr = in[2] - 0.5f;
    const float r = y + 1.402f * cr, b = y + 1.772f * cb;
    const float g = (y - 0.299f * r - 0.114f * b) / 0.587f;
    out[0] = clamp01(r);
    out[1] = clamp01(g);
    out[2] = clamp01(b);
}

conversion to_rgb_of(color_space s) {
    switch (s) {
        case color_space::rgb: return copy3;
        case color_space::hsv: return hsv_to_rgb;
        case color_space::hsl: return hsl_to_rgb;
        case color_space::ycbcr: return ycbcr_to_rgb;
    }
    throw std::invalid_argument("unknown colour space");
}
conversion from_rgb_of(color_space s) {
    switch (s) {
        case color_space::rgb: return copy3;
        case color_space::hsv: return rgb_to_hsv;
        case color_space::hsl: return rgb_to_hsl;
        case color_space::ycbcr: return rgb_to_ycbcr;
    }
    throw std::invalid_argument("unknown colour space");
}

bool has_hue(color_space s) noexcept { return s == color_space::hsv || s == color_space::hsl; }

// --------------------------------------------------------------------------------------- values <-> floats
// In 8 bits the hue has 256 steps for the whole turn and everything else 255 steps.

inline float decode(std::uint8_t v, bool hue) noexcept { return hue ? float(v) / 256.0f : float(v) / 255.0f; }
inline std::uint8_t encode(float v, bool hue) noexcept {
    if (hue) return std::uint8_t(int(v * 256.0f + 0.5f) & 255);  // 256 steps: a hue that rounds up to 1 is 0 again
    v = v > 0.0f ? v : 0.0f;
    v = v < 1.0f ? v : 1.0f;
    return std::uint8_t(v * 255.0f + 0.5f);
}

}  // namespace

template <class T>
traits::if_image_t<mat<T>, void> convert_color_inplace(mat<T>& img, color_space from, color_space to) {
    const conversion into_rgb = to_rgb_of(from), out_of_rgb = from_rgb_of(to);  // throw for an unknown space
    if (img.empty()) return;
    const int c = img.channels();
    if (c != 3 && c != 4) throw std::invalid_argument("a colour conversion needs an image of 3 or 4 channels (RGB or RGBA)");
    if (from == to) return;
    const bool hue_in = has_hue(from), hue_out = has_hue(to);

    parallel_for(std::size_t(img.height()), double(img.width()) * pixel_ns, [&](std::size_t b, std::size_t e) {
        for (std::size_t y = b; y < e; ++y) {
            T* row = img.row(int(y));
            for (int x = 0; x < img.width(); ++x) {
                T* px = row + std::size_t(x) * std::size_t(c);
                float v[3], rgb[3], res[3];
                for (int k = 0; k < 3; ++k) {
                    if constexpr (std::is_same_v<T, std::uint8_t>) {
                        v[k] = decode(px[k], hue_in && k == 0);
                    } else {
                        // the hue wraps (it is made into [0, 1) by hsv_to_rgb); everything else is read as the nearest end
                        v[k] = (hue_in && k == 0) ? px[k] : clamp01(px[k]);
                    }
                }
                if (v[0] != v[0] || v[1] != v[1] || v[2] != v[2]) {  // a NaN (floats only)
                    px[0] = px[1] = px[2] = std::numeric_limits<T>::quiet_NaN();
                    continue;
                }
                into_rgb(v, rgb);
                out_of_rgb(rgb, res);
                for (int k = 0; k < 3; ++k) {
                    if constexpr (std::is_same_v<T, std::uint8_t>) px[k] = encode(res[k], hue_out && k == 0);
                    else px[k] = res[k];
                }
            }
        }
    });
}

template STB_API void convert_color_inplace<std::uint8_t>(mat<std::uint8_t>&, color_space, color_space);
template STB_API void convert_color_inplace<float>(mat<float>&, color_space, color_space);

}  // namespace stb
