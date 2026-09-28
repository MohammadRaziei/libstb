file(GLOB LIBSTB_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

add_executable(test_libstb_cpp
    ${LIBSTB_TEST_SOURCES}
    ${LIBSTB_TEST_SOURCE_DIR}/utest/utest_main.cxx
)
target_include_directories(test_libstb_cpp PRIVATE ${LIBSTB_TEST_SOURCE_DIR})
# Public API only: stb headers are never included by tests.
target_link_libraries(test_libstb_cpp PRIVATE libstb::core)
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
