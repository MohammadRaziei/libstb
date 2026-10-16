# libstb Documentation Build
# Builds C++ docs (Doxygen) and Python docs (Sphinx) via CreateDocs.cmake scripts

cmake_minimum_required(VERSION 3.19)

set(LIBSTB_DOCS_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)

set(LIBSTB_DOCS_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cpp)
set(LIBSTB_DOCS_CPP_OUT ${LIBSTB_DOCS_OUT}/cpp)

set(LIBSTB_DOCS_PYTHON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/python)
set(LIBSTB_DOCS_PYTHON_OUT ${LIBSTB_DOCS_OUT}/python)


# ── C++ Documentation (Doxygen) ───────────────────────────────

find_package(Doxygen)

if(DOXYGEN_FOUND)
    set(LIBSTB_DOCS_CPP_HTML ${LIBSTB_DOCS_CPP_OUT}/html/index.html)

    add_custom_command(
        OUTPUT ${LIBSTB_DOCS_CPP_HTML}
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DPROJECT_VERSION=${PROJECT_VERSION}
            -DOUTPUT_DIR=${LIBSTB_DOCS_CPP_OUT}
            -DDOXYGEN_EXECUTABLE=${DOXYGEN_EXECUTABLE}
            -DFOOTER_IN=${LIBSTB_DOCS_CPP_DIR}/footer.html.in
            -DTOPBAR_IN=${LIBSTB_DOCS_CPP_DIR}/topbar.html.in
            -DDOXY_CSS=${LIBSTB_DOCS_CPP_DIR}/libstb-doxygen.css
            -DDOXYFILE_IN=${LIBSTB_DOCS_CPP_DIR}/Doxyfile.in
            -DLOGO_SRC=${PROJECT_SOURCE_DIR}/docs/images/libstb-theme.svg
            -DASSETS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/assets
            -P ${LIBSTB_DOCS_CPP_DIR}/CreateDocs.cmake
        DEPENDS
            ${LIBSTB_DOCS_CPP_DIR}/CreateDocs.cmake
            ${LIBSTB_DOCS_CPP_DIR}/footer.html.in
            ${LIBSTB_DOCS_CPP_DIR}/topbar.html.in
            ${LIBSTB_DOCS_CPP_DIR}/libstb-doxygen.css
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/libstb-docs.css
            ${LIBSTB_DOCS_CPP_DIR}/Doxyfile.in
            ${LIBSTB_DOCS_CPP_DIR}/mainpage.md
        COMMENT "Generating C++ API documentation with Doxygen"
        VERBATIM
    )

    add_custom_target(libstb_docs_cpp DEPENDS ${LIBSTB_DOCS_CPP_HTML})
else()
    message(WARNING "Doxygen not found - C++ documentation will not be generated")
    add_custom_target(libstb_docs_cpp
        COMMAND ${CMAKE_COMMAND} -E echo "Skipping C++ docs: Doxygen not found"
    )
endif()


# ── Python Documentation (Sphinx) ─────────────────────────────

find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import sphinx"
    OUTPUT_QUIET
    ERROR_QUIET
    RESULT_VARIABLE SPHINX_CHECK
)

if(SPHINX_CHECK EQUAL 0)
    set(LIBSTB_DOCS_PYTHON_HTML ${LIBSTB_DOCS_PYTHON_OUT}/html/index.html)

    add_custom_command(
        OUTPUT ${LIBSTB_DOCS_PYTHON_HTML}
        COMMAND ${CMAKE_COMMAND}
            -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
            -DSOURCE_DIR=${LIBSTB_DOCS_PYTHON_DIR}/source
            -DOUTPUT_DIR=${LIBSTB_DOCS_PYTHON_OUT}
            -DLIBSTB_PYTHON_INSTALL_DIR=${PYTHON_INSTALL_DIR}
            -P ${LIBSTB_DOCS_PYTHON_DIR}/CreateDocs.cmake
        DEPENDS
            ${LIBSTB_DOCS_PYTHON_DIR}/CreateDocs.cmake
            ${LIBSTB_DOCS_PYTHON_DIR}/source/conf.py
            ${LIBSTB_DOCS_PYTHON_DIR}/source/index.rst
        COMMENT "Building Python API documentation with Sphinx"
        VERBATIM
    )

    add_custom_target(libstb_docs_python DEPENDS ${LIBSTB_DOCS_PYTHON_HTML})

    # Must have build_python run first so the module is installed
    if(TARGET build_python)
        add_dependencies(libstb_docs_python build_python)
    endif()
else()
    message(WARNING "Sphinx not found - Python documentation will not be generated")
    add_custom_target(libstb_docs_python
        COMMAND ${CMAKE_COMMAND} -E echo "Skipping Python docs: Sphinx not found"
    )
endif()


