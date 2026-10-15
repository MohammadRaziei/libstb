#!/usr/bin/env python3
"""gather.py: merge what every benchmark step measured into outputs/benchmark.json.

    outputs/
      results/<step>.json   <- python/bench_*.py, verify.py, system_info.py: one file per step
      benchmark.json        <- this script
      index.html            <- generate_report.py, drawn from benchmark.json alone

This script measures nothing. It reads results/*.json and computes the numbers the page
shows: which library won the most corpus entries (the reference), the mean per-entry ratio
against it, the trend lines over image size, the output-size ratios of the encoders, the font
and install-footprint tables. generate_report.py then only has to draw them, and the docs
landing page reads the same file in the browser (docs/bench-teaser.js).

Ratios are E[target/ref]: the ratio on EACH corpus entry individually, averaged across
entries (ratio first, then average), against whichever library wins the most comparisons.
See e_ratio() below and README.md ("On fairness").

Usage: gather.py <outputs dir>        (reads <dir>/results/*.json, writes <dir>/benchmark.json)
"""
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path

LIB_LABELS = {
    "libstb": "libstb",
    "libstb_srgb": "libstb (sRGB-correct)",
    "pillow": "Pillow",
    "opencv": "OpenCV",
    "imageio": "imageio",
    "skimage": "scikit-image",
    "numpy": "numpy",
}
LIB_ORDER = list(LIB_LABELS)

RESIZE_NOTE = (
    "libstb, Pillow and scikit-image prefilter when shrinking; OpenCV's INTER_LINEAR and INTER_CUBIC do not "
    "(faster, and they alias), and INTER_AREA is its antialiasing counterpart. \"libstb\" is "
    "<code>Resizer(filter, srgb=False)</code>, the like-for-like pairing; \"libstb (sRGB-correct)\" is the "
    "library's own default, which blends in linear light like a correct resizer should, listed separately so "
    "the price of that is visible. 3-channel images only. scikit-image is skipped above 1 MP (seconds per call)."
)

OP_META = {
    "info": ("info: read the header only, no pixels",
             "Width, height and channel count without decoding anything. Only libstb and Pillow have a "
             "header-only call; OpenCV and imageio do not, so they are absent here."),
    "decode": ("decode: encoded bytes in, pixel array out",
               "Each file was written by Pillow, never by stb. OpenCV has no TGA decoder, so it has no TGA "
               "entries. imageio delegates to Pillow, so it is Pillow plus its own overhead."),
    "load_file": ("load_file: path in, pixel array out",
                  "The same as decode but starting from a path, so file I/O is included: what someone actually types."),
    "encode_png": ("encode PNG: raw pixels in, file bytes out",
                   "zlib level 6 on every side. stb bundles its own small deflate instead of zlib, so the same level "
                   "does not produce the same bytes: read the speed together with the output-size table."),
    "encode_jpg": ("encode JPEG: raw pixels in, file bytes out",
                   "Quality 90 on every side, 3-channel images only. Encoders make different size/quality trades "
                   "at the same setting, so the output-size table matters as much as the time."),
    "encode_bmp": ("encode BMP: raw pixels in, file bytes out",
                   "Uncompressed, so this is mostly memory bandwidth. 3-channel images only."),
    "encode_tga": ("encode TGA: raw pixels in, file bytes out",
                   "Uncompressed on every side (libstb with <code>rle=False</code>, matching Pillow's default). "
                   "OpenCV has no TGA encoder."),
    "resize_down_linear": ("resize, half size, linear (triangle)", RESIZE_NOTE),
    "resize_down_cubic": ("resize, half size, cubic (Catmull-Rom)", RESIZE_NOTE),
    "resize_down_box": ("resize, half size, box / area", RESIZE_NOTE + " scikit-image has no box filter."),
    "resize_up_linear": ("resize, double size, linear (triangle)", RESIZE_NOTE),
    "resize_up_cubic": ("resize, double size, cubic (Catmull-Rom)", RESIZE_NOTE),
    "crop": ("crop: the central half of each side",
             "A new, independent image in every library (an OpenCV slice is a view, so it is copied). "
             "This is mostly memory bandwidth. 3-channel images only."),
    "rotate90": ("rotate: a counter-clockwise quarter turn",
                 "Pillow's transpose(ROTATE_90) and cv2.rotate against libstb's rotate90. A turn reads rows "
                 "and writes columns, so it is about memory access patterns. 3-channel images only."),
    "flip_horizontal": ("flip left to right",
                        "Pillow's transpose(FLIP_LEFT_RIGHT) and cv2.flip against libstb's flip_horizontal. "
                        "3-channel images only."),
    "convert_gray": ("convert: RGB to gray",
                     "libstb and Pillow use the same integer formula and give identical pixels; OpenCV's BGR2GRAY "
                     "rounds slightly differently. libstb runs the fastest SIMD backend of the CPU it is measured on "
                     "(<code>libstb.simd_name()</code>). 3-channel images only."),
    "composite": ("composite: RGBA over RGBA, straight alpha",
                  "An RGBA image laid over its own upside-down copy, against Pillow's <code>Image.alpha_composite</code> "
                  "(OpenCV has no equivalent). The alpha channel is identical; the colour is within one level where the "
                  "pixel is visible. 4-channel images only, so only the graphics images take part."),
}

