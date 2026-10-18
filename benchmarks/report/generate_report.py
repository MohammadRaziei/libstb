"""
generate_report.py: combine every result JSON the benchmark suite wrote
into one standalone HTML report. Chart.js and all the raw JSON data are
embedded directly in the file, so the output needs nothing else to view
or to keep: no server, no network, no sibling files. That is also why
this is the report that lives in benchmarks/outputs/ (next to results/*.json and
benchmark.json): everything else (corpus, venv, scratch) is disposable build output
under build/.

What this file deliberately does NOT contain: how the corpus was
generated, why timing uses best-of-N, or the ru_maxrss/execve gotcha
met while measuring memory. That is methodology, for people reading the
source, not benchmark results for someone skimming a report: it lives in
benchmarks/README.md instead.

Ratios, when computed, are E[target/ref]: the ratio on EACH corpus entry
individually, averaged across entries (ratio-then-average, not
average-then-ratio), against whichever library wins the most
comparisons. See _e_ratio() and README.md ("On fairness").
"""
import argparse
import base64
import datetime
import json
import math
import os
import re
import shutil
import sys

from jinja2 import Environment, FileSystemLoader

HERE = os.path.dirname(os.path.abspath(__file__))

LIB_LABELS = {
    "libstb": "libstb",
    "libstb_srgb": "libstb (sRGB-correct)",
    "pillow": "Pillow",
    "opencv": "OpenCV",
    "imageio": "imageio",
    "skimage": "scikit-image",
    "numpy": "numpy",
}
# Series colours come from the site's own tokens (--s-<lib> in docs/assets/libstb-docs.css), so a library has
# the same colour here as in the landing page's bars. They are filled in by build(); LIB_COLORS holds the
# dark theme's colour of each library and RECOLOR maps each of them to its light theme colour.
LIB_COLORS = {}
RECOLOR = {}
FALLBACK_COLOR = "#858c96"
# Color is never the only way to tell two series apart (colorblind
# readers cannot rely on hue alone): every multi-series chart also
# varies point shape and line dash per library, keyed off these maps.
LIB_POINT_STYLES = {
    "libstb": "circle", "libstb_srgb": "rectRot", "pillow": "triangle",
    "opencv": "rect", "imageio": "star", "skimage": "crossRot",
}
LIB_DASH = {
    "libstb": [], "libstb_srgb": [4, 2], "pillow": [6, 3],
    "opencv": [2, 2], "imageio": [8, 3, 2, 3], "skimage": [1, 3],
}

# ------------------------------------------------------------ operations --

RESIZE_NOTE = (
    "libstb, Pillow and scikit-image prefilter when shrinking; OpenCV's INTER_LINEAR and INTER_CUBIC do not "
    "(faster, and they alias), and INTER_AREA is its antialiasing counterpart. \"libstb\" is "
    "<code>Resizer(filter, srgb=False)</code>, the like-for-like pairing; \"libstb (sRGB-correct)\" is the "
    "library's own default, which blends in linear light like a correct resizer should, listed separately so "
    "the price of that is visible. 3-channel images only. scikit-image is skipped above 1 MP (seconds per call)."
)

