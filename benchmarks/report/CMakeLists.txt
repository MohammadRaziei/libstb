# ------------------------------------------------------------- report --
# outputs/index.html is the standalone report. Chart.js and KaTeX are linked from cdnjs at fixed
# versions (see CHARTJS_URL and KATEX_BASE in generate_report.py); nothing is downloaded at build
# time. It is built straight from outputs/results/*.json. outputs/benchmark.json is a small digest of
# the same results (gather.py, standard library only): the docs landing page reads it in the browser
# to draw its teaser, so the report itself does not depend on it.
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
  DEPENDS
    "${LIBSTB_BENCH_VENV_STAMP}"
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
add_custom_target(stb_bench_report DEPENDS "${REPORT_HTML}" "${REPORT_JSON}")
