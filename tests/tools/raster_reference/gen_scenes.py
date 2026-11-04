#!/usr/bin/env python3
"""Writes scenes.h: the drawing scenes that tests/cpp/test_raster_reference.cpp and reference.c share.

Every scene is a path (M, L, Q, C, Z commands), a mode (fill non-zero, fill even-odd, stroke), a matrix
and a clip size. reference.c draws them with plutovg (the library libstb's rasterizer and stroker are
ported from) and prints a hash of each coverage image; the test draws the same scenes with libstb and
compares the hashes. Run:

    python3 gen_scenes.py > scenes.h

The numbers are written with 9 significant digits, so C and C++ read back the identical floats.
"""
import math
import random

scenes = []

FILL_NZ, FILL_EO, STROKE = 0, 1, 2
BUTT, ROUND, SQUARE = 0, 1, 2   # caps (and ROUND is also the round join)
MITER, JOIN_ROUND, BEVEL = 0, 1, 2


def scene(name, w, h, mode, ops, width=1.0, cap=BUTT, join=MITER, miter=4.0, dashes=(), doff=0.0,
          m=(1, 0, 0, 1, 0, 0)):
    scenes.append(dict(name=name, w=w, h=h, mode=mode, ops=ops, width=width, cap=cap, join=join,
                       miter=miter, dashes=list(dashes), doff=doff, m=list(m)))


def rect(x, y, w, h):
    return [("M", x, y), ("L", x + w, y), ("L", x + w, y + h), ("L", x, y + h), ("Z",)]


def ellipse(cx, cy, rx, ry):
    k = 0.5522847498
    return [("M", cx, cy - ry),
            ("C", cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy),
            ("C", cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry),
            ("C", cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy),
            ("C", cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry), ("Z",)]


def star(cx, cy, r, n=5, skip=2):
    pts = [(cx + r * math.sin(2 * math.pi * skip * i / n), cy - r * math.cos(2 * math.pi * skip * i / n))
           for i in range(n)]
    return [("M",) + pts[0]] + [("L",) + p for p in pts[1:]] + [("Z",)]


# --- fills
scene("rect_pixel_aligned", 32, 32, FILL_NZ, rect(4, 4, 20, 12))
scene("rect_fractional", 32, 32, FILL_NZ, rect(3.3, 4.7, 17.25, 11.6))
scene("rect_sub_pixel", 16, 16, FILL_NZ, rect(5.25, 5.25, 0.4, 0.4))
scene("circle", 64, 64, FILL_NZ, ellipse(32, 32, 25, 25))
scene("ellipse_offset", 64, 48, FILL_NZ, ellipse(30.4, 22.9, 27.3, 11.1))
scene("star_non_zero", 64, 64, FILL_NZ, star(32, 33, 28))
scene("star_even_odd", 64, 64, FILL_EO, star(32, 33, 28))
scene("two_circles_same_dir_nz", 64, 64, FILL_NZ, ellipse(32, 32, 28, 28) + ellipse(32, 32, 14, 14))
scene("two_circles_same_dir_eo", 64, 64, FILL_EO, ellipse(32, 32, 28, 28) + ellipse(32, 32, 14, 14))
scene("hole_nz", 64, 64, FILL_NZ, ellipse(32, 32, 28, 28) +
      [("M", 32, 18), ("C", 24.3, 18, 18, 24.3, 18, 32), ("C", 18, 39.7, 24.3, 46, 32, 46),
       ("C", 39.7, 46, 46, 39.7, 46, 32), ("C", 46, 24.3, 39.7, 18, 32, 18), ("Z",)])
