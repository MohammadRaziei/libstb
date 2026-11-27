#!/usr/bin/env python3
"""Prepare the generated inputs of the docs site. Standard library only.

    python3 scripts/prepare.py snippets   build src/generated/snippets.json from ../../examples/<topic>/<lang>/main.<ext>
    python3 scripts/prepare.py check      fail when a topic's main example misses a language, has no README.md or has no page of its name
    python3 scripts/prepare.py bench      copy benchmarks/outputs/benchmark.json to src/generated/ and static/benchmarks/
    python3 scripts/prepare.py all        snippets + check + bench

`bench` takes the digest that benchmarks/report/gather.py writes (benchmarks/outputs/benchmark.json) and the raw results beside it. No digest is fine: the
page then says there is no data yet.

ponytail: no `symbols` and no API links yet: the Doxygen and Sphinx references are not part of this site (see docs/site/README.md).
"""

import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent.parent
EXAMPLES = REPO / "examples"
GITHUB = "https://github.com/mohammadraziei/libstb"
BRANCH = "master"
LANGS = json.loads((ROOT / "languages.json").read_text(encoding="utf-8"))
OUT = ROOT / "src" / "generated"


def collect():
    """{topic: {file: {language id: path}}} from examples/<topic>/<lang>/<file>.<ext>. `main` is the example of the page;
    any other file is an extra that a page shows below it (an extra may exist in one language only)."""
    found = {}
    for lang in LANGS:
        for f in sorted(EXAMPLES.glob(f"*/{lang['id']}/*.{lang['ext']}")):
            found.setdefault(f.parent.parent.name, {}).setdefault(f.stem, {})[lang["id"]] = f
    return found


def cmd_snippets():
    data = {}
    for topic, files in collect().items():
        folder = f"examples/{topic}"
        data[topic] = {
            "readme": f"{GITHUB}/tree/{BRANCH}/{folder}",
            "files": {
                stem: {
                    lang: {
                        "code": f.read_text(encoding="utf-8").rstrip("\n"),
                        "path": f.relative_to(REPO).as_posix(),
                        "url": f"{GITHUB}/blob/{BRANCH}/{f.relative_to(REPO).as_posix()}",
                    }
                    for lang, f in per_lang.items()
                }
                for stem, per_lang in sorted(files.items())
            },
        }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "snippets.json").write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"snippets.json: {len(data)} examples")
    if not data:
        print("warning: no example files found; pages that use <Lang> will show nothing", file=sys.stderr)


EMPTY_BENCH = {"generated": None, "repeats": None, "n_entries": 0, "libraries": {},
               "headline": {"rows": [], "install": []}, "sections": [],
               "scaling": {"sides": [], "panels": []}, "fonts": {"font": "", "ops": []},
               "install": [], "verify": None, "system": None}


def cmd_bench():
    out = REPO / "benchmarks" / "outputs"
    digest = out / "benchmark.json"
    OUT.mkdir(parents=True, exist_ok=True)
    if not digest.exists():
        print(f"warning: {digest} not found; the benchmarks page will say there is no data", file=sys.stderr)
        (OUT / "benchmark.json").write_text(json.dumps(EMPTY_BENCH) + "\n", encoding="utf-8")
        return
    shutil.copyfile(digest, OUT / "benchmark.json")
    # the raw files stay downloadable next to the page: /benchmarks/benchmark.json and /benchmarks/results/<step>.json
    dst = ROOT / "static" / "benchmarks"
    (dst / "results").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(digest, dst / "benchmark.json")
    for f in sorted((out / "results").glob("*.json")):
        shutil.copyfile(f, dst / "results" / f.name)
    print(f"benchmark.json: copied with {len(list((dst / 'results').glob('*.json')))} result file(s)")


def cmd_check():
    ids = [lang["id"] for lang in LANGS]
    pages = {f.stem for f in (ROOT / "docs").rglob("*.mdx")}
    bad = []
    for topic, files in sorted(collect().items()):
        if "main" not in files:
            bad.append(f"  {topic}: no main file")
        else:
            missing = [i for i in ids if i not in files["main"]]
            if missing:
                bad.append(f"  {topic}: main is missing in {', '.join(missing)}")
        if not (EXAMPLES / topic / "README.md").is_file():
            bad.append(f"  {topic}: no README.md")
        if topic.replace("_", "-") not in pages:
            bad.append(f"  {topic}: no page docs/**/{topic.replace('_', '-')}.mdx (a topic has the name of its page)")
    if bad:
        print("examples check failed:\n" + "\n".join(bad), file=sys.stderr)
        return 1
    print("examples check: ok")
    return 0


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "all"
    if cmd == "snippets":
        cmd_snippets()
        return 0
    if cmd == "check":
        return cmd_check()
    if cmd == "bench":
        cmd_bench()
        return 0
    if cmd == "all":
        cmd_snippets()
        cmd_bench()
        return cmd_check()
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