OP_META = {
    "info": {
        "title": "info: read the header only, no pixels",
        "note": "Width, height and channel count without decoding anything. Only libstb and Pillow have a "
                "header-only call; OpenCV and imageio do not, so they are absent here.",
    },
    "decode": {
        "title": "decode: encoded bytes in, pixel array out",
        "note": "Each file was written by Pillow, never by stb. OpenCV has no TGA decoder, so it has no TGA "
                "entries. imageio delegates to Pillow, so it is Pillow plus its own overhead.",
    },
    "load_file": {
        "title": "load_file: path in, pixel array out",
        "note": "The same as decode but starting from a path, so file I/O is included: what someone actually types.",
    },
    "encode_png": {
        "title": "encode PNG: raw pixels in, file bytes out",
        "note": "zlib level 6 on every side. stb bundles its own small deflate instead of zlib, so the same level "
                "does not produce the same bytes: read the speed together with the output-size table below.",
    },
    "encode_jpg": {
        "title": "encode JPEG: raw pixels in, file bytes out",
        "note": "Quality 90 on every side, 3-channel images only. Encoders make different size/quality trades "
                "at the same setting, so the output-size table matters as much as the time.",
    },
    "encode_bmp": {
        "title": "encode BMP: raw pixels in, file bytes out",
        "note": "Uncompressed, so this is mostly memory bandwidth. 3-channel images only.",
    },
    "encode_tga": {
        "title": "encode TGA: raw pixels in, file bytes out",
        "note": "Uncompressed on every side (libstb with <code>rle=False</code>, matching Pillow's default). "
                "OpenCV has no TGA encoder.",
    },
    "resize_down_linear": {"title": "resize, half size, linear (triangle)", "note": RESIZE_NOTE},
    "resize_down_cubic": {"title": "resize, half size, cubic (Catmull-Rom)", "note": RESIZE_NOTE},
    "resize_down_box": {"title": "resize, half size, box / area",
                        "note": RESIZE_NOTE + " scikit-image has no box filter."},
    "resize_up_linear": {"title": "resize, double size, linear (triangle)", "note": RESIZE_NOTE},
    "resize_up_cubic": {"title": "resize, double size, cubic (Catmull-Rom)", "note": RESIZE_NOTE},
    "crop": {
        "title": "crop: the central half of each side",
        "note": "A new, independent image in every library (an OpenCV slice is a view, so it is copied). "
                "This is mostly memory bandwidth. 3-channel images only.",
    },
    "rotate90": {
        "title": "rotate: a counter-clockwise quarter turn",
        "note": "Pillow's transpose(ROTATE_90) and cv2.rotate against libstb's rotate90. A turn reads rows "
                "and writes columns, so it is about memory access patterns. 3-channel images only.",
    },
    "flip_horizontal": {
        "title": "flip left to right",
        "note": "Pillow's transpose(FLIP_LEFT_RIGHT) and cv2.flip against libstb's flip_horizontal. 3-channel images only.",
    },
    "convert_gray": {
        "title": "convert: RGB to gray",
        "note": "libstb and Pillow use the same integer formula and give identical pixels; OpenCV's BGR2GRAY "
                "rounds slightly differently. libstb runs the fastest SIMD backend of the CPU it is measured on "
                "(<code>libstb.simd_name()</code>). 3-channel images only.",
    },
    "composite": {
        "title": "composite: RGBA over RGBA, straight alpha",
        "note": "An RGBA image laid over its own upside-down copy, against Pillow's <code>Image.alpha_composite</code> "
                "(OpenCV has no equivalent). The alpha channel is identical; the colour is within one level where the "
                "pixel is visible. 4-channel images only, so only the graphics images take part.",
    },
}

SECTIONS = [
    {"id": "decoding", "kicker": "Decoding", "title": "Reading images",
     "lede": "Header-only reads and full decodes, from bytes and from a file, on every corpus entry: "
             "synthetic images of four shapes and real photographs, each stored as png, jpg, bmp and tga.",
     "ops": ["info", "decode", "load_file"], "dataset": "throughput"},
    {"id": "encoding", "kicker": "Encoding", "title": "Writing images",
     "lede": "Raw pixels to file bytes, at matched settings. Speed alone is not the whole story for an "
             "encoder, so each panel also shows how large the output is.",
     "ops": ["encode_png", "encode_jpg", "encode_bmp", "encode_tga"], "dataset": "throughput"},
    {"id": "resizing", "kicker": "Resizing", "title": "Resizing images",
     "lede": "Half size and double size, with linear, cubic and box filters.",
     "ops": ["resize_down_linear", "resize_down_cubic", "resize_down_box",
             "resize_up_linear", "resize_up_cubic"], "dataset": "throughput"},
    {"id": "editing", "kicker": "Editing", "title": "Editing images",
     "lede": "Crop, quarter turn, flip, RGB to gray and alpha compositing: the operations libstb added for "
             "working on pixels without leaving the library, against Pillow and OpenCV where they have one.",
     "ops": ["crop", "rotate90", "flip_horizontal", "convert_gray", "composite"], "dataset": "throughput"},
]

SCALING_TITLES = {
    "decode_png": "decode PNG", "decode_jpg": "decode JPEG", "encode_png": "encode PNG",
    "encode_jpg": "encode JPEG", "resize_down_cubic": "resize, half size, cubic",
}

FONT_OP_TITLES = {
    "open": "open: load the font file, get a font object",
    "measure": "measure: how big would this text be",
    "render": "render: text in, coverage bitmap out (measure + allocate + draw)",
}


