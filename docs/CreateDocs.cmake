# CreateDocs.cmake - the landing page
# Called via: cmake -P CreateDocs.cmake
# Required CMake variables (passed via -D):
#   OUTPUT_DIR, INDEX_IN, PROJECT_VERSION, LOGO_SQ,
#   ASSETS_DIR   (docs/assets: the one stylesheet, its fonts, the logos and the benchmark teaser script)
#   SHOWCASE_SCRIPT (docs/make_showcase.py: writes the frames of the live demo with libstb itself)
#   LIBSTB_PYTHON_INSTALL_DIR (optional: the built module, put on PYTHONPATH for SHOWCASE_SCRIPT)
#   LANGS_FILE (docs/supported_langs.json), LANGS_DIR (docs/, holding each
#   <folder>/index-docs.json + index-install.txt + index-example.txt; see
#   supported_langs.json's own "_comment" field for the format),
#   GENERATE_SCRIPT (docs/generate_index.py)
#
# The tab panels are built by GENERATE_SCRIPT, a small Python helper: reading JSON and templating
# HTML fought CMake's own string()/file(STRINGS) more than it needed to, and this project already
# requires Python3 for its Sphinx docs, so there is no new dependency.

cmake_minimum_required(VERSION 3.19)

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

# The stylesheet, fonts, logos and teaser script all live in assets/, next to each other, in every page.
file(COPY "${ASSETS_DIR}" DESTINATION "${OUTPUT_DIR}")
if(DEFINED LOGO_SQ)
  file(COPY_FILE "${LOGO_SQ}" "${OUTPUT_DIR}/assets/libstb-sq.svg")
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${GENERATE_SCRIPT}"
          --index-in        "${INDEX_IN}"
          --output          "${OUTPUT_DIR}/index.html"
          --langs-file      "${LANGS_FILE}"
          --langs-dir       "${LANGS_DIR}"
          --project-version "${PROJECT_VERSION}"
          --logo-svg        "${LOGO_SQ}"
          --favicon         "assets/libstb-sq.svg"
  RESULT_VARIABLE GENERATE_RESULT
)
if(NOT GENERATE_RESULT EQUAL 0)
  message(FATAL_ERROR "generate_index.py failed (exit ${GENERATE_RESULT})")
endif()

# The hero's live demo: every frame is written by libstb itself at build time (never committed).
# Needs libstb importable (LIBSTB_PYTHON_INSTALL_DIR, the freshly built module, is put on PYTHONPATH)
# and network access for the photo; without either the page simply hides the demo.
if(DEFINED SHOWCASE_SCRIPT)
  set(_showcase_env "")
  if(LIBSTB_PYTHON_INSTALL_DIR AND EXISTS "${LIBSTB_PYTHON_INSTALL_DIR}")
    set(_showcase_env ${CMAKE_COMMAND} -E env "PYTHONPATH=${LIBSTB_PYTHON_INSTALL_DIR}")
  endif()
  execute_process(
    COMMAND ${_showcase_env} "${Python3_EXECUTABLE}" "${SHOWCASE_SCRIPT}" --out "${OUTPUT_DIR}/showcase"
    RESULT_VARIABLE SHOWCASE_RESULT OUTPUT_QUIET ERROR_VARIABLE SHOWCASE_ERR
  )
  if(NOT SHOWCASE_RESULT EQUAL 0)
    message(WARNING "make_showcase.py failed, the landing page will have no live demo:\n${SHOWCASE_ERR}")
  endif()
endif()
