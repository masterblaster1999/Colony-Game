cmake_minimum_required(VERSION 3.20)

add_library(ColonyWorldGen STATIC
    WorldGen.cpp
)

target_include_directories(ColonyWorldGen
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ColonyWorldGen PUBLIC cxx_std_17)

# If you want warnings on this target only:
if (MSVC)
    target_compile_options(ColonyWorldGen PRIVATE /W4 /permissive-)
else()
    target_compile_options(ColonyWorldGen PRIVATE -Wall -Wextra -Wpedantic)
endif()