SECTIONS = [
    {"id": "decoding", "title": "Reading images",
     "lede": "Header-only reads and full decodes, from bytes and from a file, on every corpus entry: "
             "synthetic images of four shapes and real photographs, each stored as png, jpg, bmp and tga.",
     "ops": ["info", "decode", "load_file"]},
    {"id": "encoding", "title": "Writing images",
     "lede": "Raw pixels to file bytes, at matched settings. Speed alone is not the whole story for an "
             "encoder, so each panel also shows how large the output is.",
     "ops": ["encode_png", "encode_jpg", "encode_bmp", "encode_tga"]},
    {"id": "resizing", "title": "Resizing images",
     "lede": "Half size and double size, with linear, cubic and box filters.",
     "ops": ["resize_down_linear", "resize_down_cubic", "resize_down_box", "resize_up_linear", "resize_up_cubic"]},
    {"id": "editing", "title": "Editing images",
     "lede": "Crop, quarter turn, flip, RGB to gray and alpha compositing: the operations libstb added for "
             "working on pixels without leaving the library, against Pillow and OpenCV where they have one.",
     "ops": ["crop", "rotate90", "flip_horizontal", "convert_gray", "composite"]},
]

# The operations the landing page's teaser charts, in this order: (op key, short label)
HEADLINE_OPS = [("decode", "Decode"), ("encode_png", "Encode PNG"), ("encode_jpg", "Encode JPEG"),
                ("resize_down_cubic", "Resize, half size, cubic"), ("convert_gray", "RGB to gray")]

SCALING_TITLES = {
    "decode_png": "decode PNG", "decode_jpg": "decode JPEG", "encode_png": "encode PNG",
    "encode_jpg": "encode JPEG", "resize_down_cubic": "resize, half size, cubic",
}

FONT_OP_TITLES = {
    "open": "open: load the font file, get a font object",
    "measure": "measure: how big would this text be",
    "render": "render: text in, coverage bitmap out (measure + allocate + draw)",
}


def lib_sort_key(lib):
    return LIB_ORDER.index(lib) if lib in LIB_ORDER else 99


def label(lib):
    return LIB_LABELS.get(lib, lib)


def e_ratio(pairs):
    """pairs: [(target, ref)], same units. E[target/ref]: the mean of each pair's OWN ratio,
    not (mean of targets)/(mean of refs) and not a ratio of medians. Entries of very different
    absolute scale (a 64x64 image next to a 2048x2048 one) do not dominate just because
    their raw numbers are bigger."""
    ratios = [t / r for t, r in pairs if r and r > 0]
    return sum(ratios) / len(ratios) if ratios else None


