#!/usr/bin/env python3
"""Generate the landing page (index.html) from docs/index.html.in.

Reads docs/supported_langs.json (the language registry) plus, per language,
  <folder>/index-docs.json     the API-reference row (title, badge, description, meta_label, link_href, link_label)
  <folder>/index-install.txt   HTML for the install panel
  <folder>/index-example.txt   HTML for the quick-example panel
and fills the @DOCS_TABS@ / @DOCS_CONTENT@ / @INSTALL_TABS@ / @INSTALL_CONTENT@ / @EXAMPLE_TABS@ /
@EXAMPLE_CONTENT@ placeholders, plus @PROJECT_VERSION@, @LOGO_CONTENT@, @FAVICON@, @LANG_LIST@,
@BINDING_LIST@ and @BINDING_COUNT@ (the last three derived from the registry's optional "role":
"tool", "core" or "binding").

Code blocks in the fragments are written as <pre data-lang="python" data-label="python">...</pre>
holding HTML-escaped source. This script gives each one its header (language label and Copy button)
and colours it with the small tokenizer below, so the page needs no highlighting script and no network.

Invoked by docs/CreateDocs.cmake. Can also be run directly:

    python3 generate_index.py --index-in index.html.in --output /tmp/site/index.html \\
        --langs-file supported_langs.json --langs-dir . --project-version 0.5.0 \\
        --logo-svg images/libstb-sq.svg --favicon assets/libstb-sq.svg
"""
import argparse
import html
import json
import re
import sys
from pathlib import Path

PANELS = ("docs", "install", "example")
ROLES = ("tool", "core", "binding")
DOC_FIELDS = ("title", "badge", "description", "meta_label", "link_href", "link_label")

# ---------------------------------------------------------------- highlighting --

PY_KW = ("import from as def class return with for in if elif else while try except finally raise lambda "
         "and or not is pass yield None True False")
CPP_KW = ("int unsigned long short char bool float double void auto const constexpr static inline namespace using "
          "class struct template typename return if else for while switch case break continue new delete nullptr "
          "true false try catch throw include std")
CMAKE_KW = "include if else endif foreach endforeach function endfunction set"

TOKENIZERS = {}


