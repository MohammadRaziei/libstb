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
