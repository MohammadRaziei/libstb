cmake_minimum_required(VERSION 3.21)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(DynamicVersion)

extract_version_string(
    HEADER_FILE ${CMAKE_CURRENT_LIST_DIR}/include/stb.h
    VERSION_PREFIX LIBSTB_
    OUTPUT_VAR PROJECT_VERSION
)

project(libstb LANGUAGES CXX VERSION ${PROJECT_VERSION})
message(STATUS "Project: ${PROJECT_NAME}@v${PROJECT_VERSION}")
include(Startup)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(Optimize)

option(LIBSTB_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(LIBSTB_BUILD_PYTHON "Build Python bindings" ${PROJECT_IS_TOP_LEVEL})

# --- libstb C++ core ---
# stb headers are vendored (committed) under src/third_party/stb and are only
# ever included from src/core/*.cpp, never from a public include/ header.
# STATIC (unlike httpp's SHARED core): stb is header-only, so there is nothing
# to gain from a DLL, and each stb header is compiled with STB_*_STATIC so no
# stbi_* symbol leaks out to consumers.
file(GLOB LIBSTB_CORE_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/core/*.cpp)

add_library(libstb_core STATIC ${LIBSTB_CORE_SOURCES})
add_library(libstb::core ALIAS libstb_core)

set_target_properties(libstb_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON        # linked into the Python extension
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    EXPORT_NAME core                    # consumers link libstb::core
    OUTPUT_NAME stb_core                # libstb_core.a / stb_core.lib
)

# Install layout: the wheel carries include/, lib/ and cmake/ inside the
# `libstb` package (httpp's pattern, see get_cmake_dir() in the Python
# package); a plain `cmake --install` uses the usual prefix layout.
if(DEFINED SKBUILD)
    set(_libstb_lib_dest   libstb/lib)
    set(_libstb_inc_dest   libstb/include)
    set(_libstb_cmake_dest libstb/cmake)
else()
    set(_libstb_lib_dest   lib)
    set(_libstb_inc_dest   include)
    set(_libstb_cmake_dest lib/cmake/libstb)
endif()

target_include_directories(libstb_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${_libstb_inc_dest}>
)
# SYSTEM: vendored headers we don't control - their internal warnings are not
# ours to fix under our own -Wall/-Wextra/-W4.
target_include_directories(libstb_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/src/third_party/stb)

target_compile_options(libstb_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /EHsc>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
# -fno-trapping-math: lets GCC/Clang vectorize float->int conversions in the JPEG encoder's
# quantization (about 2x faster JPEG). It only means FP operations are not required to
# raise hardware traps (nothing here enables or reads them); results are unchanged.
target_compile_options(libstb_core PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-trapping-math>
)
apply_optimization_flags(libstb_core)

# --- install (find_package(libstb) -> libstb::core) ---
include(CMakePackageConfigHelpers)
install(TARGETS libstb_core EXPORT libstbTargets ARCHIVE DESTINATION ${_libstb_lib_dest})
install(DIRECTORY include/ DESTINATION ${_libstb_inc_dest})
install(EXPORT libstbTargets FILE libstbTargets.cmake NAMESPACE libstb:: DESTINATION ${_libstb_cmake_dest})
configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/libstbConfig.cmake.in
    ${PROJECT_BINARY_DIR}/libstbConfig.cmake
    INSTALL_DESTINATION ${_libstb_cmake_dest}
)
write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/libstbConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${PROJECT_BINARY_DIR}/libstbConfig.cmake
    ${PROJECT_BINARY_DIR}/libstbConfigVersion.cmake
    DESTINATION ${_libstb_cmake_dest}
)

if(LIBSTB_BUILD_PYTHON OR DEFINED SKBUILD)
    set(PYTHON_INSTALL_DIR ${CMAKE_BINARY_DIR}/python/install)
    add_subdirectory(${PROJECT_SOURCE_DIR}/src/bindings/python)
    if(DEFINED SKBUILD)
        return()
    endif()
endif()

if(LIBSTB_BUILD_TESTS)
    enable_testing()
    set(CMAKE_CTEST_ARGUMENTS "--output-on-failure" "-V")
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests)
endif()