def gmean(values):
    values = [v for v in values if v > 0]
    return math.exp(sum(math.log(v) for v in values) / len(values)) if values else None


def load(path):
    path = Path(path)
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return None


def ok(cell):
    return bool(cell and cell.get("available"))


def entry_label(row, canonical):
    base = f"{row['genre']}/{row['size']}"
    return base if canonical else f"{base}/{row['format']}"


def build_op(key, rows, mem_rows):
    title, note = OP_META[key]
    rows = [r for r in rows if key in r]
    libs = sorted({lib for r in rows for lib, c in r[key].items() if ok(c)}, key=lib_sort_key)
    if not libs:
        return None
    canonical = all(r.get("canonical") for r in rows)
    mem_by_entry = {(m["genre"], m["size"], m["format"]): m.get("libraries", {}) for m in mem_rows}

    wins = {lib: 0 for lib in libs}
    mem_wins = {lib: 0 for lib in libs}
    n_cmp = n_mem_cmp = 0
    time_agg = {lib: {} for lib in libs}      # synthetic entries only, keyed by megapixels
    time_agg_all = {lib: {} for lib in libs}  # every entry (the fallback)
    mem_agg = {lib: {} for lib in libs}
    mem_agg_all = {lib: {} for lib in libs}
    entries = []

    for r in rows:
        cells = [(lib, r[key].get(lib)) for lib in libs]
        cells = [(lib, c) for lib, c in cells if ok(c)]
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
        mem_cells = sorted([(lib, emem.get(lib)) for lib, _ in cells if ok(emem.get(lib))],
                           key=lambda lm: lm[1]["delta_mb"])
        if mem_cells:
            n_mem_cmp += 1
            mem_wins[mem_cells[0][0]] += 1
            for lib, m in mem_cells:
                mem_agg_all[lib].setdefault(round(mpx, 5), []).append(m["delta_mb"])
                if synthetic:
                    mem_agg[lib].setdefault(round(mpx, 5), []).append(m["delta_mb"])

        entries.append({
            "label": entry_label(r, canonical), "mpx": round(mpx, 4),
            "time_ms": {lib: round(c["seconds"] * 1000, 4) for lib, c in cells},
            "mem_mb": {lib: round(m["delta_mb"], 3) for lib, m in mem_cells},
        })

    ref_lib, time_ratio = None, {}
    if n_cmp:
        ref_lib = max(wins.items(), key=lambda kv: kv[1])[0]
        pairs = {lib: [] for lib in libs if lib != ref_lib}
        for r in rows:
            ref_cell = r[key].get(ref_lib)
            if not ok(ref_cell):
                continue
            for lib in pairs:
                c = r[key].get(lib)
                if ok(c):
                    pairs[lib].append((c["seconds"], ref_cell["seconds"]))
        time_ratio = {lib: round(e, 4) for lib, e in ((lib, e_ratio(p)) for lib, p in pairs.items() if p) if e}

    mem_ref, mem_ratio = None, {}
    if n_mem_cmp:
        mem_ref = max(mem_wins.items(), key=lambda kv: kv[1])[0]
        mp = {lib: [] for lib in libs if lib != mem_ref}
        for r in rows:
            emem = mem_by_entry.get((r["genre"], r["size"], r["format"]), {})
            ref_m = emem.get(mem_ref)
            if not ok(ref_m):
                continue
            for lib in mp:
                m = emem.get(lib)
                if ok(m):
                    mp[lib].append((m["delta_mb"], ref_m["delta_mb"]))
        mem_ratio = {lib: round(e, 4) for lib, e in ((lib, e_ratio(p)) for lib, p in mp.items() if p) if e}

    # Output size, for encoders: speed alone would reward writing a worse file.
    size_ref, size_ratio = None, {}
    if key.startswith("encode_"):
        size_ref = "pillow" if "pillow" in libs else libs[0]
        sp = {lib: [] for lib in libs if lib != size_ref}
        for r in rows:
            ref_c = r[key].get(size_ref)
            if not (ok(ref_c) and ref_c.get("out_bytes")):
                continue
            for lib in sp:
                c = r[key].get(lib)
                if ok(c) and c.get("out_bytes"):
                    sp[lib].append((c["out_bytes"], ref_c["out_bytes"]))
        for lib, p in sp.items():
            e = e_ratio(p)
            if e is not None:
                size_ratio[lib] = {"ratio": round(e, 4), "n": len(p)}

    def lines(agg, agg_all, reduce, scale=1.0):
        """One line per library: x = image size, y = reduce(values of every entry of that size).
        Synthetic entries share four sizes across genres, so they average cleanly; real photos each
        have their own odd size and would zig-zag the line, so they are only a fallback."""
        src = agg if any(len(pts) >= 2 for pts in agg.values()) else agg_all
        out = []
        for lib in libs:
            pts = []
            for x, vals in sorted(src[lib].items()):
                y = reduce(vals)
                if y is not None:
                    pts.append({"x": x, "y": round(y * scale, 5)})
            if pts:
                out.append({"lib": lib, "points": pts})
        return out

    time_lines = lines(time_agg, time_agg_all, gmean, 1000.0)
    mem_lines = lines(mem_agg, mem_agg_all, lambda v: sum(v) / len(v))
    # throughput = megapixels / seconds; the megapixel count is shared by the group, so the
    # geometric mean of throughput is mpx / geometric-mean(time)
    tp_lines = [{"lib": s["lib"], "points": [{"x": p["x"], "y": round(p["x"] / (p["y"] / 1000.0), 3)} for p in s["points"]]}
                for s in time_lines]

    return {
        "key": key, "title": title, "note": note, "libs": libs,
        "reference": ref_lib, "wins": wins, "n_compared": n_cmp, "time_ratio": time_ratio,
        "mem_reference": mem_ref, "mem_ratio": mem_ratio, "n_mem_compared": n_mem_cmp,
        "size_reference": size_ref, "size_ratio": size_ratio,
        "trend": {"throughput": tp_lines, "time_ms": time_lines, "memory_mb": mem_lines},
        "entries": entries,
    }