# --------------------------------------------------------------- helpers --

def _label(lib):
    return LIB_LABELS.get(lib, lib)


# Formulas are written in LaTeX between \( and \) (inline only) and rendered in the browser by KaTeX.
# "%s" slots: the quantity symbol (t = time, m = extra peak memory, s = output size) and the
# reference library's name; L stands for each other library the sentence goes on to list.
TEX_E_VS_REF = r"\(\mathbb{E}\!\left[\dfrac{%s_{L}}{%s_{\text{%s}}}\right]\)"
TEX_E_FONT = r"\(\mathbb{E}\!\left[\dfrac{t_{\text{libstb}}}{t_{\text{Pillow}}}\right]\)"


def _tex_e(symbol, ref_label):
    return TEX_E_VS_REF % (symbol, symbol, ref_label)


# Chart.js and KaTeX come from cdnjs, pinned to an exact version (nothing is downloaded at build time and
# nothing is inlined, so the report stays small). Bump the versions here.
CHARTJS_URL = "https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.5.1/chart.umd.min.js"
KATEX_BASE = "https://cdnjs.cloudflare.com/ajax/libs/KaTeX/0.16.22/"


def _e_ratio(pairs):
    """pairs: list of (target, ref), same units, ref > 0. Returns E[target/ref]:
    the mean of each pair's OWN ratio, not (mean of targets)/(mean of refs) and
    not a ratio of medians. Entries of very different absolute scale (a 64x64
    image next to a 2048x2048 one) do not get to dominate just because their
    raw numbers are bigger."""
    ratios = [t / r for t, r in pairs if r and r > 0]
    return sum(ratios) / len(ratios) if ratios else None


def _load(path):
    if path and os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return None


def _fmt_us(x):
    return f"{x:.1f}\u00b5s" if x < 1000 else f"{x / 1000:.2f}ms"


def _chart_js(canvas_id, config):
    return f"mkChart({json.dumps(canvas_id)}, {json.dumps(config)});"


def _bar_config(labels, values, colors, unit):
    return {
        "type": "bar",
        "data": {"labels": labels, "datasets": [{"data": values, "backgroundColor": colors}]},
        "options": {
            "indexAxis": "y", "responsive": True, "maintainAspectRatio": False,
            "scales": {"x": {"title": {"display": True, "text": unit}}, "y": {"grid": {"display": False}}},
            "plugins": {"legend": {"display": False}},
        },
    }


def _series(lib, points, show_line):
    color = LIB_COLORS.get(lib, FALLBACK_COLOR)
    return {
        "label": _label(lib), "data": points, "borderColor": color, "backgroundColor": color,
        "pointStyle": LIB_POINT_STYLES.get(lib, "circle"), "borderDash": LIB_DASH.get(lib, []) if show_line else [],
        "showLine": show_line, "tension": 0.2, "pointRadius": 5 if show_line else 4, "pointHoverRadius": 8,
    }


def _xy_config(datasets, x_title, y_title, y_log, x_log=True):
    return {
        "type": "scatter",
        "data": {"datasets": datasets},
        "options": {
            "responsive": True, "maintainAspectRatio": False,
            "scales": {
                "x": {"type": "logarithmic" if x_log else "linear", "title": {"display": True, "text": x_title}},
                "y": {"type": "logarithmic" if y_log else "linear", "title": {"display": True, "text": y_title}},
            },
            "plugins": {"legend": {"position": "bottom"}},
        },
    }


# ------------------------------------------------------------ operations --

