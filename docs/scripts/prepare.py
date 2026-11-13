#!/usr/bin/env python3
"""Generated inputs of the docs site. Standard library only.

  prepare.py sync       docs/guide.md, docs/adr/*.md and CHANGELOG.md  ->  docs/docs (generated copies; the sources stay where they are)
  prepare.py snippets   snippets/<lang>/<name>.<ext>  ->  src/generated/snippets.json
  prepare.py check      fail when a lesson snippet exists in some languages but not in all
  prepare.py bench      benchmarks/outputs/{benchmark.json,results/*.json}  ->  src/generated/benchmark.json
                        (drawn by the benchmarks page) and static/benchmarks/ (offered for download)
  prepare.py meta       version of the library  ->  src/generated/meta.json
  prepare.py symbols    Doxygen tag file + Sphinx objects.inv + api-map.json  ->  src/generated/symbols.json
  prepare.py assets     logo (also used by the Doxygen and Sphinx pages) and the sample picture
  prepare.py figures    run the Python snippets -> static/img/generated/<name>, and make_showcase.py
                        (the home-page demo) -> static/showcase
  prepare.py tokens     colour and type tokens of docs/assets/libstb-docs.css  ->  src/generated/tokens.css
  prepare.py all        sync, snippets, check, bench, meta, tokens, assets, symbols

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
ROOT = SITE.parent
GEN = SITE / "src" / "generated"
CONTENT = SITE / "docs"  # the Docusaurus docs folder (docs/docs)
REPO_URL = "https://github.com/MohammadRaziei/libstb"
LANGS = json.loads((SITE / "languages.json").read_text(encoding="utf-8"))["languages"]


def warn(msg: str) -> None:
    print(f"warning: {msg}", file=sys.stderr)


def write_json(name: str, data) -> None:
    GEN.mkdir(parents=True, exist_ok=True)
    (GEN / name).write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote src/generated/{name}")


# Pages that already exist elsewhere in the repository are copied into the site, never moved: README.md,
# CHANGELOG.md, handoff.md and the checklist link to them where they are. The copies are generated and git-ignored.
SYNC = [
    {"src": "docs/guide.md", "dst": "guide.md", "strip_h1": True,
     "front": {"title": "Reference guide", "sidebar_position": 3,
               "description": "Every Python and C++ call of libstb, with its rules."}},
    {"src": "CHANGELOG.md", "dst": "changelog.md", "strip_h1": True,
     "front": {"title": "Changelog", "sidebar_position": 9, "description": "Release notes for every libstb version."}},
]


def sync_plan() -> dict:
    """source (posix path from the repository root) -> page path under docs/docs"""
    plan = {item["src"]: item["dst"] for item in SYNC}
    for f in sorted((ROOT / "docs" / "adr").glob("*.md")):
        plan[f"docs/adr/{f.name}"] = f"adr/{f.name}"
    return plan


def rewrite_links(text: str, src: str, plan: dict) -> str:
    """Applies _rewrite_links to the prose only: fenced code blocks (C++ lambdas such as `[&](stb::span_view s)` look
    like links) are left exactly as written."""
    parts = re.split(r"(^```.*?^```[^\n]*$|^~~~.*?^~~~[^\n]*$)", text, flags=re.S | re.M)
    return "".join(p if p.startswith(("```", "~~~")) else _rewrite_links(p, src, plan) for p in parts)


def _rewrite_links(text: str, src: str, plan: dict) -> str:
    """Relative links keep working in the copy: a link to another synced page is re-aimed at that page's copy,
    and a link to anything else in the repository (README.md, NOTICE.md, benchmarks/README.md ...) becomes a GitHub URL."""
    src_dir = (ROOT / src).parent
    dst_dir = (CONTENT / plan[src]).parent

    def fix(m: re.Match) -> str:
        label, target, anchor = m.group(1), m.group(2), m.group(3) or ""
        if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", target):
            return m.group(0)
        resolved = (src_dir / target).resolve()
        try:
            rel = resolved.relative_to(ROOT.resolve()).as_posix()
        except ValueError:
            return m.group(0)
        if rel in plan:
            new = os.path.relpath(CONTENT / plan[rel], dst_dir).replace(os.sep, "/")
        else:
            new = f"{REPO_URL}/{'tree' if resolved.is_dir() else 'blob'}/master/{rel}"
        return f"[{label}]({new}{anchor})"

    return re.sub(r"\[([^\]]*)\]\(([^)#\s]+)(#[^)\s]*)?\)", fix, text)


def cmd_sync(_a) -> None:
    plan = sync_plan()
    front = {item["src"]: item for item in SYNC}
    for src, dst in plan.items():
        text = (ROOT / src).read_text(encoding="utf-8")
        text = rewrite_links(text, src, plan)
        item = front.get(src)
        if item:
            if item.get("strip_h1"):
                text = re.sub(r"^# .*\n+", "", text, count=1)  # the page title comes from the front matter
            fm = "\n".join(f"{k}: {json.dumps(v) if isinstance(v, str) else v}" for k, v in item["front"].items())
            text = f"---\n{fm}\n---\n\n{text}"
        out = CONTENT / dst
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
    (CONTENT / "adr" / "_category_.json").write_text(
        json.dumps({"label": "Design records", "position": 8}) + "\n", encoding="utf-8")
    print(f"synced {len(plan)} page(s) into docs/docs: " + ", ".join(sorted(plan.values())))


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


EMPTY_BENCH = {"generated": None, "repeats": None, "n_entries": 0, "libraries": {},
               "headline": {"rows": [], "install": []}, "sections": [],
               "scaling": {"sides": [], "panels": []}, "fonts": {"font": "", "ops": []},
               "install": [], "verify": None, "system": None}


def cmd_bench(_a) -> None:
    out = ROOT / "benchmarks" / "outputs"
    digest = out / "benchmark.json"
    if not digest.exists():
        warn(f"{digest} not found; the benchmarks page will say there is no data")
        write_json("benchmark.json", EMPTY_BENCH)
        return
    GEN.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(digest, GEN / "benchmark.json")
    print("copied benchmark.json -> src/generated")
    # The raw files stay downloadable next to the page: /benchmarks/benchmark.json and /benchmarks/results/<step>.json
    dst = SITE / "static" / "benchmarks"
    (dst / "results").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(digest, dst / "benchmark.json")
    for f in sorted((out / "results").glob("*.json")):
        shutil.copyfile(f, dst / "results" / f.name)
    print(f"copied benchmark.json and {len(list((dst / 'results').glob('*.json')))} result file(s) -> static/benchmarks")


def library_version() -> str:
    if os.environ.get("LIBSTB_VERSION"):
        return os.environ["LIBSTB_VERSION"]
    try:
        return subprocess.run([sys.executable, str(ROOT / "version.py")], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        warn("could not read the library version (set LIBSTB_VERSION)")
        return ""


def cmd_meta(_a) -> None:
    write_json("meta.json", {"version": library_version()})


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
            entry["cpp"] = f"cpp/html/{cpp[names['cpp']]}"
        elif cpp:
            missing.append(f"{key}: C++ '{names.get('cpp')}'")
        if names.get("python") in py:
            entry["python"] = f"python/html/{py[names['python']]}"
        elif py:
            missing.append(f"{key}: Python '{names.get('python')}'")
        result[key] = entry
    for m in missing:
        warn(f"symbol not found in the reference, shown without link: {m}")
    write_json("symbols.json", result)


def cmd_tokens(_a) -> None:
    """The tokens section of the shared stylesheet (the one the Doxygen and Sphinx pages use), so the site
    has the same colours without a second copy to keep in step."""
    css = (ROOT / "docs" / "assets" / "libstb-docs.css").read_text(encoding="utf-8")
    start, end = css.index("/* == tokens == */"), css.index("/* == chrome == */")
    GEN.mkdir(parents=True, exist_ok=True)
    (GEN / "tokens.css").write_text(css[start:end].rstrip() + "\n", encoding="utf-8")
    print("wrote src/generated/tokens.css")


def cmd_assets(_a) -> None:
    pairs = [
        # The Doxygen and Sphinx pages load the logo from <site>/assets/libstb-sq.svg, so it must be at this address.
        (ROOT / "docs" / "images" / "libstb-sq.svg", SITE / "static" / "assets" / "libstb-sq.svg"),
        (ROOT / "data" / "images" / "astronaut.png", SITE / "static" / "img" / "astronaut.png"),
    ]
    for src, dst in pairs:
        if src.exists():
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            print(f"copied {src.relative_to(ROOT)}")
        else:
            warn(f"{src} not found")


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
    # The home page's live demo: every frame is a file libstb wrote. It downloads one photograph, so a failure only
    # hides the demo (the page checks for the files) and does not stop the build.
    shot = subprocess.run([sys.executable, str(SITE / "scripts" / "make_showcase.py"), "--out",
                           str(SITE / "static" / "showcase")], env=env)
    if shot.returncode != 0:
        warn("make_showcase.py failed: the home page will not show its demo")


def cmd_all(a) -> None:
    cmd_sync(a); cmd_snippets(a); cmd_check(a); cmd_bench(a); cmd_meta(a); cmd_tokens(a); cmd_assets(a); cmd_symbols(a)


COMMANDS = {"sync": cmd_sync, "snippets": cmd_snippets, "check": cmd_check, "bench": cmd_bench, "meta": cmd_meta,
            "tokens": cmd_tokens, "symbols": cmd_symbols, "assets": cmd_assets, "figures": cmd_figures, "all": cmd_all}


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=COMMANDS)
    p.add_argument("--docs-out", help="docs/out of the CMake build (default: $LIBSTB_DOCS_OUT or build/docs/out)")
    p.add_argument("--python-path", help="directory holding the built libstb package (for figures)")
    args = p.parse_args()
    COMMANDS[args.command](args)


if __name__ == "__main__":
    main()
