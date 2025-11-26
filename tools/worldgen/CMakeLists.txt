cmake_minimum_required(VERSION 3.20)

# Builds a small generator tool that only uses the C++ standard library.
project(worldgen_tools LANGUAGES CXX)

add_executable(worldgen
    worldgen_main.cpp
    u16_raster.cpp
)
target_include_directories(worldgen PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_features(worldgen PRIVATE cxx_std_17)

# Put the tool under <build>/bin for convenience
set_target_properties(worldgen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