def _build_op(key, meta, rows, mem_rows):
    """One operation's panel: the who-wins summary and the trend charts across all entries."""
    rows = [r for r in rows if key in r]
    libs = sorted({lib for r in rows for lib, c in r[key].items() if c.get("available")},
                  key=lambda lib: list(LIB_LABELS).index(lib) if lib in LIB_LABELS else 99)
    if not libs:
        return None

    mem_by_entry = {(m["genre"], m["size"], m["format"]): m.get("libraries", {}) for m in mem_rows}

    wins = {lib: 0 for lib in libs}
    mem_wins = {lib: 0 for lib in libs}
    n_cmp = n_mem_cmp = 0
    # trend charts: one point per (library, image size), aggregated over the entries of that size
    time_agg = {lib: {} for lib in libs}
    mem_agg = {lib: {} for lib in libs}
    time_agg_all = {lib: {} for lib in libs}
    mem_agg_all = {lib: {} for lib in libs}
    size_pairs = {lib: [] for lib in libs}  # (out_bytes, ref out_bytes) filled below

    for r in rows:
        cells = [(lib, r[key].get(lib)) for lib in libs]
        cells = [(lib, c) for lib, c in cells if c and c.get("available")]
        if not cells:
            continue
        cells.sort(key=lambda lc: lc[1]["seconds"])
        n_cmp += 1
        wins[cells[0][0]] += 1
        mpx = r["pixels"] / 1e6
        synthetic = not r["genre"].startswith("real")
        for lib, c in cells:
            time_agg_all[lib].setdefault(round(mpx, 5), []).append(c["seconds"])
            if synthetic:
                time_agg[lib].setdefault(round(mpx, 5), []).append(c["seconds"])

        emem = mem_by_entry.get((r["genre"], r["size"], r["format"]), {})
        mem_cells = [(lib, emem.get(lib)) for lib, _ in cells]
        mem_cells = sorted([(lib, m) for lib, m in mem_cells if m and m.get("available")],
                           key=lambda lm: lm[1]["delta_mb"])
        if mem_cells:
            n_mem_cmp += 1
            mem_wins[mem_cells[0][0]] += 1
            for lib, m in mem_cells:
                mem_agg_all[lib].setdefault(round(mpx, 5), []).append(m["delta_mb"])
                if synthetic:
                    mem_agg[lib].setdefault(round(mpx, 5), []).append(m["delta_mb"])

    winner_summary, e_ratios, ref_lib = "", {}, None
    if n_cmp:
        ref_lib = max(wins.items(), key=lambda kv: kv[1])[0]
        pairs = {lib: [] for lib in libs if lib != ref_lib}
        for r in rows:
            ref_cell = r[key].get(ref_lib)
            if not (ref_cell and ref_cell.get("available")):
                continue
            for lib in pairs:
                c = r[key].get(lib)
                if c and c.get("available"):
                    pairs[lib].append((c["seconds"], ref_cell["seconds"]))
        e_ratios = {lib: e for lib, e in ((lib, _e_ratio(p)) for lib, p in pairs.items() if p) if e is not None}
        rest = ", ".join(f"{_label(lib)} {e:.1f}\u00d7" for lib, e in sorted(e_ratios.items(), key=lambda kv: kv[1]))
        winner_summary = (
            f"<strong>{_label(ref_lib)}</strong> is the reference (fastest on the most corpus entries). "
            f"On average ({_tex_e('t', _label(ref_lib))}, the mean of each entry's own ratio, not a ratio of medians)"
            + (f": {rest} as long." if rest else ".")
        )

    memory_summary = ""
    if n_mem_cmp:
        mref = max(mem_wins.items(), key=lambda kv: kv[1])[0]
        mp = {lib: [] for lib in libs if lib != mref}
        for r in rows:
            emem = mem_by_entry.get((r["genre"], r["size"], r["format"]), {})
            ref_m = emem.get(mref)
            if not (ref_m and ref_m.get("available")):
                continue
            for lib in mp:
                m = emem.get(lib)
                if m and m.get("available"):
                    mp[lib].append((m["delta_mb"], ref_m["delta_mb"]))
        me = {lib: e for lib, e in ((lib, _e_ratio(p)) for lib, p in mp.items() if p) if e is not None}
        rest = ", ".join(f"{_label(lib)} {e:.1f}\u00d7" for lib, e in sorted(me.items(), key=lambda kv: kv[1]))
        memory_summary = (
            f"<strong>{_label(mref)}</strong> needs the least extra memory on the most entries "
            f"(extra peak RSS on top of what the process held before the operation started; images under "
            f"0.065 MP are not measured)"
            + (f". On average, {_tex_e('m', _label(mref))}: {rest} as much." if rest else ".")
        )

    # Output size, for encoders: speed alone would reward writing a worse file.
    size_rows = []
    if key.startswith("encode_"):
        ref_size_lib = "pillow" if "pillow" in libs else libs[0]
        sp = {lib: [] for lib in libs if lib != ref_size_lib}
        for r in rows:
            ref_c = r[key].get(ref_size_lib)
            if not (ref_c and ref_c.get("available") and ref_c.get("out_bytes")):
                continue
            for lib in sp:
                c = r[key].get(lib)
                if c and c.get("available") and c.get("out_bytes"):
                    sp[lib].append((c["out_bytes"], ref_c["out_bytes"]))
        for lib, p in sp.items():
            e = _e_ratio(p)
            if e is not None:
                size_rows.append({"lib": _label(lib), "ratio": f"{e:.2f}\u00d7", "ref": _label(ref_size_lib), "n": len(p)})

    def _gmean(v):
        v = [x for x in v if x > 0]
        return math.exp(sum(math.log(x) for x in v) / len(v)) if v else None

    def _lines(agg, agg_all, reduce, scale=1.0):
        """One line per library: x = image size, y = reduce(values of every entry of that size).
        Synthetic entries share four sizes across genres, so they average cleanly; real photos each
        have their own odd size and would zig-zag the line, so they are used only as a fallback."""
        src = agg if any(len(pts) >= 2 for pts in agg.values()) else agg_all
        ds = []
        for lib in libs:
            pts = []
            for x, vals in sorted(src[lib].items()):
                y = reduce(vals)
                if y is not None:
                    pts.append({"x": x, "y": round(y * scale, 5)})
            if pts:
                ds.append(_series(lib, pts, True))
        return ds

    time_ds = _lines(time_agg, time_agg_all, _gmean, 1000.0)
    mem_ds = _lines(mem_agg, mem_agg_all, lambda v: sum(v) / len(v))
    # throughput = megapixels / seconds; the megapixel count is shared by the group, so the
    # geometric mean of throughput is mpx / geometric-mean(time)
    tp_ds = []
    for d_ in _lines(time_agg, time_agg_all, _gmean):
        tp_ds.append({**d_, "data": [{"x": p["x"], "y": round(p["x"] / p["y"], 3)} for p in d_["data"]]})

    charts = []
    has_tp = bool(tp_ds)
    if has_tp:
        charts.append(_chart_js(f"chart-tp-{key}", _xy_config(
            tp_ds, "Image size (megapixels, log scale)", "Throughput (MP/s, log scale)", True)))
    has_time = bool(time_ds)
    if has_time:
        charts.append(_chart_js(f"chart-time-trend-{key}", _xy_config(
            time_ds, "Image size (megapixels, log scale)", "Time (ms, log scale)", True)))
    has_mem = bool(mem_ds)
    if has_mem:
        charts.append(_chart_js(f"chart-mem-trend-{key}", _xy_config(
            mem_ds, "Image size (megapixels, log scale)", "Extra peak memory (MB)", False)))

    return {
        "key": key, "title": meta["title"], "note": meta["note"],
        "winner_summary": winner_summary, "memory_summary": memory_summary,
        "ref_lib": ref_lib, "e_ratios": e_ratios, "size_rows": size_rows,
        "trend_chart_js": "\n".join(charts),
        "has_tp": has_tp, "has_time": has_time, "has_mem": has_mem,
    }


