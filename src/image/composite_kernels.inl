// Internal: the vector kernels of composite_row.cpp, included once per instruction set (inside namespace sse2 and
// namespace avx2) after that namespace has defined `vec`, `lanes` (pixels per vector), STB_K_ATTR (the function
// attributes: `inline`, or the AVX2 target) and the lane operations below. Never include it anywhere else.
//
// A pixel's four channels are 16-bit lanes (two pixels per register half: `lo` and `hi`), where x * a never leaves 16
// bits for valid premultiplied pixels, so mullo / add / shift give exactly the integers byte_mul and mix255 give on
// two 32-bit lanes: every instruction set gives the same results as the scalar code.

STB_K_ATTR vec finish(vec t) {  // (t + (t >> 8) + 0x80) >> 8
    return vsrl8(vadd(vadd(t, vsrl8(t)), vset16(0x80)));
}
STB_K_ATTR vec mul16(vec x, vec a) { return finish(vmullo(x, a)); }
STB_K_ATTR vec mix16(vec x, vec a, vec y, vec b) { return finish(vadd(vmullo(x, a), vmullo(y, b))); }
STB_K_ATTR vec inv16(vec x) { return vsub(vset16(255), x); }

// Two (or four) pixels: D, S as 16-bit lanes, CA the coverage and IA = 255 - CA in every lane; the new destination.
template <composite_op Op, bool Full, bool Solid>
STB_K_ATTR vec half(vec D, vec S, vec CA, vec IA) {
    if constexpr (Op == composite_op::clear) {
        return mul16(D, IA);
    } else if constexpr (Op == composite_op::source) {
        if constexpr (Solid && !Full) return vadd(mul16(S, CA), mul16(D, IA));
        else return mix16(S, CA, D, IA);
    } else if constexpr (Op == composite_op::destination) {
        return D;
    } else if constexpr (Op == composite_op::source_over) {
        const vec s = Full ? S : mul16(S, CA);
        return vadd(s, mul16(D, inv16(valpha(s))));
    } else if constexpr (Op == composite_op::destination_over) {
        const vec s = Full ? S : mul16(S, CA);
        return vadd(D, mul16(s, inv16(valpha(D))));
    } else if constexpr (Op == composite_op::source_in) {
        return mix16(Full ? S : mul16(S, CA), valpha(D), D, IA);
    } else if constexpr (Op == composite_op::destination_in) {
        const vec a = Full ? valpha(S) : vadd(mul16(valpha(S), CA), IA);
        return mul16(D, a);
    } else if constexpr (Op == composite_op::source_out) {
        return mix16(Full ? S : mul16(S, CA), inv16(valpha(D)), D, IA);
    } else if constexpr (Op == composite_op::destination_out) {
        const vec a = Full ? inv16(valpha(S)) : vadd(mul16(inv16(valpha(S)), CA), IA);
        return mul16(D, a);
    } else if constexpr (Op == composite_op::source_atop) {
        const vec s = Full ? S : mul16(S, CA);
        return mix16(s, valpha(D), D, inv16(valpha(s)));
    } else if constexpr (Op == composite_op::destination_atop) {
        const vec s = Full ? S : mul16(S, CA);
        const vec a = Full ? valpha(s) : vadd(valpha(s), IA);
        return mix16(D, a, s, inv16(valpha(D)));
    } else if constexpr (Op == composite_op::exclusive_or) {
        const vec s = Full ? S : mul16(S, CA);
        return mix16(s, inv16(valpha(D)), D, inv16(valpha(s)));
    } else {  // plus_lighter
        const vec r = vmin(vadd(S, D), vset16(255));
        return Full ? r : mix16(r, CA, D, IA);
    }
}

