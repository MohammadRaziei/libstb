# libstb docs site (lesson layer)

Docusaurus pages on top of the two API references. Doxygen (C++) and Sphinx (Python) stay as they are
and are copied into the site as static pages; this folder only adds the lessons.

```
languages.json            language tabs (one source of truth; first entry is the default tab)
api-map.json              C++ name <-> Python name, resolved to reference URLs by prepare.py
docs/                     lesson pages (MDX), one page per function
snippets/<lang>/<name>.*  real programs; ctest builds and runs them
src/components/           Lang, Compare, Bench, ApiLink
scripts/prepare.py        generated inputs (stdlib only), run `python3 scripts/prepare.py --help`
```

A new language is one entry in `languages.json` and one `snippets/<id>/` folder with a file per lesson.
`prepare.py check` (also a ctest test) fails when a lesson exists in some languages and not in all.

## Run

```
cmake -S . -B build -DSTB_BUILD_DOCS=ON -DSTB_BUILD_TESTS=ON
cmake --build build --target stb_docs_cpp stb_docs_python
cmake --build build --target stb_docs_figures    # pictures shown on the lesson pages
cmake --build build --target stb_docs_site       # -> build/docs/out/site
ctest --test-dir build -R docs_snippet            # snippets compile and run, languages match
```

Or from this folder, while writing: `npm install && npm start`.
Set `DOCS_URL` and `DOCS_BASE_URL` when publishing; the defaults are `http://localhost` and `/`.

## Not verified

Written without running anything, so none of this has been executed:

- `npm install`, `docusaurus build` and `tsc`. The package versions in `package.json` were chosen from memory
  and never installed.
- The CMake changes (`stb_docs_figures`, `stb_docs_site`, the snippet tests, `GENERATE_TAGFILE` in the Doxyfile).
- That the snippets compile and run, and that the names in `api-map.json` exist in the Doxygen tag file
  and Sphinx inventory. An unresolved name is shown without a link and `prepare.py symbols` warns about it.
- That Prism highlights `cpp` and `python` with the default `prism-react-renderer` set.
- The look of the page in light and dark mode, and keyboard focus. The CSS maps the existing
  `libstb-docs.css` tokens by hand.
- Only `prepare.py` was syntax-checked. The benchmark sentences on the Resize page were compared against
  `benchmarks/outputs/benchmark.json` (generated 2026-10-15).
