file(GLOB LIBSTB_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

# The core sources are compiled into the test executable (like ctoon's tests do with ctoon.c)
# instead of linking libstb::core: the --coverage flags below then reach their object files,
# and the shipped library target stays untouched. libstb_apply_core_build_settings() gives
# them the same flags libstb_core is built with.
add_executable(test_libstb_cpp
    ${LIBSTB_CORE_SOURCES}
    ${LIBSTB_TEST_SOURCES}
    ${LIBSTB_TEST_SOURCE_DIR}/utest/utest_main.cxx
)
libstb_apply_core_build_settings(test_libstb_cpp)
# Tests use the public API only: the vendored stb headers are on the include path just because
# the core sources are compiled in here, and no test includes them.
target_include_directories(test_libstb_cpp PRIVATE ${PROJECT_SOURCE_DIR}/include ${LIBSTB_TEST_SOURCE_DIR})
# Synthetic font with solid-rectangle glyphs (tests/data/make_test_font.py).
target_compile_definitions(test_libstb_cpp PRIVATE
    LIBSTB_TEST_DATA_DIR="${LIBSTB_TEST_SOURCE_DIR}/data")
find_package(Threads REQUIRED)
target_link_libraries(test_libstb_cpp PRIVATE Threads::Threads)

add_test(NAME test_libstb_cpp COMMAND $<TARGET_FILE:test_libstb_cpp>)

add_custom_target(libstb_test_cpp
    COMMAND ${CMAKE_COMMAND} -E echo "-- Running c++ tests..."
    COMMAND $<TARGET_FILE:test_libstb_cpp>
    WORKING_DIRECTORY ${LIBSTB_TEST_BINARY_DIR}
)
add_dependencies(libstb_test_cpp test_libstb_cpp)
add_dependencies(libstb_test libstb_test_cpp)

# ── Coverage (lcov + genhtml) ─────────────────────────────────────────────────
# The test executable above is also the coverage one: with lcov and genhtml available it is
# built with coverage instrumentation (-O0 so lines map 1:1), and libstb_coverage_cpp runs it.
if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
    target_compile_options(test_libstb_cpp PRIVATE -O0 -g --coverage)
    target_link_options(test_libstb_cpp PRIVATE --coverage)
    # LTO (from apply_optimization_flags) would blur the line counters
    set_target_properties(test_libstb_cpp PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)

    add_custom_target(libstb_coverage_cpp
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/html
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/html

        COMMAND ${CMAKE_COMMAND} -E echo "-- Running c++ tests with coverage..."
        COMMAND ${LCOV_EXECUTABLE} --zerocounters --directory ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND $<TARGET_FILE:test_libstb_cpp>

        COMMAND ${CMAKE_COMMAND} -E echo "-- Capturing coverage..."
        COMMAND ${LCOV_EXECUTABLE}
                --capture
                --base-directory ${PROJECT_SOURCE_DIR}
                --directory ${CMAKE_CURRENT_BINARY_DIR}
                --exclude "*/tests/*"
                --no-external
                --rc function_coverage=1
                --output-file ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage_temp.lcov
        COMMAND ${CMAKE_COMMAND} -E echo "Filtering libstb source files..."
        COMMAND ${LCOV_EXECUTABLE}
                --extract
                ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage_temp.lcov
                "*/src/core/*" "*/include/stb/*"
                --output-file ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage.lcov
        COMMAND ${CMAKE_COMMAND} -E remove ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage_temp.lcov

        COMMAND ${CMAKE_COMMAND} -E echo "-- Generating HTML report..."
        COMMAND ${GENHTML_EXECUTABLE} --quiet
                ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage.lcov
                --output-directory ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/html
                --prefix ${PROJECT_SOURCE_DIR}
                --title "libstb C++ Coverage"
                --css-file ${LIBSTB_TEST_SOURCE_DIR}/genhtml.css
                --dark-mode
        COMMAND ${CMAKE_COMMAND} -E echo "-- HTML entry point created here: ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/html/index.html"

        COMMAND ${CMAKE_COMMAND} -E echo "-- Fixing paths to relative..."
        COMMAND ${CMAKE_COMMAND}
                -DCOV_FILE=${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage.lcov
                -DSOURCE_PATH=${PROJECT_SOURCE_DIR}/
                -P ${PROJECT_SOURCE_DIR}/cmake/FixLCovPaths.cmake
        COMMAND ${CMAKE_COMMAND} -E echo "-- Coverage summary:"
        # --summary rather than --list: with lcov 2.0 the per-file table of --list shows wrong rates
        COMMAND ${LCOV_EXECUTABLE} --summary ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage.lcov

        WORKING_DIRECTORY ${LIBSTB_TEST_BINARY_DIR}
        USES_TERMINAL
    )
    add_dependencies(libstb_coverage_cpp test_libstb_cpp)
    add_dependencies(libstb_coverage libstb_coverage_cpp)

    file(MAKE_DIRECTORY ${LIBSTB_COVERAGE_CPP_BINARY_DIR})

    add_custom_command(TARGET libstb_coverage_cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                -DIFRAME_SRC="html/index.html"
                -DSECTION_TITLE="C++%20Core"
                -DLOGO_SVG="${PROJECT_SOURCE_DIR}/docs/images/libstb-sq.svg"
                -DPROJECT_NAME="${PROJECT_NAME}"
                -DOUTPUT_DIR="${LIBSTB_COVERAGE_CPP_BINARY_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/GenerateIframe.cmake"
        COMMENT "Generating iframe wrapper for C++ coverage report"
        WORKING_DIRECTORY ${LIBSTB_TEST_SOURCE_DIR}
    )
else()
    message(WARNING "LCOV or GENHTML executables not found: C++ coverage targets will be skipped "
                    "(e.g. 'sudo apt install lcov' on Linux).")
endif()