def _build_sections(throughput, throughput_memory):
    if not throughput:
        return []
    rows = throughput["results"]
    throughput_memory = throughput_memory or {}
    out = []
    for sec in SECTIONS:
        ops = []
        for key in sec["ops"]:
            op = _build_op(key, OP_META[key], rows, throughput_memory.get(key, []))
            if op:
                ops.append(op)
        if ops:
            out.append({**sec, "ops_built": ops})
    return out


# --------------------------------------------------------------- scaling --

def _build_scaling(scaling):
    if not scaling:
        return {"panels": [], "chart_js": ""}
    panels, js = [], []
    for label, by_lib in scaling["cases"].items():
        ds = []
        for lib, series in by_lib.items():
            if not series.get("available") or not series.get("points"):
                continue
            pts = [{"x": round(p["pixels"] / 1e6, 5), "y": round(p["pixels"] / 1e6 / p["seconds"], 3)}
                   for p in series["points"] if p["seconds"] > 0]
            ds.append(_series(lib, pts, True))
        if not ds:
            continue
        cid = f"chart-scaling-{label}"
        js.append(_chart_js(cid, _xy_config(ds, "Image size (megapixels, log scale)",
                                            "Throughput (MP/s, log scale)", True)))
        panels.append({"id": cid, "title": SCALING_TITLES.get(label, label)})
    return {"panels": panels, "chart_js": "\n".join(js), "sides": scaling.get("sides", [])}