scene("quad_blob", 48, 48, FILL_NZ, [("M", 5, 40), ("Q", 24, -20, 43, 40), ("Q", 24, 25, 5, 40), ("Z",)])
scene("open_path_fill", 40, 40, FILL_NZ, [("M", 5, 5), ("L", 35, 10), ("L", 20, 35)])
scene("clipped_left_top", 40, 40, FILL_NZ, ellipse(2, 3, 25, 18))
scene("clipped_all_sides", 40, 40, FILL_NZ, ellipse(20, 20, 60, 45))
scene("fully_outside", 40, 40, FILL_NZ, rect(100, 100, 10, 10))
scene("huge_rect", 40, 40, FILL_NZ, rect(-5000, -5000, 10000, 10000))
scene("thin_sliver", 40, 40, FILL_NZ, [("M", 2, 2), ("L", 38, 3.2), ("L", 2, 2.15), ("Z",)])
scene("degenerate_line_fill", 20, 20, FILL_NZ, [("M", 3, 3), ("L", 17, 17), ("Z",)])
scene("tall_thin_vertical", 20, 60, FILL_NZ, rect(8.2, 1.3, 0.9, 55.5))

# --- fills with matrices
scene("rotated_rect", 64, 64, FILL_NZ, rect(-15, -10, 30, 20), m=(0.8660254, 0.5, -0.5, 0.8660254, 32, 32))
scene("non_uniform_scale", 64, 64, FILL_NZ, ellipse(0, 0, 10, 10), m=(3, 0, 0, 1.2, 32, 32))
scene("mirrored", 64, 64, FILL_NZ, star(0, 0, 20), m=(-1, 0, 0, 1, 40, 32))
scene("shear", 64, 64, FILL_NZ, rect(10, 10, 30, 30), m=(1, 0, 0.6, 1, -10, 0))

# --- strokes
poly = [("M", 8, 50), ("L", 30, 10), ("L", 52, 50), ("L", 70, 14)]
for cap_name, cap in (("butt", BUTT), ("round", ROUND), ("square", SQUARE)):
    for join_name, join in (("miter", MITER), ("round", JOIN_ROUND), ("bevel", BEVEL)):
        scene(f"poly_{cap_name}_{join_name}_w8", 80, 64, STROKE, poly, width=8, cap=cap, join=join)
scene("poly_w1", 80, 64, STROKE, poly, width=1)
scene("poly_w0_5", 80, 64, STROKE, poly, width=0.5)
scene("poly_w25_miter", 80, 64, STROKE, poly, width=25)
scene("miter_limit_1", 80, 64, STROKE, poly, width=8, miter=1.0)
scene("miter_limit_2", 80, 64, STROKE, poly, width=8, miter=2.0)
scene("miter_limit_10", 80, 64, STROKE, [("M", 8, 50), ("L", 60, 30), ("L", 8, 20)], width=8, miter=10.0)
scene("sharp_spike", 80, 64, STROKE, [("M", 8, 50), ("L", 70, 32), ("L", 8, 30)], width=6, miter=20)
scene("closed_triangle", 64, 64, STROKE, [("M", 10, 50), ("L", 32, 8), ("L", 54, 50), ("Z",)], width=6)
scene("closed_triangle_round", 64, 64, STROKE, [("M", 10, 50), ("L", 32, 8), ("L", 54, 50), ("Z",)], width=6, join=JOIN_ROUND)
scene("closed_rect_bevel", 64, 64, STROKE, rect(10, 12, 40, 36), width=7, join=BEVEL)
scene("circle_stroke", 64, 64, STROKE, ellipse(32, 32, 24, 24), width=5)
scene("circle_stroke_thin", 64, 64, STROKE, ellipse(32, 32, 24, 24), width=0.8)
scene("circle_stroke_wide", 64, 64, STROKE, ellipse(32, 32, 12, 12), width=30)
scene("ellipse_stroke", 64, 48, STROKE, ellipse(32, 24, 28, 12), width=4, join=JOIN_ROUND)
scene("tight_curve_wide", 64, 64, STROKE, [("M", 10, 32), ("C", 60, 4, 4, 4, 54, 32)], width=14)
scene("tight_curve_wide_round", 64, 64, STROKE, [("M", 10, 32), ("C", 60, 4, 4, 4, 54, 32)], width=14, cap=ROUND, join=JOIN_ROUND)
scene("cusp", 64, 64, STROKE, [("M", 10, 50), ("C", 60, 10, 4, 10, 54, 50)], width=6)
scene("s_curve", 64, 64, STROKE, [("M", 6, 50), ("C", 30, 0, 34, 64, 58, 14)], width=5, cap=ROUND)
scene("quad_stroke", 64, 64, STROKE, [("M", 6, 50), ("Q", 32, -10, 58, 50)], width=5, cap=SQUARE)
scene("zero_length_round", 24, 24, STROKE, [("M", 12, 12), ("L", 12, 12)], width=10, cap=ROUND)
scene("zero_length_square", 24, 24, STROKE, [("M", 12, 12), ("L", 12, 12)], width=10, cap=SQUARE)
scene("zero_length_butt", 24, 24, STROKE, [("M", 12, 12), ("L", 12, 12)], width=10, cap=BUTT)
scene("single_move", 24, 24, STROKE, [("M", 12, 12)], width=10, cap=ROUND)
scene("repeated_points", 40, 40, STROKE, [("M", 5, 5), ("L", 5, 5), ("L", 20, 20), ("L", 20, 20), ("L", 35, 8)], width=6, join=JOIN_ROUND)
scene("backtrack", 60, 30, STROKE, [("M", 5, 15), ("L", 50, 15), ("L", 10, 15)], width=6)
scene("two_subpaths", 60, 40, STROKE, [("M", 5, 8), ("L", 50, 8), ("M", 5, 30), ("L", 50, 33)], width=5, cap=ROUND)
scene("stroke_clipped", 40, 40, STROKE, [("M", -10, 20), ("L", 50, 22)], width=9, cap=SQUARE)
scene("stroke_partly_outside", 40, 40, STROKE, ellipse(40, 40, 30, 30), width=6)
scene("hairline_diagonal", 40, 40, STROKE, [("M", 2, 2), ("L", 38, 30)], width=0.3)
scene("stroke_tiny_width", 40, 40, STROKE, [("M", 2, 2), ("L", 38, 30)], width=0.02)

