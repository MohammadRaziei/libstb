# libstb Documentation Build
# C++ reference (Doxygen) and Python reference (Sphinx) via their CreateDocs.cmake scripts, and the
# front of the site (home page, lessons, benchmarks) with Docusaurus (docs/ is the Docusaurus project).
#
#   stb_docs_doxygen    docs/out/cpp/html       C++ reference
#   stb_docs_sphinx     docs/out/python/html    Python reference
#   stb_docs_docusaurus docs/out/                home page, /docs/ lessons, /benchmarks/
#   stb_docs            all three

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

    add_custom_target(stb_docs_doxygen DEPENDS ${LIBSTB_DOCS_CPP_HTML})
else()
    message(WARNING "Doxygen not found - C++ documentation will not be generated")
    add_custom_target(stb_docs_doxygen
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

    add_custom_target(stb_docs_sphinx DEPENDS ${LIBSTB_DOCS_PYTHON_HTML})

    # Must have build_python run first so the module is installed
    if(TARGET build_python)
        add_dependencies(stb_docs_sphinx build_python)
    endif()
else()
    message(WARNING "Sphinx not found - Python documentation will not be generated")
    add_custom_target(stb_docs_sphinx
        COMMAND ${CMAKE_COMMAND} -E echo "Skipping Python docs: Sphinx not found"
    )
endif()

# ── Docusaurus: home page, lessons, benchmarks ───────────────────────────────
# This folder is a Docusaurus project: it is the front of the site. It replaces the generated landing page and the
# standalone benchmark report: the home page and the lessons are written here (src/, docs/), and /benchmarks/ is drawn
# from benchmarks/outputs/*.json. The two references keep their addresses (python/html and cpp/html).
# Docusaurus builds into its own folder and the result is merged into docs/out, so it never wipes the
# Doxygen and Sphinx output that is already there.
#
#   stb_docs_figures  run the Python snippets and the home-page demo; their output is what the pages show
find_program(LIBSTB_NPM npm)
find_program(LIBSTB_PYTHON NAMES python3 python)
set(LIBSTB_SITE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LIBSTB_SITE_BUILD ${CMAKE_CURRENT_BINARY_DIR}/site-build)

if(LIBSTB_NPM AND LIBSTB_PYTHON)
    add_custom_target(stb_docs_figures
        COMMAND ${LIBSTB_PYTHON} ${LIBSTB_SITE_DIR}/scripts/prepare.py figures
                --python-path ${PYTHON_INSTALL_DIR}
        COMMENT "Running the Python snippets and the home-page demo to produce the figures"
        VERBATIM
    )
    if(TARGET build_python)
        add_dependencies(stb_docs_figures build_python)
    endif()

    add_custom_target(stb_docs_docusaurus
        COMMAND ${LIBSTB_NPM} install
        COMMAND ${CMAKE_COMMAND} -E env
                LIBSTB_DOCS_OUT=${LIBSTB_DOCS_OUT}
                LIBSTB_VERSION=${PROJECT_VERSION}
                ${LIBSTB_NPM} run build -- --out-dir ${LIBSTB_SITE_BUILD}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${LIBSTB_SITE_BUILD} ${LIBSTB_DOCS_OUT}
        WORKING_DIRECTORY ${LIBSTB_SITE_DIR}
        COMMENT "Building the Docusaurus site -> ${LIBSTB_DOCS_OUT}"
        VERBATIM
    )
    # The site shows both references' addresses through the tag file and objects.inv.
    add_dependencies(stb_docs_docusaurus stb_docs_doxygen stb_docs_sphinx stb_docs_figures)
else()
    add_custom_target(stb_docs_docusaurus
        COMMAND ${CMAKE_COMMAND} -E echo "stb_docs_docusaurus needs npm and python3, and one of them was not found"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
endif()


# ── Master target ─────────────────────────────────────────────

add_custom_target(stb_docs
    COMMAND ${CMAKE_COMMAND} -E echo "-- All documentation generated"
    COMMENT "Building all documentation"
)

add_dependencies(stb_docs stb_docs_doxygen stb_docs_sphinx stb_docs_docusaurus)


# ── Post-process: inject HTML comment into all output HTML files ──

add_custom_command(TARGET stb_docs POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT_DIR=${LIBSTB_DOCS_OUT}
        -DCOMMENT_IN=${CMAKE_CURRENT_SOURCE_DIR}/initial_comment.in
        -DASCII_ART_FILE=${CMAKE_CURRENT_SOURCE_DIR}/images/libstb-sq.txt
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/InjectComment.cmake
    COMMENT "Injecting HTML comments into documentation pages"
    VERBATIM
)