// `lanes` packed pixels in, `lanes` packed pixels out.
template <composite_op Op, bool Full, bool Solid>
STB_K_ATTR vec compute(vec d, vec s, vec CA, vec IA) {
    const vec z = vzero();
    return vpack(half<Op, Full, Solid>(vunpacklo(d, z), vunpacklo(s, z), CA, IA),
                 half<Op, Full, Solid>(vunpackhi(d, z), vunpackhi(s, z), CA, IA));
}

// ---------------------------------------------------------------- blend modes (source_over, the closed-form modes)

// x * y / 255 as the scalar mul255 of composite_row.cpp rounds it, (x * y + 127) / 255: not what finish() gives for 24
// of the 65536 pairs, so the modes cannot use mul16 and stay equal to the scalar code. With u = x * y + 127 (below
// 2^16 as an unsigned lane), u / 255 == (u + 1 + (u >> 8)) >> 8 exactly.
STB_K_ATTR vec mulq(vec x, vec y) {
    const vec u = vadd(vmullo(x, y), vset16(127));
    return vsrl8(vadd(vadd(u, vset16(1)), vsrl8(u)));
}

// The colour channels of mode M of S over D (S already scaled by the coverage), one value per 16-bit lane, as
// mode_value gives them; the alpha lane holds garbage that mode_half replaces.
template <blend_mode M>
STB_K_ATTR vec mode_colour(vec D, vec s, vec as, vec ab) {
    const vec ias = inv16(as), iab = inv16(ab);
    if constexpr (M == blend_mode::multiply) {
        return vadd(vadd(mulq(s, iab), mulq(D, ias)), mulq(s, D));
    } else if constexpr (M == blend_mode::screen) {
        return vsub(vadd(s, D), mulq(s, D));
    } else if constexpr (M == blend_mode::darken) {
        return vsub(vadd(s, D), vmax(mulq(s, ab), mulq(D, as)));
    } else if constexpr (M == blend_mode::lighten) {
        return vsub(vadd(s, D), vmin(mulq(s, ab), mulq(D, as)));
    } else if constexpr (M == blend_mode::difference) {
        const vec m = vmin(mulq(s, ab), mulq(D, as));
        return vsub(vsub(vadd(s, D), m), m);
    } else if constexpr (M == blend_mode::exclusion) {
        const vec m = mulq(s, D);
        return vsub(vsub(vadd(s, D), m), m);
    } else if constexpr (M == blend_mode::add) {
        return vadd(vadd(mulq(s, iab), mulq(D, ias)), vmin(mulq(as, ab), vadd(mulq(D, as), mulq(s, ab))));
    } else {  // overlay: multiply where the backdrop is dark (2 cb <= ab), screen where it is light
        const vec base = vadd(mulq(s, iab), mulq(D, ias));
        const vec m = mulq(s, D);
        const vec low = vadd(vadd(base, m), m);
        const vec k = vsub(vadd(D, D), ab);
        const vec high = vsub(vadd(vadd(base, mulq(s, ab)), mulq(as, k)), mulq(s, k));
        const vec light = vcmpgt(vadd(D, D), ab);
        return vor(vand(light, high), vandnot(light, low));
    }
}

// Two (or four) pixels: the new destination, as mode_over computes it.
template <blend_mode M, bool Full>
STB_K_ATTR vec mode_half(vec D, vec S, vec CA) {
    const vec s = Full ? S : mul16(S, CA);
    const vec as = valpha(s), ab = valpha(D);
    const vec ar = vsub(vadd(as, ab), mulq(as, ab));
    const vec c = vmin(vmax(mode_colour<M>(D, s, as, ab), vzero()), ar);  // a premultiplied channel never exceeds the alpha
    return vor(vand(valphamask(), ar), vandnot(valphamask(), c));
}

template <blend_mode M, bool Full>
STB_K_ATTR vec compute_mode(vec d, vec s, vec CA) {
    const vec z = vzero();
    return vpack(mode_half<M, Full>(vunpacklo(d, z), vunpacklo(s, z), CA),
                 mode_half<M, Full>(vunpackhi(d, z), vunpackhi(s, z), CA));
}
