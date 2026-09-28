cmake_minimum_required(VERSION 3.15)
project(libstb_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Put `app` at a predictable path on every generator (multi-config included).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} ${CMAKE_BINARY_DIR})
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Ask the *installed* libstb package where its CMake config lives (same
# pattern as nanobind/pybind11) - no manual -I/-L flags, no hardcoded paths.
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import libstb; print(libstb.get_cmake_dir())"
    OUTPUT_VARIABLE libstb_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

find_package(libstb CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE libstb::core)
