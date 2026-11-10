#!/usr/bin/env python3
"""Generated inputs of the docs site. Standard library only.

  prepare.py snippets   snippets/<lang>/<name>.<ext>  ->  src/generated/snippets.json
  prepare.py check      fail when a lesson snippet exists in some languages but not in all
  prepare.py bench      benchmarks/outputs/benchmark.json  ->  src/generated/benchmark.json (slim)
  prepare.py symbols    Doxygen tag file + Sphinx objects.inv + api-map.json  ->  src/generated/symbols.json
  prepare.py assets     sample pictures used by the lessons  ->  static/img
  prepare.py api        Doxygen and Sphinx HTML  ->  static/api/{cpp,python}/html
  prepare.py figures    run the Python snippets and write their output to static/img/generated/<name>
  prepare.py all        snippets, check, bench, assets, symbols, api

Missing inputs (docs not built yet, no benchmark file) never stop the site from building:
the matching file is written empty and a warning is printed.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import zlib
import xml.etree.ElementTree as ET
from pathlib import Path

SITE = Path(__file__).resolve().parent.parent
ROOT = SITE.parents[1]
GEN = SITE / "src" / "generated"
LANGS = json.loads((SITE / "languages.json").read_text(encoding="utf-8"))["languages"]


def warn(msg: str) -> None:
    print(f"warning: {msg}", file=sys.stderr)


def write_json(name: str, data) -> None:
    GEN.mkdir(parents=True, exist_ok=True)
    (GEN / name).write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote src/generated/{name}")


def snippet_files() -> dict[str, dict[str, Path]]:
    """name -> {language id -> file}"""
    found: dict[str, dict[str, Path]] = {}
    for lang in LANGS:
        for f in sorted((SITE / "snippets" / lang["id"]).glob(f"*.{lang['ext']}")):
            found.setdefault(f.stem, {})[lang["id"]] = f
    return found


def cmd_snippets(_a) -> None:
    out = {n: {l: p.read_text(encoding="utf-8").rstrip("\n") for l, p in files.items()}
           for n, files in snippet_files().items()}
    write_json("snippets.json", out)


def cmd_check(_a) -> None:
    ids = [l["id"] for l in LANGS]
    bad = [(n, [i for i in ids if i not in files]) for n, files in snippet_files().items()
           if set(files) != set(ids)]
    for n, missing in bad:
        print(f"snippet '{n}' has no version for: {', '.join(missing)}", file=sys.stderr)
    if bad:
        sys.exit(1)
    print(f"{len(snippet_files())} snippet(s), all present in: {', '.join(ids)}")


def cmd_bench(_a) -> None:
    src = ROOT / "benchmarks" / "outputs" / "benchmark.json"
    if not src.exists():
        warn(f"{src} not found; the Bench component will show nothing")
        write_json("benchmark.json", {"sections": [], "libraries": {}})
        return
    d = json.loads(src.read_text(encoding="utf-8"))
    keep = ("key", "title", "note", "libs", "reference", "n_compared", "time_ratio")
    slim = {
        "generated": d.get("generated"),
        "repeats": d.get("repeats"),
        "n_entries": d.get("n_entries"),
        "libraries": d.get("libraries", {}),
        "system": {k: d.get("system", {}).get(k) for k in ("cpu_model", "logical_cores", "os")},
        "sections": [{"id": s["id"], "title": s["title"],
                      "ops": [{k: o.get(k) for k in keep} for o in s["ops"]]} for s in d["sections"]],
    }
    write_json("benchmark.json", slim)


def docs_out(args) -> Path:
    return Path(args.docs_out or os.environ.get("LIBSTB_DOCS_OUT") or ROOT / "build" / "docs" / "out")


def read_tagfile(path: Path) -> dict[str, str]:
    """Qualified C++ name -> path relative to docs/out/cpp/html. First overload wins."""
    out: dict[str, str] = {}
    for comp in ET.parse(path).getroot().iter("compound"):
        cname, cfile = comp.findtext("name"), comp.findtext("filename")
        if not cname or not cfile:
            continue
        if comp.get("kind") in ("class", "struct", "namespace"):
            out.setdefault(cname, cfile)
        for mem in comp.findall("member"):
            mname, afile, anchor = mem.findtext("name"), mem.findtext("anchorfile"), mem.findtext("anchor")
            if mname and afile:
                out.setdefault(f"{cname}::{mname}", f"{afile}#{anchor}" if anchor else afile)
    return out


def read_objects_inv(path: Path) -> dict[str, str]:
    """Sphinx name -> path relative to docs/out/python/html."""
    raw = path.read_bytes()
    parts = raw.split(b"\n", 4)  # four text header lines, then the zlib part
    out: dict[str, str] = {}
    line_re = re.compile(r"(.+?)\s+(\S+:\S+)\s+(-?\d+)\s+(\S*)\s+(.*)")
    for line in zlib.decompress(parts[4]).decode("utf-8").splitlines():
        m = line_re.match(line)
        if m:
            name, _role, _prio, uri, _disp = m.groups()
            out.setdefault(name, uri.replace("$", name) if uri.endswith("$") else uri)
    return out


def cmd_symbols(a) -> None:
    base = docs_out(a)
    cpp, py = {}, {}
    tag, inv = base / "cpp" / "libstb.tag", base / "python" / "html" / "objects.inv"
    if tag.exists():
        cpp = read_tagfile(tag)
    else:
        warn(f"{tag} not found (build stb_docs_doxygen); C++ links will be plain text")
    if inv.exists():
        py = read_objects_inv(inv)
    else:
        warn(f"{inv} not found (build stb_docs_sphinx); Python links will be plain text")

    amap = {k: v for k, v in json.loads((SITE / "api-map.json").read_text(encoding="utf-8")).items()
            if not k.startswith("_")}
    result, missing = {}, []
    for key, names in amap.items():
        entry = {}
        if names.get("cpp") in cpp:
            entry["cpp"] = f"api/cpp/html/{cpp[names['cpp']]}"
        elif cpp:
            missing.append(f"{key}: C++ '{names.get('cpp')}'")
        if names.get("python") in py:
            entry["python"] = f"api/python/html/{py[names['python']]}"
        elif py:
            missing.append(f"{key}: Python '{names.get('python')}'")
        result[key] = entry
    for m in missing:
        warn(f"symbol not found in the reference, shown without link: {m}")
    write_json("symbols.json", result)


def cmd_assets(_a) -> None:
    src = ROOT / "data" / "images" / "astronaut.png"
    dst = SITE / "static" / "img" / "astronaut.png"
    if src.exists():
        shutil.copyfile(src, dst)
        print(f"copied {src.relative_to(ROOT)}")
    else:
        warn(f"{src} not found")


def cmd_api(a) -> None:
    base = docs_out(a)
    for lang in ("cpp", "python"):
        src, dst = base / lang / "html", SITE / "static" / "api" / lang / "html"
        if src.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True)
            print(f"copied {lang} reference")
        else:
            warn(f"{src} not found; the {lang} reference link in the navbar will 404 until docs are built")


def cmd_figures(a) -> None:
    env = dict(os.environ)
    if a.python_path:
        env["PYTHONPATH"] = a.python_path + os.pathsep + env.get("PYTHONPATH", "")
    for name, files in snippet_files().items():
        if "python" not in files:
            continue
        out = SITE / "static" / "img" / "generated" / name
        subprocess.run([sys.executable, str(files["python"]), str(ROOT / "data"), str(out)], env=env, check=True)
        print(f"figures for '{name}' -> {out.relative_to(SITE)}")


def cmd_all(a) -> None:
    cmd_snippets(a); cmd_check(a); cmd_bench(a); cmd_assets(a); cmd_symbols(a); cmd_api(a)


COMMANDS = {"snippets": cmd_snippets, "check": cmd_check, "bench": cmd_bench, "symbols": cmd_symbols,
            "assets": cmd_assets, "api": cmd_api, "figures": cmd_figures, "all": cmd_all}


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=COMMANDS)
    p.add_argument("--docs-out", help="docs/out of the CMake build (default: $LIBSTB_DOCS_OUT or build/docs/out)")
    p.add_argument("--python-path", help="directory holding the built libstb package (for figures)")
    args = p.parse_args()
    COMMANDS[args.command](args)


if __name__ == "__main__":
    main()
