# ------------------------------------------------------------- report --
# There is no HTML report any more. This step only gathers outputs/results/*.json into
# outputs/benchmark.json, a digest with every number and every chart series the docs site
# needs (gather.py, standard library only). The page itself is drawn by the docs site
# (docs/site, Docusaurus) from benchmark.json, and the raw results/*.json are offered there
# for download.
set(REPORT_JSON "${LIBSTB_BENCH_OUTPUT_DIR}/benchmark.json")

file(MAKE_DIRECTORY "${LIBSTB_BENCH_OUTPUT_DIR}")

add_custom_command(
  OUTPUT "${REPORT_JSON}"
  # -W ignore::RuntimeWarning:site: see the comment on VENV_PYTHON in
  # python/CMakeLists.txt for why every venv-python invocation carries this.
  COMMAND "${LIBSTB_BENCH_VENV_PYTHON}" -W "ignore::RuntimeWarning:site"
          "${CMAKE_CURRENT_SOURCE_DIR}/gather.py" "${LIBSTB_BENCH_OUTPUT_DIR}"
  DEPENDS "${LIBSTB_BENCH_VENV_STAMP}" "${CMAKE_CURRENT_SOURCE_DIR}/gather.py" ${LIBSTB_BENCH_ALL_RESULT_JSONS}
  COMMENT "libstb benchmarks: gathering outputs/results/*.json -> outputs/benchmark.json"
  USES_TERMINAL
)
add_custom_target(stb_bench_report DEPENDS "${REPORT_JSON}")
