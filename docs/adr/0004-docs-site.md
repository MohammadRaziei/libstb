# ADR-0004: The documentation site is built like httpp's: `docs/site`, one page per concept, examples as real files

- **Status:** Accepted
- **Decided by:** the project owner ("look at how docs work in httpp; it should be the same for libstb": drop the old `docs/`, copy httpp's, rename, then fix the rest)

## Context

libstb had a Docusaurus home page and guide, a Doxygen (C++) and a Sphinx (Python) reference built by CMake targets (`stb_docs*`), snippets in `docs/snippets/` run by ctest, and a guide copied in by `prepare.py sync`. httpp, the sister project, has one site in `docs/site/` with a different, simpler routine, and the owner wants one routine for both.

## Decision

- The site is `docs/site/` (Docusaurus). One page per concept; the code on a page follows a language switch in the top bar (`languages.json`), and each page ends with an "API at a glance" table (`Differ`).
- The code lives in `examples/<topic>/<lang>/main.<ext>` with a `README.md` in `examples/<topic>/`, never pasted into a page. `scripts/prepare.py check` fails when a topic lacks a language or a README, or has no page of its name.
- `tests/python/test_examples.py` runs every example in a clean folder, builds the C++ one with the CMake recipe read from the install page, requires the same bytes from both languages, and requires them to be the pictures the page shows (`docs/site/static/img/<topic>/`).
- The CI job `build-docs` (in `cmake.yml`) runs the check, the examples test, the type check and the build, and uploads the result as `cmake-documentation`. The orchestrator's `deploy-pages` publishes it with the coverage dashboard under `/coverage`. There is no separate `pages.yml` (as httpp has): two workflows deploying to one Pages site would overwrite each other, and the coverage dashboard would be lost.
- `docs/guide.md`, `docs/adr/` and `docs/images/` stay where they are, because the README, the changelog, the source comments and the tests link to them.
- The benchmarks page is libstb's own (`src/pages/benchmarks.tsx`, drawing `benchmarks/outputs/benchmark.json`); httpp's `Bench` component draws httpp's result format and was not reused.

## Rejected alternatives

- **Keep Doxygen and Sphinx beside the site.** httpp has no reference of this kind, the routine is meant to be the same, and the two builds needed apt packages and a built extension. The cost is that there is no API reference and no `ApiLink` for now.
- **Copy httpp's `pages.yml`.** It would replace the coverage deploy (see above).

## Consequences

- There is one lesson (`resize`); the rest of the library is documented in the guide and the README until its pages are written.
- The home page, the colours (`src/css/tokens.css` keeps libstb's token names for the benchmarks page; `custom.css` is httpp's) have not been checked in a browser here.
- The git history keeps the old `docs/` (Doxygen, Sphinx, snippets, `make_showcase.py`, the old home page).
