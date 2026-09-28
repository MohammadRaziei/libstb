# Development.Module needs only Python.h (no libpython: correct for extension
# modules, and manylinux has none). SABIModule is what nanobind's STABLE_ABI
# needs; OPTIONAL because Python < 3.12 has no usable limited-API library and
# nanobind then silently builds a normal per-version extension.
find_package(Python COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule REQUIRED)

if(DEFINED SKBUILD)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(libstb_py STABLE_ABI NB_STATIC ${CMAKE_CURRENT_SOURCE_DIR}/binding.cpp)
    target_link_libraries(libstb_py PRIVATE libstb::core)

    # .pyi stubs, generated from the freshly built module.
    add_custom_command(TARGET libstb_py POST_BUILD
        COMMAND ${Python_EXECUTABLE} -m nanobind.stubgen
                -m libstb_py
                -o ${CMAKE_CURRENT_BINARY_DIR}/libstb_py.pyi
                -i $<TARGET_FILE_DIR:libstb_py>
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/libstb_py.pyi
        COMMENT "Generating libstb_py.pyi"
    )

    install(TARGETS libstb_py LIBRARY DESTINATION libstb)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libstb_py.pyi DESTINATION libstb)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/libstb/
        DESTINATION libstb
        FILES_MATCHING PATTERN "*.py")
    return()
endif()

# Standalone (non-SKBUILD) build: build the extension in-tree via
# ExternalProject so `cmake --build` + `ctest` exercise the same path as
# `pip install .`, without re-running the top-level configure under
# scikit-build-core.
include(ExternalProject)
execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import nanobind; print(nanobind.cmake_dir())"
    OUTPUT_VARIABLE _libstb_nb_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _libstb_nb_rc
    ERROR_QUIET
)
if(NOT _libstb_nb_rc EQUAL 0)
    message(FATAL_ERROR "nanobind not found: pip install nanobind")
endif()

ExternalProject_Add(build_python
    SOURCE_DIR ${CMAKE_SOURCE_DIR}
    BINARY_DIR ${CMAKE_BINARY_DIR}/python/build
    INSTALL_DIR ${PYTHON_INSTALL_DIR}
    CMAKE_ARGS
        --no-warn-unused-cli
        -DSKBUILD=2
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${PYTHON_INSTALL_DIR}
        -DPython_EXECUTABLE=${Python_EXECUTABLE}
        -Dnanobind_DIR=${_libstb_nb_dir}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install . --config Release
    BUILD_ALWAYS 1
)
add_dependencies(build_python libstb_core)