def _tok(name, kw, comment, extra=()):
    kw_re = r"\b(?:%s)\b" % "|".join(kw.split()) if kw else None
    parts = [("c", comment), ("s", r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')]
    parts += list(extra)
    parts.append(("n", r"\b\d[\d.]*\b"))
    if kw_re:
        parts.append(("k", kw_re))
    parts.append(("f", r"\b[A-Za-z_][\w:]*(?=\()"))
    # group names must be unique; the first letter is the token class (c, s, n, k, f)
    TOKENIZERS[name] = re.compile("|".join(f"(?P<{k}{i}>{v})" for i, (k, v) in enumerate(parts)))


_tok("python", PY_KW, r"#[^\n]*")
_tok("cpp", CPP_KW, r"//[^\n]*", extra=[("k", r"#\s*include\s*<[^>\n]*>")])
_tok("cmake", CMAKE_KW, r"#[^\n]*")
_tok("bash", "", r"#[^\n]*", extra=[("k", r"^\$|\bpip\b|\bcmake\b")])


def highlight(code: str, lang: str) -> str:
    rx = TOKENIZERS.get(lang)
    if rx is None:
        return html.escape(code)
    out, pos = [], 0
    for m in rx.finditer(code):
        out.append(html.escape(code[pos:m.start()]))
        out.append(f'<span class="tok-{m.lastgroup[0]}">{html.escape(m.group())}</span>')
        pos = m.end()
    out.append(html.escape(code[pos:]))
    return "".join(out)


PRE_RE = re.compile(r'<pre data-lang="([\w+-]+)"(?: data-label="([^"]*)")?>(.*?)</pre>', re.S)


def decorate_code(fragment: str) -> str:
    def repl(m):
        lang, label, body = m.group(1), m.group(2) or m.group(1), m.group(3)
        code = html.unescape(body).strip("\n")
        return (
            '<div class="code"><div class="code-head"><span>%s</span>'
            '<button class="copy-btn" type="button" onclick="copyCode(this)">Copy</button></div>'
            '<pre><code>%s</code></pre></div>' % (html.escape(label), highlight(code, lang))
        )
    return PRE_RE.sub(repl, fragment)


# ------------------------------------------------------------------- assembly --

DOC_ROW = """<div class="ref">
    <h3>{title}<span>{badge}</span></h3>
    <div><p>{description}</p><span class="meta">{meta_label}</span></div>
    <a class="go" href="{link_href}">{link_label} &rarr;</a>
</div>
"""


def oxford_join(items):
    if len(items) <= 2:
        return " and ".join(items)
    return ", ".join(items[:-1]) + ", and " + items[-1]


def language_placeholders(languages):
    for lang in languages:
        role = lang.get("role", "binding")
        if role not in ROLES:
            sys.exit(f"generate_index.py: unknown role {role!r} for {lang['folder']!r} (expected one of {ROLES})")
    names = [l["name"] for l in languages if l.get("role", "binding") != "tool"]
    bindings = [l["name"] for l in languages if l.get("role", "binding") == "binding"]
    return {"@LANG_LIST@": oxford_join(names), "@BINDING_LIST@": oxford_join(bindings),
            "@BINDING_COUNT@": str(len(bindings))}


def tab_button(panel, folder, name, active):
    cls = "tab-btn active" if active else "tab-btn"
    return (f'<button class="{cls}" type="button" data-tab="{panel}-{folder}" '
            f"onclick=\"switchTab('{panel}','{folder}')\">{html.escape(name)}</button>\n")


def tab_content(panel, folder, inner, active):
    cls = "tab-content active" if active else "tab-content"
    return f'<div class="{cls}" id="{panel}-{folder}">\n{inner}</div>\n'


def read(path):
    try:
        return Path(path).read_text(encoding="utf-8")
    except OSError as exc:
        sys.exit(f"generate_index.py: failed to read {path}: {exc}")


def load_docs_row(lang_dir):
    path = lang_dir / "index-docs.json"
    try:
        data = json.loads(read(path))
    except json.JSONDecodeError as exc:
        sys.exit(f"generate_index.py: {path} is not valid JSON: {exc}")
    missing = [f for f in DOC_FIELDS if f not in data]
    if missing:
        sys.exit(f"generate_index.py: {path} is missing field(s): {', '.join(missing)}")
    return DOC_ROW.format(**{f: data[f] for f in DOC_FIELDS})


def build(args):
    languages = json.loads(read(args.langs_file))["languages"]
    tabs = {p: [] for p in PANELS}
    content = {p: [] for p in PANELS}
    for i, lang in enumerate(languages):
        folder, name, active = lang["folder"], lang["name"], i == 0
        lang_dir = Path(args.langs_dir) / folder
        for p in PANELS:
            tabs[p].append(tab_button(p, folder, name, active))
        content["docs"].append(tab_content("docs", folder, load_docs_row(lang_dir), active))
        for p in ("install", "example"):
            frag = decorate_code(read(lang_dir / f"index-{p}.txt"))
            content[p].append(tab_content(p, folder, frag, active))

    text = read(args.index_in)
    for p in PANELS:
        text = text.replace(f"@{p.upper()}_TABS@", "".join(tabs[p]))
        text = text.replace(f"@{p.upper()}_CONTENT@", "".join(content[p]))
    for key, value in language_placeholders(languages).items():
        text = text.replace(key, value)
    text = text.replace("@PROJECT_VERSION@", args.project_version)
    text = text.replace("@LOGO_CONTENT@", read(args.logo_svg) if args.logo_svg else "")
    text = text.replace("@FAVICON@", args.favicon)
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--index-in", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--langs-file", required=True)
    ap.add_argument("--langs-dir", required=True)
    ap.add_argument("--project-version", required=True)
    ap.add_argument("--logo-svg", default="")
    ap.add_argument("--favicon", default="assets/libstb-sq.svg")
    args = ap.parse_args()
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(build(args), encoding="utf-8")
    print(f"-- Route index created at: {out}")


if __name__ == "__main__":
    main()