def build_sections(throughput, throughput_memory):
    if not throughput:
        return []
    rows = throughput["results"]
    mem = throughput_memory or {}
    out = []
    for sec in SECTIONS:
        ops = [op for op in (build_op(k, rows, mem.get(k, [])) for k in sec["ops"]) if op]
        if ops:
            out.append({"id": sec["id"], "title": sec["title"], "lede": sec["lede"], "ops": ops})
    return out


def build_scaling(scaling):
    if not scaling:
        return None
    panels = []
    for key, by_lib in scaling["cases"].items():
        series = []
        for lib, s in sorted(by_lib.items(), key=lambda kv: lib_sort_key(kv[0])):
            if not s.get("available") or not s.get("points"):
                continue
            pts = [{"x": round(p["pixels"] / 1e6, 5), "y": round(p["pixels"] / 1e6 / p["seconds"], 3)}
                   for p in s["points"] if p["seconds"] > 0]
            if pts:
                series.append({"lib": lib, "points": pts})
        if series:
            panels.append({"key": key, "title": SCALING_TITLES.get(key, key), "series": series})
    return {"sides": scaling.get("sides", []), "panels": panels}


def fmt_us(x):
    return f"{x:.1f} \u00b5s" if x < 1000 else f"{x / 1000:.2f} ms"


def build_fonts(fonts):
    if not fonts:
        return None
    ops = []
    for op in ("open", "measure", "render"):
        rows, pairs = [], []
        for c in (c for c in fonts["cases"] if c["op"] == op):
            lib = c["libraries"]
            if not (ok(lib.get("libstb")) and ok(lib.get("pillow"))):
                continue
            case = f"{c['px']}px" if c["text"] is None else f"{c['text']} {c['px']}px"
            s, p = lib["libstb"]["seconds"], lib["pillow"]["seconds"]
            pairs.append((s, p))
            rows.append({"case": case, "libstb_us": round(s * 1e6, 3), "pillow_us": round(p * 1e6, 3),
                         "libstb": fmt_us(s * 1e6), "pillow": fmt_us(p * 1e6), "ratio": round(s / p, 4)})
        if rows:
            ops.append({"key": op, "title": FONT_OP_TITLES[op], "rows": rows,
                        "mean_ratio": round(e_ratio(pairs), 4) if e_ratio(pairs) else None})
    return {"font": fonts.get("font", ""), "ops": ops} if ops else None


