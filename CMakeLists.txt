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

option(STB_BUILD_SHARED "Build the C++ core as a shared library (libstb.so, libstb.dylib, stb.dll) instead of a static one" ON)
option(STB_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(STB_BUILD_PYTHON "Build Python bindings" ${PROJECT_IS_TOP_LEVEL})
option(STB_BUILD_EXAMPLES "Build the C++ examples in examples/cpp" OFF)
option(STB_BUILD_DOCS "Build documentation (Doxygen + Sphinx)" OFF)

# --- libstb C++ core ---
# There is no stb header in the tree any more: the decoders, encoders, font reader and resizer are
# libstb's own code derived from stb (see NOTICE.md), and no public include/ header includes any of it.
# One folder per module (util, image, codec, zlib, font, draw); the bindings live elsewhere.
#
# The core is a shared library by default (libstb.so, libstb.dylib, stb.dll): the Python extension
# and every C++ program link to the same copy, which is what lets a wheel carry one engine instead
# of two. Only what the public headers mark STB_API is exported (include/stb/export.hpp); the rest
# is hidden. -DSTB_BUILD_SHARED=OFF gives libstb_core.a / stb_core.lib instead.
file(GLOB_RECURSE LIBSTB_CORE_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/src/util/*.cpp
    ${PROJECT_SOURCE_DIR}/src/image/*.cpp
    ${PROJECT_SOURCE_DIR}/src/codec/*.cpp
    ${PROJECT_SOURCE_DIR}/src/zlib/*.cpp
    ${PROJECT_SOURCE_DIR}/src/font/*.cpp
    ${PROJECT_SOURCE_DIR}/src/draw/*.cpp)

if(STB_BUILD_SHARED)
    add_library(stb_core SHARED ${LIBSTB_CORE_SOURCES})
    target_compile_definitions(stb_core PRIVATE STB_BUILDING_LIBRARY)
    set(_libstb_output_name stb)            # libstb.so / libstb.dylib / stb.dll
else()
    add_library(stb_core STATIC ${LIBSTB_CORE_SOURCES})
    target_compile_definitions(stb_core PUBLIC STB_STATIC_DEFINE)   # STB_API is empty
    set(_libstb_output_name stb_core)       # libstb_core.a / stb_core.lib
endif()
add_library(libstb::core ALIAS stb_core)

set_target_properties(stb_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON        # the static library is linked into the Python extension too
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    EXPORT_NAME core                    # consumers link libstb::core
    OUTPUT_NAME ${_libstb_output_name}
)
if(STB_BUILD_SHARED AND NOT DEFINED SKBUILD)
    # libstb.so -> libstb.so.0.8 -> libstb.so.0.8.0 in a plain install. A wheel carries a single
    # file (zip has no symlinks), so it gets no version suffix.
    set_target_properties(stb_core PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
endif()
# MSVC: the exported classes hold std::string / std::vector members (C4251) and derive from std
# exceptions (C4275). Both are fine as long as the library and its users use the same compiler and
# runtime, which a DLL of C++ classes already requires.
target_compile_options(stb_core PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4251 /wd4275>)

# Install layout: the wheel carries include/, lib/ and cmake/ inside the
# `libstb` package (httpp's pattern, see get_cmake_dir() in the Python
# package); a plain `cmake --install` uses the usual prefix layout.
if(DEFINED SKBUILD)
    set(_libstb_lib_dest   libstb/lib)
    set(_libstb_bin_dest   libstb/lib)  # stb.dll beside stb.lib; libstb/__init__.py adds it with os.add_dll_directory
    set(_libstb_inc_dest   libstb/include)
    set(_libstb_cmake_dest libstb/cmake)
else()
    set(_libstb_lib_dest   lib)
    set(_libstb_bin_dest   bin)
    set(_libstb_inc_dest   include)
    set(_libstb_cmake_dest lib/cmake/libstb)
endif()

target_include_directories(stb_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${_libstb_inc_dest}>
)
# Everything that decides how the core sources are compiled lives in this function, so a target
# that compiles them itself (tests/cpp does, to attach coverage flags) gets exactly the same
# settings as stb_core.
function(libstb_apply_core_build_settings target)
    # Internal headers are included relative to src/ ("image/simd.hpp", "font/shaper/gsub.hpp").
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)

    # A target that compiles the core sources itself (the C++ tests) has no library to import
    # from: STB_API must be empty there. stb_core sets its own definition.
    if(NOT target STREQUAL "stb_core")
        target_compile_definitions(${target} PRIVATE STB_STATIC_DEFINE)
    endif()

    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /EHsc>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )
    # -fno-trapping-math: lets GCC/Clang vectorize float->int conversions in the JPEG encoder's
    # quantization (about 2x faster JPEG). It only means FP operations are not required to
    # raise hardware traps (nothing here enables or reads them); results are unchanged.
    target_compile_options(${target} PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-trapping-math>
    )
    apply_optimization_flags(${target})

    # shape.cpp draws with floating point and the result must be the same on every platform
    # (the C++ and Python canvas tests compare against one fixed hash), so no fused multiply-add
    # contraction: GCC and Clang would otherwise use FMA on ARM and give last-bit differences.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${PROJECT_SOURCE_DIR}/src/draw/shape.cpp
            TARGET_DIRECTORY ${target}
            PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
    endif()

    # kernels_scalar.cpp is the "scalar" SIMD backend: the same pixel loops with the
    # compiler's auto-vectoriser off. (MSVC has no such switch: there it may still vectorise,
    # with identical results.) TARGET_DIRECTORY: source file properties are per directory scope.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(${PROJECT_SOURCE_DIR}/src/image/kernels_scalar.cpp
            TARGET_DIRECTORY ${target}
            PROPERTIES COMPILE_OPTIONS "-fno-tree-vectorize;-fno-tree-slp-vectorize")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set_source_files_properties(${PROJECT_SOURCE_DIR}/src/image/kernels_scalar.cpp
            TARGET_DIRECTORY ${target}
            PROPERTIES COMPILE_OPTIONS "-fno-vectorize;-fno-slp-vectorize")
    endif()
endfunction()

libstb_apply_core_build_settings(stb_core)

# --- install (find_package(libstb) -> libstb::core) ---
include(CMakePackageConfigHelpers)
install(TARGETS stb_core EXPORT libstbTargets
    LIBRARY DESTINATION ${_libstb_lib_dest}     # libstb.so, libstb.dylib
    RUNTIME DESTINATION ${_libstb_bin_dest}     # stb.dll
    ARCHIVE DESTINATION ${_libstb_lib_dest}     # stb.lib (the import library) or libstb_core.a
)
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

if(STB_BUILD_PYTHON OR DEFINED SKBUILD)
    set(PYTHON_INSTALL_DIR ${CMAKE_BINARY_DIR}/python/install)
    add_subdirectory(${PROJECT_SOURCE_DIR}/src/bindings/python)
    if(DEFINED SKBUILD)
        return()
    endif()
endif()

if(STB_BUILD_TESTS)
    enable_testing()
    set(CMAKE_CTEST_ARGUMENTS "--output-on-failure" "-V")
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests)
endif()

# The C++ examples (one executable per examples/cpp/*.cpp), against the same library the users link.
if(STB_BUILD_EXAMPLES)
    add_subdirectory(${PROJECT_SOURCE_DIR}/examples/cpp)
endif()

# Documentation (Doxygen + Sphinx). Needs the Python extension: the Sphinx build imports it.
if(STB_BUILD_DOCS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/docs)
endif()
