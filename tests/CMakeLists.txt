set(LIBSTB_TEST_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(LIBSTB_TEST_BINARY_DIR ${CMAKE_BINARY_DIR}/tests)

add_custom_target(libstb_test
    COMMAND ${CMAKE_COMMAND} -E echo "-- Running All tests..."
)

add_subdirectory(${LIBSTB_TEST_SOURCE_DIR}/cpp)

if(TARGET build_python)
    add_subdirectory(${LIBSTB_TEST_SOURCE_DIR}/python)
    add_subdirectory(${LIBSTB_TEST_SOURCE_DIR}/cmake)
endif()