# ----------------------------------------------------------------- fonts --

def _build_fonts(fonts):
    if not fonts:
        return {"ops": [], "chart_js": "", "font": ""}
    ops, js = [], []
    for op in ("open", "measure", "render"):
        cases = [c for c in fonts["cases"] if c["op"] == op]
        if not cases:
            continue
        labels, rows, pairs = [], [], []
        for c in cases:
            l = c["libraries"]
            if not (l.get("libstb", {}).get("available") and l.get("pillow", {}).get("available")):
                continue
            label = f"{c['px']}px" if c["text"] is None else f"{c['text']} {c['px']}px"
            labels.append(label)
            s, p = l["libstb"]["seconds"], l["pillow"]["seconds"]
            pairs.append((s, p))
            rows.append({"label": label, "libstb": _fmt_us(s * 1e6), "pillow": _fmt_us(p * 1e6),
                         "ratio": f"{s / p:.2f}\u00d7"})
        if not rows:
            continue
        cid = f"chart-font-{op}"
        cfg = {
            "type": "bar",
            "data": {"labels": labels, "datasets": [
                {"label": _label("libstb"), "data": [round(s * 1e6, 3) for s, _ in pairs],
                 "backgroundColor": LIB_COLORS["libstb"]},
                {"label": _label("pillow"), "data": [round(p * 1e6, 3) for _, p in pairs],
                 "backgroundColor": LIB_COLORS["pillow"]},
            ]},
            "options": {"responsive": True, "maintainAspectRatio": False,
                        "scales": {"y": {"type": "logarithmic", "title": {"display": True, "text": "\u00b5s (log scale)"}}},
                        "plugins": {"legend": {"position": "bottom"}}},
        }
        js.append(_chart_js(cid, cfg))
        e = _e_ratio(pairs)
        summary = (f"On average ({TEX_E_FONT}, the mean of each case's own ratio): "
                   f"<strong>{e:.2f}\u00d7</strong>; below 1 means libstb took less time.") if e else ""
        ops.append({"key": op, "title": FONT_OP_TITLES[op], "id": cid, "rows": rows, "summary": summary})
    return {"ops": ops, "chart_js": "\n".join(js), "font": fonts.get("font", "")}


# ------------------------------------------------------------------ sizes --

def _size_kb(text):
    try:
        num, unit = text.split()
        return float(num) * {"B": 1 / 1024, "KB": 1, "MB": 1024, "GB": 1024 * 1024}.get(unit, 0)
    except (ValueError, AttributeError):
        return 0.0


def _build_sizes(sizes):
    if not sizes:
        return []
    rows = []
    for key, d in sizes.items():
        if not d.get("available"):
            continue
        rows.append({
            "label": _label(key), "is_self": key == "libstb", "version": d.get("version", ""),
            "own": d.get("size", ""), "total": d.get("total_size", ""),
            "kb": _size_kb(d.get("total_size", "")), "n_deps": len(d.get("dependencies", [])),
            "capability": d.get("capability", ""),
        })
    rows.sort(key=lambda r: r["kb"])
    return rows


def _build_sizes_chart(size_rows):
    if not size_rows:
        return ""
    lib_by_label = {v: k for k, v in LIB_LABELS.items()}
    labels = [f"{r['label']} ({r['n_deps']} dep{'s' if r['n_deps'] != 1 else ''})" for r in size_rows]
    values = [round(r["kb"] / 1024, 2) for r in size_rows]
    colors = [LIB_COLORS.get(lib_by_label.get(r["label"], ""), FALLBACK_COLOR) for r in size_rows]
    return _chart_js("chart-install-size", _bar_config(labels, values, colors, "MB (wheel + all dependencies)"))


# ------------------------------------------------------------------ build --

REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
SITE_ASSETS = os.path.join(REPO_ROOT, "docs", "assets")


def _series_palettes(site_css):
    """{lib: hex} for the dark and the light theme, from the --s-<lib> tokens: the first block of tokens
    in the stylesheet is the light theme, the one after [data-theme="dark"] is the dark theme."""
    dark_at = site_css.index('[data-theme="dark"]')
    grab = lambda text: {m.group(1): m.group(2).lower() for m in re.finditer(r"--s-(\w+):\s*(#[0-9a-fA-F]{6})", text)}
    return grab(site_css[dark_at:]), grab(site_css[:dark_at])