def size_kb(text):
    try:
        num, unit = text.split()
        return float(num) * {"B": 1 / 1024, "KB": 1, "MB": 1024, "GB": 1024 * 1024}.get(unit, 0)
    except (ValueError, AttributeError):
        return 0.0


def build_sizes(sizes):
    if not sizes:
        return []
    rows = []
    for key, d in sizes.items():
        if not d.get("available"):
            continue
        rows.append({"lib": key, "label": label(key), "version": d.get("version", ""),
                     "own": d.get("size", ""), "total": d.get("total_size", ""),
                     "kb": round(size_kb(d.get("total_size", "")), 1),
                     "n_deps": len(d.get("dependencies", [])), "capability": d.get("capability", "")})
    rows.sort(key=lambda r: r["kb"])
    return rows


def headline(sections, sizes_rows):
    """What the landing page teases: the mean time ratio of each library against libstb, for a few
    operations, when libstb is not the reference the sentence says who is."""
    by_key = {op["key"]: op for sec in sections for op in sec["ops"]}
    rows = []
    for key, short in HEADLINE_OPS:
        op = by_key.get(key)
        if not op or not op["reference"]:
            continue
        # express everything against libstb when it ran: other / libstb
        ref, ratios = op["reference"], dict(op["time_ratio"])
        if "libstb" in op["libs"]:
            if ref == "libstb":
                ratios["libstb"] = 1.0
            elif "libstb" in ratios:
                base = ratios["libstb"]
                ratios = {lib: round(v / base, 4) for lib, v in ratios.items()}
                ratios[ref] = round(1 / base, 4)
                ratios["libstb"] = 1.0
        else:
            ratios[ref] = 1.0
        # scikit-image is only timed up to 1 MP and is orders of magnitude off on resize: it stays in the
        # detailed section, where its sizes are stated, and out of the at-a-glance bars
        ratios = {lib: v for lib, v in ratios.items() if lib != "skimage"}
        rows.append({"key": key, "label": short, "reference": ref, "ratios": ratios})
    return {"rows": rows, "install": [{"lib": r["lib"], "label": r["label"], "total": r["total"],
                                       "kb": r["kb"], "n_deps": r["n_deps"]} for r in sizes_rows]}


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: gather.py <outputs dir>")
    out_dir = Path(sys.argv[1])
    res = out_dir / "results"
    throughput = load(res / "throughput.json")
    throughput_memory = load(res / "throughput_memory.json")
    scaling = load(res / "scaling.json")
    fonts = load(res / "fonts.json")
    sizes = load(res / "sizes.json")
    verify = load(res / "verify.json")
    system = load(res / "system_info.json")

    sections = build_sections(throughput, throughput_memory)
    size_rows = build_sizes(sizes)
    libs = sorted({lib for s in sections for op in s["ops"] for lib in op["libs"]}, key=lib_sort_key)

    data = {
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "repeats": throughput.get("repeats") if throughput else None,
        "n_entries": len(throughput["results"]) if throughput else 0,
        "libraries": {lib: label(lib) for lib in libs},
        "headline": headline(sections, size_rows),
        "sections": sections,
        "scaling": build_scaling(scaling),
        "fonts": build_fonts(fonts),
        "install": size_rows,
        "verify": verify,
        "system": system,
    }
    dest = out_dir / "benchmark.json"
    dest.write_text(json.dumps(data, separators=(",", ":"), ensure_ascii=False), encoding="utf-8")
    print(f"gather: wrote {dest} ({dest.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
