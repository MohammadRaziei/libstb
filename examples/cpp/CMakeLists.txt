# One executable per example: examples/cpp/tour.cpp becomes the target `tour`.
file(GLOB LIBSTB_EXAMPLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
foreach(source IN LISTS LIBSTB_EXAMPLE_SOURCES)
    get_filename_component(name ${source} NAME_WE)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE libstb::libstb)
    # The sample pictures and fonts of data/: the examples run without arguments.
    target_compile_definitions(${name} PRIVATE LIBSTB_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
endforeach()
