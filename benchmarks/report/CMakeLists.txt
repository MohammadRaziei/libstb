# ------------------------------------------------------------- Chart.js --
# Fetched fresh via plain file(DOWNLOAD) from the npm registry (a tarball,
# same as `npm pack` would give you): nothing vendored in the source
# tree, matching the "downloads live in the build dir, not git" rule
# this whole benchmark suite follows.

set(CHARTJS_VERSION "4.5.1" CACHE STRING "Chart.js version to embed in the report")
set(CHARTJS_DL_DIR "${CMAKE_CURRENT_BINARY_DIR}/_chartjs")
set(CHARTJS_ARCHIVE "${CHARTJS_DL_DIR}/chart.js-${CHARTJS_VERSION}.tgz")
set(CHARTJS_EXTRACTED "${CHARTJS_DL_DIR}/extracted")
set(CHARTJS_UMD_MIN "${CHARTJS_EXTRACTED}/package/dist/chart.umd.min.js")

file(MAKE_DIRECTORY "${CHARTJS_DL_DIR}")
file(MAKE_DIRECTORY "${CHARTJS_EXTRACTED}")

add_custom_command(
  OUTPUT "${CHARTJS_UMD_MIN}"
  COMMAND ${CMAKE_COMMAND} -DURL=https://registry.npmjs.org/chart.js/-/chart.js-${CHARTJS_VERSION}.tgz
                            -DDEST=${CHARTJS_ARCHIVE}
                            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Download.cmake"
  COMMAND ${CMAKE_COMMAND} -E tar xzf "${CHARTJS_ARCHIVE}"
          WORKING_DIRECTORY "${CHARTJS_EXTRACTED}"
  COMMENT "libstb benchmarks: fetching Chart.js ${CHARTJS_VERSION} (for the standalone report)"
  USES_TERMINAL
)
add_custom_target(libstb_bench_chartjs DEPENDS "${CHARTJS_UMD_MIN}")

# --------------------------------------------------------------- KaTeX --
# The report writes its formulas in LaTeX and renders them in the browser with KaTeX. Fetched
# the same way as Chart.js (an npm tarball, kept in the build dir, never in git); the
# generator inlines its script, stylesheet and the few fonts it needs, so the report stays one file.

set(KATEX_VERSION "0.16.22" CACHE STRING "KaTeX version to embed in the report")
set(KATEX_DL_DIR "${CMAKE_CURRENT_BINARY_DIR}/_katex")
set(KATEX_ARCHIVE "${KATEX_DL_DIR}/katex-${KATEX_VERSION}.tgz")
set(KATEX_EXTRACTED "${KATEX_DL_DIR}/extracted")
set(KATEX_DIST "${KATEX_EXTRACTED}/package/dist")
set(KATEX_MIN_JS "${KATEX_DIST}/katex.min.js")

file(MAKE_DIRECTORY "${KATEX_DL_DIR}")
file(MAKE_DIRECTORY "${KATEX_EXTRACTED}")

add_custom_command(
  OUTPUT "${KATEX_MIN_JS}"
  COMMAND ${CMAKE_COMMAND} -DURL=https://registry.npmjs.org/katex/-/katex-${KATEX_VERSION}.tgz
                            -DDEST=${KATEX_ARCHIVE}
                            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Download.cmake"
  COMMAND ${CMAKE_COMMAND} -E tar xzf "${KATEX_ARCHIVE}"
          WORKING_DIRECTORY "${KATEX_EXTRACTED}"
  COMMENT "libstb benchmarks: fetching KaTeX ${KATEX_VERSION} (formulas in the standalone report)"
  USES_TERMINAL
)
add_custom_target(libstb_bench_katex DEPENDS "${KATEX_MIN_JS}")

# ------------------------------------------------------------- report --
# outputs/index.html is the standalone report (Chart.js and KaTeX inlined, one file). It is built
# straight from outputs/results/*.json. outputs/benchmark.json is a small digest of the same
# results (gather.py, standard library only): the docs landing page reads it in the browser to
# draw its teaser, so the report itself does not depend on it.

set(REPORT_HTML "${LIBSTB_BENCH_OUTPUT_DIR}/index.html")
set(REPORT_JSON "${LIBSTB_BENCH_OUTPUT_DIR}/benchmark.json")

file(MAKE_DIRECTORY "${LIBSTB_BENCH_OUTPUT_DIR}")

add_custom_command(
  OUTPUT "${REPORT_HTML}"
  # -W ignore::RuntimeWarning:site: see the comment on VENV_PYTHON in
  # python/CMakeLists.txt for why every venv-python invocation carries this.
  COMMAND "${LIBSTB_BENCH_VENV_PYTHON}" -W "ignore::RuntimeWarning:site"
          "${CMAKE_CURRENT_SOURCE_DIR}/generate_report.py"
          "${LIBSTB_BENCH_RESULT_JSON_DIR}" "${REPORT_HTML}"
          --chartjs-path "${CHARTJS_UMD_MIN}"
          --katex-dir "${KATEX_DIST}"
  DEPENDS
    "${LIBSTB_BENCH_VENV_STAMP}"
    "${CHARTJS_UMD_MIN}"
    "${KATEX_MIN_JS}"
    "${CMAKE_CURRENT_SOURCE_DIR}/generate_report.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/template.html.jinja2"
    ${LIBSTB_BENCH_ALL_RESULT_JSONS}
  COMMENT "libstb benchmarks: rendering the standalone HTML report"
  USES_TERMINAL
)

add_custom_command(
  OUTPUT "${REPORT_JSON}"
  COMMAND "${LIBSTB_BENCH_VENV_PYTHON}" -W "ignore::RuntimeWarning:site"
          "${CMAKE_CURRENT_SOURCE_DIR}/gather.py" "${LIBSTB_BENCH_OUTPUT_DIR}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gather.py" ${LIBSTB_BENCH_ALL_RESULT_JSONS}
  COMMENT "libstb benchmarks: gathering outputs/results/*.json -> outputs/benchmark.json"
  USES_TERMINAL
)
add_custom_target(libstb_bench_report DEPENDS "${REPORT_HTML}" "${REPORT_JSON}")
