# Docs snippets are real programs: they are built and run as tests, so a snippet that stops
# compiling (or a lesson that exists in one language only) fails ctest instead of rotting.
# Each snippet takes <data-dir> <out-dir>; the same run produces the figures a lesson page shows.

find_package(Python3 COMPONENTS Interpreter QUIET)

file(GLOB LIBSTB_SNIPPETS_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp)
foreach(source IN LISTS LIBSTB_SNIPPETS_CPP)
    get_filename_component(name ${source} NAME_WE)
    add_executable(snippet_${name} ${source})
    target_link_libraries(snippet_${name} PRIVATE libstb::libstb)
    add_test(NAME docs_snippet_cpp_${name}
        COMMAND snippet_${name} ${PROJECT_SOURCE_DIR}/data ${CMAKE_CURRENT_BINARY_DIR}/out/cpp/${name})
    if(WIN32)
        # stb_core is a DLL (stb.dll). Windows looks for it beside the executable, and nothing else
        # puts it there (test_stb_cpp compiles the core sources in instead of linking the DLL).
        add_custom_command(TARGET snippet_${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:stb_core> $<TARGET_FILE_DIR:snippet_${name}>
            VERBATIM)
    endif()
    # A snippet that hangs must fail the test, not hold the CI job for hours.
    set_tests_properties(docs_snippet_cpp_${name} PROPERTIES TIMEOUT 120)
endforeach()

if(Python3_FOUND)
    # Same names in every language folder (the language list is languages.json).
    add_test(NAME docs_snippets_parity
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/prepare.py check)

    if(PYTHON_INSTALL_DIR)
        file(GLOB LIBSTB_SNIPPETS_PY CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/python/*.py)
        foreach(source IN LISTS LIBSTB_SNIPPETS_PY)
            get_filename_component(name ${source} NAME_WE)
            add_test(NAME docs_snippet_python_${name}
                COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${PYTHON_INSTALL_DIR}
                        ${Python3_EXECUTABLE} ${source}
                        ${PROJECT_SOURCE_DIR}/data ${CMAKE_CURRENT_BINARY_DIR}/out/python/${name})
            set_tests_properties(docs_snippet_python_${name} PROPERTIES TIMEOUT 120)
        endforeach()
    endif()
endif()
