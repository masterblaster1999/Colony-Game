# src/path/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

add_library(colony_path STATIC
    ${CMAKE_CURRENT_LIST_DIR}/jps.cpp          # MUST contain jps_find_path definition
    ${CMAKE_CURRENT_LIST_DIR}/grid.cpp         # typical helpers
    ${CMAKE_CURRENT_LIST_DIR}/cell.cpp
    ${CMAKE_CURRENT_LIST_DIR}/options.cpp
)
target_compile_features(colony_path PUBLIC cxx_std_20)
target_include_directories(colony_path PUBLIC ${CMAKE_SOURCE_DIR}/src)
add_library(Colony::Path ALIAS colony_path)