# --- dashes
line = [("M", 4, 20), ("L", 76, 20)]
scene("dash_simple", 80, 40, STROKE, line, width=4, dashes=[8, 4])
scene("dash_odd", 80, 40, STROKE, line, width=4, dashes=[6])
scene("dash_three", 80, 40, STROKE, line, width=4, dashes=[10, 3, 2], cap=ROUND)
scene("dash_offset", 80, 40, STROKE, line, width=4, dashes=[8, 4], doff=5)
scene("dash_offset_gap", 80, 40, STROKE, line, width=4, dashes=[8, 4], doff=10)
scene("dash_negative_offset", 80, 40, STROKE, line, width=4, dashes=[8, 4], doff=-3)
scene("dash_zero_dot_round", 80, 40, STROKE, line, width=6, dashes=[0, 9], cap=ROUND)
scene("dash_closed", 64, 64, STROKE, rect(10, 10, 44, 44), width=3, dashes=[7, 5])
scene("dash_circle", 64, 64, STROKE, ellipse(32, 32, 24, 24), width=3, dashes=[6, 4], cap=ROUND)
scene("dash_curve", 64, 64, STROKE, [("M", 6, 50), ("C", 30, 0, 34, 64, 58, 14)], width=3, dashes=[5, 3])
scene("dash_quad", 64, 64, STROKE, [("M", 6, 50), ("Q", 32, -10, 58, 50)], width=3, dashes=[4, 4])
scene("dash_subpaths", 60, 40, STROKE, [("M", 5, 8), ("L", 50, 8), ("M", 5, 30), ("L", 50, 33)], width=3, dashes=[6, 3], doff=2)
scene("dash_all_zero", 80, 40, STROKE, line, width=4, dashes=[0, 0])

# --- strokes with matrices (the width scales with the matrix; dashes are cut before it)
scene("stroke_scaled", 64, 64, STROKE, [("M", 2, 2), ("L", 10, 8), ("L", 18, 2)], width=1.5, m=(3, 0, 0, 3, 8, 20), join=JOIN_ROUND)
scene("stroke_non_uniform", 64, 64, STROKE, ellipse(0, 0, 8, 8), width=2, m=(3, 0, 0, 1, 32, 32))
scene("stroke_rotated_dashed", 64, 64, STROKE, [("M", -20, 0), ("L", 20, 0)], width=3, dashes=[5, 3], m=(0.7071, 0.7071, -0.7071, 0.7071, 32, 32))
scene("stroke_flipped", 64, 64, STROKE, [("M", 4, 4), ("L", 30, 10), ("L", 10, 30)], width=4, m=(1, 0, 0, -1, 0, 60))