# ── Route Index Page ──────────────────────────────────────────

set(LIBSTB_INDEX_OUT ${LIBSTB_DOCS_OUT}/index.html)
set(LIBSTB_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

# Per-language tab content for the docs/install/example panels: supported_langs.json
# lists the languages (see its "_comment" field for the format), and each language
# folder holds index-docs.json (structured fields) + index-install.txt +
# index-example.txt (raw HTML). generate_index.py assembles index.html from these.
# Globbed at configure time so adding or editing a fragment triggers a reconfigure -
# new fragment files need `cmake` re-run once (e.g. via `touch supported_langs.json`)
# for the glob itself to pick them up.
set(LIBSTB_SUPPORTED_LANGS ${CMAKE_CURRENT_SOURCE_DIR}/supported_langs.json)
set(LIBSTB_GENERATE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/generate_index.py)
file(GLOB_RECURSE LIBSTB_LANG_FRAGMENTS
    ${CMAKE_CURRENT_SOURCE_DIR}/*/index-docs.json
    ${CMAKE_CURRENT_SOURCE_DIR}/*/index-install.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/*/index-example.txt
)

set(LIBSTB_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(LIBSTB_SHOWCASE_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/make_showcase.py)
file(GLOB_RECURSE LIBSTB_ASSET_FILES ${LIBSTB_ASSETS_DIR}/*)

add_custom_command(
    OUTPUT ${LIBSTB_INDEX_OUT}
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_DIR=${LIBSTB_DOCS_OUT}
        -DINDEX_IN=${CMAKE_CURRENT_SOURCE_DIR}/index.html.in
        -DASSETS_DIR=${LIBSTB_ASSETS_DIR}
        -DSHOWCASE_SCRIPT=${LIBSTB_SHOWCASE_SCRIPT}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DLOGO_SQ=${PROJECT_SOURCE_DIR}/docs/images/libstb-sq.svg
        -DLANGS_FILE=${LIBSTB_SUPPORTED_LANGS}
        -DLANGS_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DGENERATE_SCRIPT=${LIBSTB_GENERATE_SCRIPT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CreateDocs.cmake
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/CreateDocs.cmake
        ${LIBSTB_GENERATE_SCRIPT}
        ${CMAKE_CURRENT_SOURCE_DIR}/index.html.in
        ${LIBSTB_ASSET_FILES}
        ${LIBSTB_SHOWCASE_SCRIPT}
        ${PROJECT_SOURCE_DIR}/docs/images/libstb-sq.svg
        ${LIBSTB_SUPPORTED_LANGS}
        ${LIBSTB_LANG_FRAGMENTS}
    COMMENT "Creating the landing page"
    VERBATIM
)

add_custom_target(libstb_docs_index DEPENDS ${LIBSTB_INDEX_OUT})


# ── Master target ─────────────────────────────────────────────

add_custom_target(libstb_docs
    COMMAND ${CMAKE_COMMAND} -E echo "-- All documentation generated"
    COMMENT "Building all documentation"
)

add_dependencies(libstb_docs libstb_docs_cpp libstb_docs_python libstb_docs_index)


# ── Post-process: inject HTML comment into all output HTML files ──

add_custom_command(TARGET libstb_docs POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_DIR=${LIBSTB_DOCS_OUT}
        -DCOMMENT_IN=${CMAKE_CURRENT_SOURCE_DIR}/initial_comment.in
        -DASCII_ART_FILE=${CMAKE_CURRENT_SOURCE_DIR}/images/libstb-sq.txt
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/InjectComment.cmake
    COMMENT "Injecting HTML comments into documentation pages"
    VERBATIM
)

# ── Benchmarks page ───────────────────────────────────────────
# benchmarks/outputs/ is generated on a developer machine (see benchmarks/README.md) and
# committed; it is only copied here, never rebuilt. It is published as it is, folder and all:
# index.html (the page), benchmark.json (its data, which the landing page's teaser also reads in
# the browser) and results/<step>.json (what each step measured). Runs after InjectComment above so the page is published
# byte-for-byte as committed.
set(LIBSTB_BENCH_OUTDIR ${PROJECT_SOURCE_DIR}/benchmarks/outputs)
if(EXISTS ${LIBSTB_BENCH_OUTDIR}/index.html)
    add_custom_command(TARGET libstb_docs POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${LIBSTB_BENCH_OUTDIR} ${LIBSTB_DOCS_OUT}/benchmarks
        COMMENT "Publishing committed benchmarks/outputs/ -> ${LIBSTB_DOCS_OUT}/benchmarks/"
        VERBATIM
    )
else()
    message(WARNING "benchmarks/outputs/index.html not found -- the docs site "
                    "will have no /benchmarks/ page. Generate it as described in "
                    "benchmarks/README.md and commit it.")
endif()