def _site_chrome(output_path):
    """The docs site's design tokens and top bar (docs/assets/libstb-docs.css, everything above its
    "== base ==" marker comment) and the logo, so this page
    wears the same top bar and type as the landing page. The charts are not touched by any of this."""
    css, logo = "", ""
    try:
        with open(os.path.join(SITE_ASSETS, "libstb-docs.css"), encoding="utf-8") as f:
            text = f.read()
        css = text[: text.index("/* == base == */")]
        with open(os.path.join(REPO_ROOT, "docs", "images", "libstb-sq.svg"), encoding="utf-8") as f:
            logo = f.read().strip()
    except (OSError, ValueError):
        print("generate_report: docs/assets not found, the page keeps a plain top bar", file=sys.stderr)
    return css, logo


def build(results_dir, output_path):
    site_css, logo_svg = _site_chrome(output_path)
    if site_css:
        dark, light = _series_palettes(site_css)
        LIB_COLORS.update(dark)
        RECOLOR.update({dark[lib]: light[lib] for lib in dark if lib in light})
    throughput = _load(os.path.join(results_dir, "throughput.json"))
    throughput_memory = _load(os.path.join(results_dir, "throughput_memory.json"))
    scaling = _load(os.path.join(results_dir, "scaling.json"))
    fonts = _load(os.path.join(results_dir, "fonts.json"))
    sizes = _load(os.path.join(results_dir, "sizes.json"))
    verify = _load(os.path.join(results_dir, "verify.json"))
    system_info = _load(os.path.join(results_dir, "system_info.json"))

    sections = _build_sections(throughput, throughput_memory)
    scl = _build_scaling(scaling)
    fnt = _build_fonts(fonts)
    size_rows = _build_sizes(sizes)
    size_chart_js = _build_sizes_chart(size_rows)

    embedded = {"throughput": throughput, "throughput_memory": throughput_memory, "scaling": scaling,
                "fonts": fonts, "sizes": sizes, "verify": verify, "system_info": system_info}

    chart_scripts = [scl["chart_js"], fnt["chart_js"], size_chart_js]
    for sec in sections:
        for op in sec["ops_built"]:
            chart_scripts.append(op["trend_chart_js"])

    n_entries = len(throughput["results"]) if throughput else 0
    repeats = throughput.get("repeats", "?") if throughput else "?"
    meta = [f"{n_entries} corpus entries", f"{repeats} repeats per cell"]
    if verify:
        meta.append(f"{verify['passed']} output checks passed before timing")

    env = Environment(loader=FileSystemLoader(HERE), autoescape=False)
    # the report is one file, so its icon travels inside it
    favicon = ("data:image/svg+xml;base64," + base64.b64encode(logo_svg.encode("utf-8")).decode("ascii")) if logo_svg else ""
    html = env.get_template("template.html.jinja2").render(
        generated_at=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        headline="libstb, measured honestly",
        subhead=("Image decoding, encoding, resizing and text, each measured on its own against the "
                 "libraries people actually reach for: Pillow, OpenCV, imageio and scikit-image."),
        meta_line=" \u00b7 ".join(meta),
        sections=sections, scaling=scl, fonts=fnt, has_fonts=bool(fnt["ops"]),
        has_scaling=bool(scl["panels"]), has_throughput=bool(sections),
        has_throughput_memory=bool(throughput_memory),
        has_sizes=bool(size_rows), size_rows=size_rows,
        verify=verify, system_info=system_info, repeats=repeats,
        chartjs_url=CHARTJS_URL, katex_base=KATEX_BASE, embedded_json=json.dumps(embedded),
        chart_scripts="\n".join(chart_scripts),
        site_css=site_css, logo_svg=logo_svg, favicon=favicon, recolor=RECOLOR,
    )

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html)
    return output_path


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("results_dir", help="directory holding throughput.json, scaling.json, sizes.json, ...")
    p.add_argument("output", help="path to write the standalone report.html")
    args = p.parse_args()

    out = build(args.results_dir, args.output)
    print(f"generate_report: wrote {out} ({os.path.getsize(out) / 1024:.0f}KB)")
