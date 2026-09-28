find_package(Python3 COMPONENTS Interpreter REQUIRED)

execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pytest, numpy"
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE LIBSTB_PYDEPS_RESULT
)
if(NOT LIBSTB_PYDEPS_RESULT EQUAL 0)
    message(WARNING "pytest/numpy not installed: Python tests skipped (pip install pytest numpy)")
    return()
endif()

# Run from the install dir so `import libstb` finds the freshly built package.
add_test(NAME test_libstb_python
    COMMAND ${Python3_EXECUTABLE} -m pytest ${LIBSTB_TEST_SOURCE_DIR}/python -p no:cacheprovider
    WORKING_DIRECTORY ${PYTHON_INSTALL_DIR}
)

add_custom_target(libstb_test_python
    COMMAND ${CMAKE_COMMAND} -E echo "-- Running python tests..."
    COMMAND ${Python3_EXECUTABLE} -m pytest ${LIBSTB_TEST_SOURCE_DIR}/python -p no:cacheprovider
    WORKING_DIRECTORY ${PYTHON_INSTALL_DIR}
    USES_TERMINAL
)
add_dependencies(libstb_test_python build_python)
add_dependencies(libstb_test libstb_test_python)
