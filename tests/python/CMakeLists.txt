find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pytest, numpy, PIL"
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE LIBSTB_PYDEPS_RESULT
)
if(NOT LIBSTB_PYDEPS_RESULT EQUAL 0)
    message(WARNING "pytest/numpy/Pillow not installed: Python tests skipped (pip install pytest numpy pillow)")
    return()
endif()

# Run from the install dir so `import libstb` finds the freshly built package.
add_test(NAME test_stb_python
    COMMAND ${Python3_EXECUTABLE} -m pytest ${LIBSTB_TEST_SOURCE_DIR}/python -p no:cacheprovider
    WORKING_DIRECTORY ${PYTHON_INSTALL_DIR}
)

add_custom_target(stb_test_python
    COMMAND ${CMAKE_COMMAND} -E echo "-- Running python tests..."
    COMMAND ${Python3_EXECUTABLE} -m pytest ${LIBSTB_TEST_SOURCE_DIR}/python -p no:cacheprovider
    WORKING_DIRECTORY ${PYTHON_INSTALL_DIR}
    USES_TERMINAL
)
add_dependencies(stb_test_python build_python)
add_dependencies(stb_test stb_test_python)

# ── Coverage (coverage.py, reported through lcov-format files) ────────────────
# Measures the Python layer (src/bindings/python/libstb/*.py, installed under
# PYTHON_INSTALL_DIR). The native extension is not instrumented here: the C++ core
# is covered by stb_coverage_cpp. pytest-memray is switched off for this run
# (LIBSTB_NO_MEMRAY): its allocation tracking and coverage.py's tracing do not mix.
execute_process(
    COMMAND ${Python3_EXECUTABLE} -m coverage --version
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE LIBSTB_COVERAGE_CHECK_RESULT
)

if(LIBSTB_COVERAGE_CHECK_RESULT EQUAL 0)
    add_custom_target(stb_coverage_python
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}/html
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}/html

        COMMAND ${CMAKE_COMMAND} -E echo "-- Capturing coverage..."
        COMMAND ${CMAKE_COMMAND} -E env LIBSTB_NO_MEMRAY=1
                ${Python3_EXECUTABLE} -m coverage run
                --source=${PYTHON_INSTALL_DIR}
                --omit="*/tests/*"
                -m pytest ${LIBSTB_TEST_SOURCE_DIR}/python -p no:cacheprovider

        COMMAND ${CMAKE_COMMAND} -E echo "-- Generating coverage.lcov file..."
        COMMAND ${Python3_EXECUTABLE} -m coverage lcov -o ${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}/coverage.lcov

        COMMAND ${CMAKE_COMMAND}
                -DCOV_FILE=${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}/coverage.lcov
                -DPREFIX_PATH=src/bindings/python/
                -P ${PROJECT_SOURCE_DIR}/cmake/FixLCovPaths.cmake

        COMMAND ${CMAKE_COMMAND} -E echo "-- Generating HTML report..."
        COMMAND ${Python3_EXECUTABLE} -m coverage html -d ${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}/html

        COMMAND ${CMAKE_COMMAND} -E echo "-- HTML entry point created here: ${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}/html/index.html"
        COMMAND ${CMAKE_COMMAND} -E echo "-- Coverage summary:"
        COMMAND ${Python3_EXECUTABLE} -m coverage report

        WORKING_DIRECTORY ${PYTHON_INSTALL_DIR}
        USES_TERMINAL
    )
    add_dependencies(stb_coverage_python build_python)
    add_dependencies(stb_coverage stb_coverage_python)

    file(MAKE_DIRECTORY ${LIBSTB_COVERAGE_PYTHON_BINARY_DIR})

    add_custom_command(TARGET stb_coverage_python POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DIFRAME_SRC="html/index.html"
                -DSECTION_TITLE="Python%20Layer"
                -DLOGO_SVG="${PROJECT_SOURCE_DIR}/docs/images/libstb-sq.svg"
                -DPROJECT_NAME="${PROJECT_NAME}"
                -DOUTPUT_DIR="${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/GenerateIframe.cmake"
        COMMENT "Generating iframe wrapper for Python coverage report"
        WORKING_DIRECTORY ${LIBSTB_TEST_SOURCE_DIR}
    )
else()
    message(WARNING "coverage module is not installed: the Python coverage target will be skipped "
                    "(pip install coverage).")
endif()
