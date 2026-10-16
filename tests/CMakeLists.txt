set(LIBSTB_TEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LIBSTB_TEST_BINARY_DIR ${CMAKE_BINARY_DIR}/tests)

# Coverage reports (lcov + genhtml for C++, coverage.py for the Python layer). Each language
# writes into its own directory; stb_coverage_total merges them. The dashboard that links
# them all is generated at the end of this file (build/coverage/index.html).
set(LIBSTB_COVERAGE_BINARY_DIR ${CMAKE_BINARY_DIR}/coverage)
set(LIBSTB_COVERAGE_TOTAL_BINARY_DIR  ${LIBSTB_COVERAGE_BINARY_DIR}/total  CACHE PATH "Total coverage output directory")
set(LIBSTB_COVERAGE_CPP_BINARY_DIR    ${LIBSTB_COVERAGE_BINARY_DIR}/cpp    CACHE PATH "C++ coverage output directory")
set(LIBSTB_COVERAGE_PYTHON_BINARY_DIR ${LIBSTB_COVERAGE_BINARY_DIR}/python CACHE PATH "Python coverage output directory")
file(MAKE_DIRECTORY ${LIBSTB_COVERAGE_BINARY_DIR})

add_custom_target(stb_test
    COMMAND ${CMAKE_COMMAND} -E echo "-- Running All tests..."
)

add_custom_target(stb_coverage
    COMMAND ${CMAKE_COMMAND} -E echo "-- Running coverage for all languages ..."
)

find_program(LCOV_EXECUTABLE lcov)
find_program(GENHTML_EXECUTABLE genhtml)

add_subdirectory(${LIBSTB_TEST_SOURCE_DIR}/cpp)

if(TARGET build_python)
    add_subdirectory(${LIBSTB_TEST_SOURCE_DIR}/python)
    add_subdirectory(${LIBSTB_TEST_SOURCE_DIR}/cmake)
endif()

# ── Total coverage: merge the per-language lcov files ─────────────────────────
if(TARGET stb_coverage_cpp AND TARGET stb_coverage_python)
    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
        add_custom_target(stb_coverage_total
            COMMAND ${CMAKE_COMMAND} -E remove_directory ${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/html
            COMMAND ${CMAKE_COMMAND} -E echo "-- Merging C++ and Python coverage..."
            COMMAND ${LCOV_EXECUTABLE}
                    --add-tracefile ${LIBSTB_COVERAGE_CPP_BINARY_DIR}/coverage.lcov
                    --add-tracefile ${LIBSTB_COVERAGE_PYTHON_BINARY_DIR}/coverage.lcov
                    --output-file ${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/coverage.lcov
            COMMAND ${CMAKE_COMMAND} -E echo "-- Total coverage info created at: ${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/coverage.lcov"

            # lcov files of the Python layer carry paths relative to src/bindings/python/
            COMMAND ${CMAKE_COMMAND}
                -DCOV_FILE=${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/coverage.lcov
                -DPREFIX_PATH=${PROJECT_SOURCE_DIR}/
                -P ${PROJECT_SOURCE_DIR}/cmake/FixLCovPaths.cmake
            COMMAND ${CMAKE_COMMAND} -E echo "-- Generating HTML report..."
            COMMAND ${GENHTML_EXECUTABLE} --quiet
                    ${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/coverage.lcov
                    --output-directory ${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/html
                    --prefix ${PROJECT_SOURCE_DIR}
                    --title "libstb Total Coverage"
                    --css-file ${LIBSTB_TEST_SOURCE_DIR}/genhtml.css
                    --dark-mode
            COMMAND ${CMAKE_COMMAND}
                -DCOV_FILE=${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/coverage.lcov
                -DSOURCE_PATH=${PROJECT_SOURCE_DIR}/
                -P ${PROJECT_SOURCE_DIR}/cmake/FixLCovPaths.cmake
            COMMAND ${CMAKE_COMMAND} -E echo "-- HTML entry point created here: ${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}/html/index.html"

            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            USES_TERMINAL
        )
        add_dependencies(stb_coverage_total stb_coverage_cpp stb_coverage_python)
        add_dependencies(stb_coverage stb_coverage_total)

        add_custom_command(TARGET stb_coverage_total POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DIFRAME_SRC="html/index.html"
                -DSECTION_TITLE="Total%20Coverage"
                -DLOGO_SVG="${PROJECT_SOURCE_DIR}/docs/images/libstb-sq.svg"
                -DPROJECT_NAME="${PROJECT_NAME}"
                -DOUTPUT_DIR="${LIBSTB_COVERAGE_TOTAL_BINARY_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/GenerateIframe.cmake"
            COMMENT "Generating iframe wrapper for total coverage report"
            WORKING_DIRECTORY ${LIBSTB_TEST_SOURCE_DIR}
        )
    endif()
endif()

# ── Dashboard: build/coverage/index.html links every report that exists ───────
set(COVERAGE_DATA_JSON "[")

if(TARGET stb_coverage_total)
    set(COVERAGE_DATA_JSON "${COVERAGE_DATA_JSON}
        {
            \"id\": \"total\",
            \"title\": \"Total Coverage\",
            \"badge\": \"Merged\",
            \"badgeClass\": \"total\",
            \"color\": \"#a371f7\",
            \"desc\": \"Combined coverage report for the C++ core and the Python layer. Useful for SonarQube analysis.\",
            \"htmlLink\": \"total/index.html\",
            \"lcovLink\": \"total/coverage.lcov\"
        },")
endif()

if(TARGET stb_coverage_cpp)
    set(COVERAGE_DATA_JSON "${COVERAGE_DATA_JSON}
        {
            \"id\": \"cpp\",
            \"title\": \"C++ Core\",
            \"badge\": \"Native\",
            \"color\": \"#f0883e\",
            \"desc\": \"Coverage of the core library (src/core and the public headers) as exercised by the C++ tests, generated by LCOV. The vendored stb headers are left out.\",
            \"htmlLink\": \"cpp/index.html\",
            \"lcovLink\": \"cpp/coverage.lcov\"
        },")
endif()

if(TARGET stb_coverage_python)
    set(COVERAGE_DATA_JSON "${COVERAGE_DATA_JSON}
        {
            \"id\": \"py\",
            \"title\": \"Python Layer\",
            \"badge\": \"Interpreter\",
            \"color\": \"#3572A5\",
            \"desc\": \"Coverage of the Python package (src/bindings/python/libstb) as exercised by the Python tests, generated by coverage.py. The native extension itself is not instrumented here.\",
            \"htmlLink\": \"python/index.html\",
            \"lcovLink\": \"python/coverage.lcov\"
        },")
endif()

set(COVERAGE_DATA_JSON "${COVERAGE_DATA_JSON}]")

set(LIBSTB_LOGO_SVG "${PROJECT_SOURCE_DIR}/docs/images/libstb-sq.svg")
if(EXISTS "${LIBSTB_LOGO_SVG}")
    file(READ "${LIBSTB_LOGO_SVG}" LOGO_CONTENT)
else()
    set(LOGO_CONTENT "")
endif()

configure_file(
    ${LIBSTB_TEST_SOURCE_DIR}/coverage_dashboard.html.in
    ${LIBSTB_COVERAGE_BINARY_DIR}/index.html
    @ONLY
)
