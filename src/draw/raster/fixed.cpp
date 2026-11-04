#include "draw/raster/fixed.hpp"

namespace stb::detail {

namespace {

constexpr std::uint64_t trig_scale = 0xDBD95B16ULL;  // the CORDIC gain, 1 / 0.607252935
constexpr int trig_safe_msb = 29;
constexpr int trig_max_iters = 23;

constexpr fixed arctan_table[] = {1740967, 919879, 466945, 234379, 117304, 58666, 29335, 14668,
                                  7334,    3667,   1833,   917,    458,    229,   115,   57,
                                  29,      14,     7,      4,      2,      1};

// The index of the highest set bit of a 32-bit value (-1 for 0).
int msb32(std::uint32_t x) noexcept {
    if (x == 0) return -1;
    int n = 0;
    if (x >= (1u << 16)) n += 16, x >>= 16;
    if (x >= (1u << 8)) n += 8, x >>= 8;
    if (x >= (1u << 4)) n += 4, x >>= 4;
    if (x >= (1u << 2)) n += 2, x >>= 2;
    if (x >= (1u << 1)) n += 1;
    return n;
}

// Moves the sign of a negative value into `sign`.
void move_sign(fixed& v, int& sign) noexcept {
    if (v < 0) {
        v = -v;
        sign = -sign;
    }
}

fixed trig_downscale(fixed val) noexcept {
    const fixed s = val;
    val = fx_abs(val);
    const std::int64_t v = std::int64_t(val * std::int64_t(trig_scale)) + 0x100000000LL;
    val = fixed(v >> 32);
    return s >= 0 ? val : -val;
}

// Scales the vector so that its largest component has its highest bit at trig_safe_msb; returns the
// shift that was applied (negative: the vector was shifted down).
int trig_prenorm(fvec& v) noexcept {
    const fixed x = v.x;
    const fixed y = v.y;
    int shift = msb32(std::uint32_t(fx_abs(x) | fx_abs(y)));
    if (shift <= trig_safe_msb) {
        shift = trig_safe_msb - shift;
        v.x = fixed(std::uint64_t(x) << shift);
        v.y = fixed(std::uint64_t(y) << shift);
    } else {
        shift -= trig_safe_msb;
        v.x = x >> shift;
        v.y = y >> shift;
        shift = -shift;
    }
    return shift;
}

void trig_pseudo_rotate(fvec& vec, fixed theta) noexcept {
    fixed x = vec.x;
    fixed y = vec.y;
    while (theta < -angle_pi4) {
        const fixed t = y;
        y = -x;
        x = t;
        theta += angle_pi2;
    }
    while (theta > angle_pi4) {
        const fixed t = -y;
        y = x;
        x = t;
        theta -= angle_pi2;
    }
    const fixed* arctan = arctan_table;
    fixed b = 1;
    for (int i = 1; i < trig_max_iters; b <<= 1, ++i) {
        const fixed v1 = (y + b) >> i;
        const fixed v2 = (x + b) >> i;
        if (theta < 0) {
            const fixed t = x + v1;
            y = y - v2;
            x = t;
            theta += *arctan++;
        } else {
            const fixed t = x - v1;
            y = y + v2;
            x = t;
            theta -= *arctan++;
        }
    }
    vec.x = x;
    vec.y = y;
}

// Turns (x, y) into (length * gain, angle).
void trig_pseudo_polarize(fvec& vec) noexcept {
    fixed theta;
    fixed x = vec.x;
    fixed y = vec.y;
    if (y > x) {
        if (y > -x) {
            theta = angle_pi2;
            const fixed t = y;
            y = -x;
            x = t;
        } else {
            theta = y > 0 ? angle_pi : -angle_pi;
            x = -x;
            y = -y;
        }
    } else {
        if (y < -x) {
            theta = -angle_pi2;
            const fixed t = -y;
            y = x;
            x = t;
        } else {
            theta = 0;
        }
    }
    const fixed* arctan = arctan_table;
    fixed b = 1;
    for (int i = 1; i < trig_max_iters; b <<= 1, ++i) {
        const fixed v1 = (y + b) >> i;
        const fixed v2 = (x + b) >> i;
        if (y > 0) {
            const fixed t = x + v1;
            y = y - v2;
            x = t;
            theta += *arctan++;
        } else {
            const fixed t = x - v1;
            y = y + v2;
            x = t;
            theta -= *arctan++;
        }
    }
    // Round the angle to a multiple of 32 (the CORDIC result is only that exact).
    if (theta >= 0)
        theta = (theta + 16) & ~fixed(31);
    else
        theta = -((-theta + 16) & ~fixed(31));
    vec.x = x;
    vec.y = theta;
}

fvec unit_raw(fixed angle) noexcept {
    fvec v{fixed(trig_scale >> 8), 0};
    trig_pseudo_rotate(v, angle);
    return v;
}

void rotate(fvec& vec, fixed angle) noexcept {
    if (vec.x == 0 && vec.y == 0) return;
    fvec v = vec;
    int shift = trig_prenorm(v);
    trig_pseudo_rotate(v, angle);
    v.x = trig_downscale(v.x);
    v.y = trig_downscale(v.y);
    if (shift > 0) {
        const std::int32_t half = std::int32_t(1) << (shift - 1);
        vec.x = (v.x + half - (v.x < 0)) >> shift;
        vec.y = (v.y + half - (v.y < 0)) >> shift;
    } else {
        shift = -shift;
        vec.x = fixed(std::uint64_t(v.x) << shift);
        vec.y = fixed(std::uint64_t(v.y) << shift);
    }
}

}  // namespace

fixed mul_fix(fixed a, fixed b) noexcept {
    int s = 1;
    move_sign(a, s);
    move_sign(b, s);
    const fixed c = fixed((std::int64_t(a) * b + 0x8000) >> 16);
    return s > 0 ? c : -c;
}

fixed mul_div(fixed a, fixed b, fixed c) noexcept {
    int s = 1;
    move_sign(a, s);
    move_sign(b, s);
    move_sign(c, s);
    const fixed d = c > 0 ? fixed((std::int64_t(a) * b + (c >> 1)) / c) : 0x7FFFFFFF;
    return s > 0 ? d : -d;
}

fixed div_fix(fixed a, fixed b) noexcept {
    int s = 1;
    move_sign(a, s);
    move_sign(b, s);
    const fixed q = b > 0 ? fixed(((std::uint64_t(a) << 16) + std::uint64_t(b >> 1)) / std::uint64_t(b)) : 0x7FFFFFFF;
    return s < 0 ? -q : q;
}

fixed atan2_fix(fixed dx, fixed dy) noexcept {
    if (dx == 0 && dy == 0) return 0;
    fvec v{dx, dy};
    trig_prenorm(v);
    trig_pseudo_polarize(v);
    return v.y;
}

fixed cos_fix(fixed angle) noexcept {
    const fvec v = unit_raw(angle);
    return (v.x + 0x80) >> 8;
}

fixed sin_fix(fixed angle) noexcept { return cos_fix(angle_pi2 - angle); }

fixed tan_fix(fixed angle) noexcept {
    const fvec v = unit_raw(angle);
    return div_fix(v.y, v.x);
}

fixed angle_diff(fixed from, fixed to) noexcept {
    fixed delta = to - from;
    while (delta <= -angle_pi) delta += angle_2pi;
    while (delta > angle_pi) delta -= angle_2pi;
    return delta;
}

fvec unit_vector(fixed angle) noexcept {
    const fvec v = unit_raw(angle);
    return {(v.x + 0x80) >> 8, (v.y + 0x80) >> 8};
}

fvec from_polar(fixed len, fixed angle) noexcept {
    fvec v{len, 0};
    rotate(v, angle);
    return v;
}

fixed length(fvec v) noexcept {
    if (v.x == 0) return fx_abs(v.y);
    if (v.y == 0) return fx_abs(v.x);
    const int shift = trig_prenorm(v);
    trig_pseudo_polarize(v);
    v.x = trig_downscale(v.x);
    if (shift > 0) return (v.x + (fixed(1) << (shift - 1))) >> shift;
    return fixed(std::uint32_t(v.x) << -shift);
}

}  // namespace stb::detail
