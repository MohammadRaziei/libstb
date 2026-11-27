# libstb docs site

One page per concept. The code on every page switches between Python and C++ with the control in the top bar.

## Run

```bash
cd docs/site
npm install
npm start          # dev server; runs scripts/prepare.py first
npm run build      # static site in build/
```

`scripts/prepare.py` needs only the Python standard library. It builds `src/generated/snippets.json` from the files in `../../examples/<topic>/<lang>/main.<ext>`, fails when an example exists in one language only or has no `README.md`, and copies the benchmark digest `benchmarks/outputs/benchmark.json` for the benchmarks page and the lesson bars.

## Test the examples

```bash
pip install .                                      # from the repository root
python -m pytest tests/python/test_examples.py     # python and cpp (cpp needs CMake; it is skipped without)
```

It runs every example in a temporary folder and compares the pictures with what its page says; the C++ ones are built with the CMake recipe of `docs/install.mdx` and must produce the same files as the Python ones. A new example needs a check in that file.

## Add a lesson

1. Add `examples/<topic>/python/main.py`, `examples/<topic>/cpp/main.cpp` and `examples/<topic>/README.md`, where `<topic>` is the name of the page with `_` for `-` (`resize.mdx` is `resize`). Same job in both languages, plus a check in `tests/python/test_examples.py`. The README says what it does, how to run it and links to the page; it does not repeat the code. More files in the topic (`examples/<topic>/python/verified.py`, ...) are extras that the page shows below the main one with `<Lang name="<topic>" file="verified" />`; an extra may exist in one language only.
2. Add `docs/<area>/<page>.mdx`, use `<Lang name="<topic>" />` for the code and `<Only lang="cpp">...</Only>` for text that applies to one language. Do not put a heading inside `<Only>`: the table of contents would list it for the other language too. End the page with an "API at a glance" section made with `<Differ rows={[...]} />`.
3. Add the page id to `sidebars.ts`.

Pictures that a page shows are the output of its example: run the example, copy the files to `static/img/<topic>/` and commit them (the `resize` page does this).

## Add a language

One line in `languages.json` and a `<lang>/main.<ext>` file in each topic of `examples/`.

## Publishing

The CI job `build-docs` (`.github/workflows/cmake.yml`) builds the site with `DOCS_URL` and `DOCS_BASE_URL` taken from the repository, and the orchestrator's `deploy-pages` job publishes it, with the coverage dashboard under `/coverage`, at https://mohammadraziei.github.io/libstb/ (ADR-0004). Set Settings, Pages, Source: "GitHub Actions" once, or the address keeps answering 404. Locally the site is at http://localhost:3000/libstb/. There is no separate Pages workflow: two workflows deploying to one site would overwrite each other.

## The Benchmarks page

`/benchmarks` (`src/pages/benchmarks.tsx`, `src/components/benchmarks/`) draws `benchmarks/outputs/benchmark.json`, which `benchmarks/report/gather.py` writes. The lesson bars (`src/components/Bench.tsx`) read the same digest. Colours come from `src/css/tokens.css`.

## Not done yet

- No Doxygen (C++) or Sphinx (Python) reference and so no `ApiLink`; `docs/guide.md` is on GitHub only.
- One lesson (`resize`). The rest of the library is described in the guide and the README.
