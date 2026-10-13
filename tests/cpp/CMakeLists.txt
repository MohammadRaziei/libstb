file(GLOB LIBSTB_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

add_executable(test_libstb_cpp
    ${LIBSTB_TEST_SOURCES}
    ${LIBSTB_TEST_SOURCE_DIR}/utest/utest_main.cxx
)
target_include_directories(test_libstb_cpp PRIVATE ${LIBSTB_TEST_SOURCE_DIR})
# Public API only: stb headers are never included by tests.
target_link_libraries(test_libstb_cpp PRIVATE libstb::core)
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
# The regular test above links the optimised libstb::core. For coverage the core
# sources are compiled a second time with --coverage -O0, into their own static
# library and test executable, so the normal build (and the Python extension, which
# also links libstb::core) stays untouched. Both are EXCLUDE_FROM_ALL: only
# `libstb_coverage_cpp` builds them.
if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
    add_library(libstb_core_cov STATIC EXCLUDE_FROM_ALL ${LIBSTB_CORE_SOURCES})
    target_include_directories(libstb_core_cov
        PUBLIC ${PROJECT_SOURCE_DIR}/include)
    target_include_directories(libstb_core_cov SYSTEM
        PRIVATE ${PROJECT_SOURCE_DIR}/src/third_party/stb)
    target_compile_options(libstb_core_cov PRIVATE -O0 -g --coverage)
    target_link_options(libstb_core_cov PUBLIC --coverage)
    set_target_properties(libstb_core_cov PROPERTIES POSITION_INDEPENDENT_CODE ON)

    add_executable(test_libstb_cpp_cov EXCLUDE_FROM_ALL
        ${LIBSTB_TEST_SOURCES}
        ${LIBSTB_TEST_SOURCE_DIR}/utest/utest_main.cxx
    )
    target_include_directories(test_libstb_cpp_cov PRIVATE ${LIBSTB_TEST_SOURCE_DIR})
    target_link_libraries(test_libstb_cpp_cov PRIVATE libstb_core_cov Threads::Threads)
    target_compile_definitions(test_libstb_cpp_cov PRIVATE
        LIBSTB_TEST_DATA_DIR="${LIBSTB_TEST_SOURCE_DIR}/data")
    target_compile_options(test_libstb_cpp_cov PRIVATE -O0 -g --coverage)

    add_custom_target(libstb_coverage_cpp
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/html
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/html

        COMMAND ${CMAKE_COMMAND} -E echo "-- Running c++ tests with coverage..."
        COMMAND ${LCOV_EXECUTABLE} --zerocounters --directory ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND $<TARGET_FILE:test_libstb_cpp_cov>

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
    add_dependencies(libstb_coverage_cpp test_libstb_cpp_cov)
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