# --- random fuzz
rng = random.Random(20261102)


def r(lo, hi):
    return round(rng.uniform(lo, hi), 2)


for i in range(240):
    w = h = 48
    ops = [("M", r(-8, 56), r(-8, 56))]
    for _ in range(rng.randint(1, 6)):
        kind = rng.choice("LLQCC")
        if kind == "L":
            ops.append(("L", r(-8, 56), r(-8, 56)))
        elif kind == "Q":
            ops.append(("Q", r(-8, 56), r(-8, 56), r(-8, 56), r(-8, 56)))
        else:
            ops.append(("C",) + tuple(r(-8, 56) for _ in range(6)))
    if rng.random() < 0.5:
        ops.append(("Z",))
    if rng.random() < 0.3:
        ops.append(("M", r(0, 48), r(0, 48)))
        ops.append(("L", r(0, 48), r(0, 48)))
        ops.append(("L", r(0, 48), r(0, 48)))
        ops.append(("Z",))
    mode = rng.choice([FILL_NZ, FILL_EO, STROKE, STROKE])
    m = (1, 0, 0, 1, 0, 0)
    if rng.random() < 0.4:
        a = rng.uniform(0, 6.28)
        s = rng.uniform(0.5, 2)
        m = tuple(round(v, 4) for v in (s * math.cos(a), s * math.sin(a), -s * math.sin(a) * rng.uniform(0.7, 1.3),
                                         s * math.cos(a), r(-10, 10), r(-10, 10)))
    dashes = []
    if mode == STROKE and rng.random() < 0.35:
        dashes = [r(0.5, 8) for _ in range(rng.randint(1, 4))]
    scene(f"fuzz_{i:02d}", w, h, mode, ops, width=r(0.3, 12), cap=rng.choice([BUTT, ROUND, SQUARE]),
          join=rng.choice([MITER, JOIN_ROUND, BEVEL]), miter=rng.choice([1, 2, 4, 10]),
          dashes=dashes, doff=r(-5, 20) if dashes else 0.0, m=m)


def f(v):
    t = "%.9g" % v
    if "." not in t and "e" not in t and "n" not in t:
        t += ".0"
    return t + "f"


print("// Generated by gen_scenes.py: do not edit.")
print("#pragma once")
print("typedef struct ref_op { char op; float v[6]; } ref_op;")
print("typedef struct ref_scene {")
print("    const char* name; int w, h; int mode; float width; int cap, join; float miter;")
print("    const float* dashes; int ndashes; float doff; float m[6]; const ref_op* ops; int nops;")
print("} ref_scene;")
for i, s in enumerate(scenes):
    print(f"static const ref_op ops_{i}[] = {{")
    for op in s["ops"]:
        vals = [f(v) for v in op[1:]] + ["0"] * (6 - (len(op) - 1))
        print("    {'%s', {%s}}," % (op[0], ", ".join(vals)))
    print("};")
    if s["dashes"]:
        print(f"static const float dashes_{i}[] = {{{', '.join(f(v) for v in s['dashes'])}}};")
print("static const ref_scene ref_scenes[] = {")
for i, s in enumerate(scenes):
    d = f"dashes_{i}" if s["dashes"] else "0"
    print('    {"%s", %d, %d, %d, %s, %d, %d, %s, %s, %d, %s, {%s}, ops_%d, %d},' % (
        s["name"], s["w"], s["h"], s["mode"], f(s["width"]), s["cap"], s["join"], f(s["miter"]), d,
        len(s["dashes"]), f(s["doff"]), ", ".join(f(v) for v in s["m"]), i, len(s["ops"])))
print("};")
print(f"static const int ref_scene_count = {len(scenes)};")
