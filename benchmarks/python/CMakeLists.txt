set(PY_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(VENV_DIR "${CMAKE_CURRENT_BINARY_DIR}/venv")
set(RESULTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/results")
set(SCRATCH_DIR "${CMAKE_CURRENT_BINARY_DIR}/scratch")

if(WIN32)
  set(VENV_PYTHON "${VENV_DIR}/Scripts/python.exe")
else()
  set(VENV_PYTHON "${VENV_DIR}/bin/python")
endif()
# Every invocation below passes -W ignore::RuntimeWarning:site: CMake's
# Ninja generator relativizes absolute in-build-tree paths in generated
# build commands for portability (e.g. "report/../python/venv/bin/python"
# instead of the fully-resolved path). That is functionally identical,
# but it trips a sys.prefix/pyvenv.cfg consistency check Python 3.11+
# added in the frozen `site` module. Scoped to that one module so it
# never hides a real RuntimeWarning from anything else.

file(MAKE_DIRECTORY "${RESULTS_DIR}")
file(MAKE_DIRECTORY "${SCRATCH_DIR}")

# ---------------------------------------------------------------- venv --
# Self-contained: libstb itself is installed from a fresh git checkout
# (LIBSTB_BENCH_GITHUB_URL), not from the local ../../src: this
# directory never imports anything from the parent project.
#
# This runs at CONFIGURE time (execute_process, right here while
# CMakeLists.txt is being processed), not at build time, so
# `cmake -S . -B build` alone leaves you with a ready venv, and
# `cmake --build build --target libstb_benchmark` only has to run the
# actual benchmarks. The venv itself still lives under the build
# directory (${CMAKE_CURRENT_BINARY_DIR}/venv).
#
# Guarded by a stamp file: reconfiguring does NOT reinstall unless the
# venv is missing. To force a rebuild (e.g. after changing
# requirements.txt or LIBSTB_BENCH_GITHUB_URL), delete the stamp file or
# the whole build directory.

set(VENV_STAMP "${CMAKE_CURRENT_BINARY_DIR}/.venv_ready")

if(NOT EXISTS "${VENV_STAMP}")
  message(STATUS "libstb benchmarks: creating venv at ${VENV_DIR} (configure time)...")
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -m venv "${VENV_DIR}"
    RESULT_VARIABLE _libstb_venv_rc
  )
  if(NOT _libstb_venv_rc EQUAL 0)
    message(FATAL_ERROR "libstb benchmarks: failed to create venv at ${VENV_DIR}")
  endif()

  message(STATUS "libstb benchmarks: installing competitors + libstb from ${LIBSTB_BENCH_GITHUB_URL} (configure time)...")
  execute_process(
    COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" -m pip install --upgrade pip --quiet
    RESULT_VARIABLE _libstb_pip_upgrade_rc
  )
  if(NOT _libstb_pip_upgrade_rc EQUAL 0)
    message(FATAL_ERROR "libstb benchmarks: failed to upgrade pip in ${VENV_DIR}")
  endif()

  execute_process(
    COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" -m pip install -r "${PY_DIR}/requirements.txt" --quiet
    RESULT_VARIABLE _libstb_pip_req_rc
  )
  if(NOT _libstb_pip_req_rc EQUAL 0)
    message(FATAL_ERROR "libstb benchmarks: failed to install ${PY_DIR}/requirements.txt")
  endif()

  # libstb builds from source (scikit-build-core + nanobind), so this
  # step needs a C++ compiler and takes a few minutes on a fresh venv.
  execute_process(
    COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" -m pip install --force-reinstall --no-deps "${LIBSTB_BENCH_GITHUB_URL}" --quiet
    RESULT_VARIABLE _libstb_pip_pkg_rc
  )
  if(NOT _libstb_pip_pkg_rc EQUAL 0)
    message(FATAL_ERROR "libstb benchmarks: failed to install libstb from ${LIBSTB_BENCH_GITHUB_URL}")
  endif()

  file(TOUCH "${VENV_STAMP}")
else()
  message(STATUS "libstb benchmarks: venv already present at ${VENV_DIR}, skipping "
                  "(delete ${VENV_STAMP} to force recreation)")
endif()

add_custom_target(libstb_bench_venv)

# -------------------------------------------------------------- corpus --

set(CORPUS_DIR "${SCRATCH_DIR}/corpus")
set(CORPUS_MANIFEST "${CORPUS_DIR}/manifest.json")

add_custom_command(
  OUTPUT "${CORPUS_MANIFEST}"
  COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/corpus.py" "${CORPUS_DIR}"
  DEPENDS "${VENV_STAMP}" "${PY_DIR}/corpus.py"
  COMMENT "libstb benchmarks: generating the synthetic corpus"
  USES_TERMINAL
)
add_custom_target(libstb_bench_corpus DEPENDS "${CORPUS_MANIFEST}")

# --------------------------------------------------------- real corpus --
# LIBSTB_REAL_CORPUS_DIR comes from FetchRealCorpus.cmake (included by
# the parent CMakeLists.txt before add_subdirectory(python)): empty if
# every download failed, in which case this target is skipped and the
# benchmarks just run on the synthetic corpus alone.

set(REAL_CORPUS_IMG_DIR "${SCRATCH_DIR}/real_corpus")
set(REAL_CORPUS_MANIFEST "${REAL_CORPUS_IMG_DIR}/manifest.json")
set(BENCH_MANIFESTS "${CORPUS_MANIFEST}")

if(LIBSTB_REAL_CORPUS_DIR)
  add_custom_command(
    OUTPUT "${REAL_CORPUS_MANIFEST}"
    COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/real_corpus_manifest.py"
            "${LIBSTB_REAL_CORPUS_DIR}" "${REAL_CORPUS_IMG_DIR}" "${REAL_CORPUS_MANIFEST}"
    DEPENDS "${VENV_STAMP}" "${PY_DIR}/real_corpus_manifest.py" "${PY_DIR}/corpus.py"
    COMMENT "libstb benchmarks: converting the real-world photographs to every format"
    USES_TERMINAL
  )
  add_custom_target(libstb_bench_real_corpus DEPENDS "${REAL_CORPUS_MANIFEST}")
  list(APPEND BENCH_MANIFESTS "${REAL_CORPUS_MANIFEST}")
else()
  add_custom_target(libstb_bench_real_corpus
    COMMENT "libstb benchmarks: real corpus unavailable (download failed), skipping"
  )
endif()

# -------------------------------------------------------------- verify --
# Runs every (operation, library) pair on a few tiny images and checks
# the OUTPUT before anything is timed: a speed number for an operation
# that produced the wrong pixels is worse than no number. A failure here
# stops the build.

set(VERIFY_JSON "${RESULTS_DIR}/verify.json")

add_custom_command(
  OUTPUT "${VERIFY_JSON}"
  COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/verify.py" "${VERIFY_JSON}"
  DEPENDS "${VENV_STAMP}" "${PY_DIR}/verify.py" "${PY_DIR}/runners.py" "${PY_DIR}/corpus.py"
  COMMENT "libstb benchmarks: verifying every operation produces correct output"
  USES_TERMINAL
)
add_custom_target(libstb_bench_verify DEPENDS "${VERIFY_JSON}")

# ---------------------------------------------------------------- sizes --

set(SIZES_JSON "${RESULTS_DIR}/sizes.json")

add_custom_command(
  OUTPUT "${SIZES_JSON}"
  COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/bench_sizes.py" "${SIZES_JSON}"
  DEPENDS "${VENV_STAMP}" "${PY_DIR}/bench_sizes.py"
  COMMENT "libstb benchmarks: comparing install footprint (pip-size)"
  USES_TERMINAL
)
add_custom_target(libstb_bench_sizes DEPENDS "${SIZES_JSON}")

# ----------------------------------------------------------- throughput --
# Repeats per (library, corpus entry, operation) cell; the best (min)
# of these is kept. Configurable because "how many repeats is enough" is
# a real tradeoff (noise vs. wall-clock time), not something to bake in;
# 7 is a reasonable default for a quiet machine.

set(LIBSTB_BENCH_REPEATS "7" CACHE STRING
    "Timed repeats per (library, corpus entry, operation) cell in bench_throughput.py; best (min) is kept")

set(THROUGHPUT_JSON "${RESULTS_DIR}/throughput.json")

add_custom_command(
  OUTPUT "${THROUGHPUT_JSON}"
  COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/bench_throughput.py" ${BENCH_MANIFESTS} "${THROUGHPUT_JSON}"
          --repeats "${LIBSTB_BENCH_REPEATS}"
  DEPENDS "${VENV_STAMP}" "${VERIFY_JSON}" "${PY_DIR}/bench_throughput.py" "${PY_DIR}/runners.py" "${CORPUS_MANIFEST}"
          "$<$<BOOL:${LIBSTB_REAL_CORPUS_DIR}>:${REAL_CORPUS_MANIFEST}>"
  COMMENT "libstb benchmarks: decode / encode / resize speed, all libraries (${LIBSTB_BENCH_REPEATS} repeats/cell)"
  USES_TERMINAL
)
add_custom_target(libstb_bench_throughput DEPENDS "${THROUGHPUT_JSON}")
add_dependencies(libstb_bench_throughput libstb_bench_verify libstb_bench_corpus libstb_bench_real_corpus)

# ------------------------------------------------------- throughput memory --
# Memory alongside speed for the SAME operations, libraries and corpus
# entries. One driver script, not one CMake target per (entry,
# operation, library): the driver never opens an image itself (it only
# reads the manifest's metadata and hands each entry to a freshly
# spawned child), so it is exempt from the ru_maxrss/execve gotcha
# explained in README.md. See bench_op_memory_driver.py's docstring.

set(THROUGHPUT_MEMORY_JSON "${RESULTS_DIR}/throughput_memory.json")

add_custom_command(
  OUTPUT "${THROUGHPUT_MEMORY_JSON}"
  COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/bench_op_memory_driver.py" ${BENCH_MANIFESTS} "${THROUGHPUT_MEMORY_JSON}"
  DEPENDS "${VENV_STAMP}" "${VERIFY_JSON}" "${PY_DIR}/bench_op_memory_driver.py" "${PY_DIR}/bench_op_memory_one.py"
          "${PY_DIR}/runners.py" "${CORPUS_MANIFEST}"
          "$<$<BOOL:${LIBSTB_REAL_CORPUS_DIR}>:${REAL_CORPUS_MANIFEST}>"
  COMMENT "libstb benchmarks: peak memory for decode / encode / resize, all libraries"
  USES_TERMINAL
)
add_custom_target(libstb_bench_throughput_memory DEPENDS "${THROUGHPUT_MEMORY_JSON}")
add_dependencies(libstb_bench_throughput_memory libstb_bench_verify libstb_bench_corpus libstb_bench_real_corpus)

# -------------------------------------------------------------- scaling --

set(SCALING_JSON "${RESULTS_DIR}/scaling.json")

add_custom_command(
  OUTPUT "${SCALING_JSON}"
  COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/bench_scaling.py" "${SCALING_JSON}"
          --tmp-dir "${SCRATCH_DIR}/scaling"
  DEPENDS "${VENV_STAMP}" "${VERIFY_JSON}" "${PY_DIR}/bench_scaling.py" "${PY_DIR}/runners.py" "${PY_DIR}/corpus.py"
  COMMENT "libstb benchmarks: time vs image size (0.016 to 16.8 megapixels)"
  USES_TERMINAL
)
add_custom_target(libstb_bench_scaling DEPENDS "${SCALING_JSON}")
add_dependencies(libstb_bench_scaling libstb_bench_verify)

# ---------------------------------------------------------------- fonts --
# Needs a real .ttf (LIBSTB_REAL_FONT, from FetchRealCorpus.cmake). If it
# could not be downloaded the target still exists but does nothing, and
# the report simply has no font section.

set(FONTS_JSON "${RESULTS_DIR}/fonts.json")

if(LIBSTB_REAL_FONT)
  add_custom_command(
    OUTPUT "${FONTS_JSON}"
    COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/bench_fonts.py" "${LIBSTB_REAL_FONT}" "${FONTS_JSON}"
    DEPENDS "${VENV_STAMP}" "${PY_DIR}/bench_fonts.py"
    COMMENT "libstb benchmarks: text (stb_truetype vs FreeType via Pillow)"
    USES_TERMINAL
  )
  add_custom_target(libstb_bench_fonts DEPENDS "${FONTS_JSON}")
else()
  add_custom_target(libstb_bench_fonts
    COMMENT "libstb benchmarks: no font available (download failed), skipping the font benchmark"
  )
endif()

# --------------------------------------------------------- system info --
# What machine this actually ran on: reported plainly in the report
# footer, not folded into any of the other JSON files above.

set(SYSTEM_INFO_JSON "${RESULTS_DIR}/system_info.json")

add_custom_command(
  OUTPUT "${SYSTEM_INFO_JSON}"
  COMMAND "${VENV_PYTHON}" -W "ignore::RuntimeWarning:site" "${PY_DIR}/system_info.py" "${SYSTEM_INFO_JSON}"
  DEPENDS "${VENV_STAMP}" "${PY_DIR}/system_info.py"
  COMMENT "libstb benchmarks: recording machine specs (CPU/RAM/OS)"
  USES_TERMINAL
)
add_custom_target(libstb_bench_system_info DEPENDS "${SYSTEM_INFO_JSON}")

# Expose to the parent scope (report/CMakeLists.txt needs these paths)
set(LIBSTB_BENCH_RESULTS_SCRATCH_DIR "${RESULTS_DIR}" PARENT_SCOPE)
set(LIBSTB_BENCH_VENV_PYTHON "${VENV_PYTHON}" PARENT_SCOPE)
set(LIBSTB_BENCH_VENV_STAMP "${VENV_STAMP}" PARENT_SCOPE)
set(LIBSTB_BENCH_ALL_RESULT_JSONS
    "${THROUGHPUT_JSON}" "${THROUGHPUT_MEMORY_JSON}" "${SCALING_JSON}" "${SIZES_JSON}"
    "${VERIFY_JSON}" "${SYSTEM_INFO_JSON}" "$<$<BOOL:${LIBSTB_REAL_FONT}>:${FONTS_JSON}>"
    PARENT_SCOPE)
