cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

include(FetchContent)

# --------------------------------------------------------------
# doctest (header-only) as an INTERFACE target
# --------------------------------------------------------------
if(NOT TARGET doctest)
    find_package(doctest CONFIG QUIET)
    if(TARGET doctest::doctest)
        add_library(doctest INTERFACE)
        target_link_libraries(doctest INTERFACE doctest::doctest)
    else()
        set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
        FetchContent_Declare(doctest_upstream
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG        v2.4.11
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(doctest_upstream)

        add_library(doctest INTERFACE)
        target_include_directories(doctest INTERFACE
            "${doctest_upstream_SOURCE_DIR}"
        )
    endif()
endif()

# --------------------------------------------------------------
# PCG RNG (header-only)
# --------------------------------------------------------------
if(NOT TARGET pcg)
    find_package(pcg CONFIG QUIET)
    if(NOT TARGET pcg)
        find_path(PCG_INCLUDE_DIR NAMES pcg_random.hpp)
        if(PCG_INCLUDE_DIR)
            add_library(pcg INTERFACE)
            target_include_directories(pcg INTERFACE "${PCG_INCLUDE_DIR}")
        else()
            set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
            FetchContent_Declare(pcg_cpp
                GIT_REPOSITORY https://github.com/imneme/pcg-cpp.git
                GIT_TAG        master
                GIT_SHALLOW    TRUE
            )
            FetchContent_MakeAvailable(pcg_cpp)

            add_library(pcg INTERFACE)
            target_include_directories(pcg INTERFACE
                "${pcg_cpp_SOURCE_DIR}/include"
            )
        endif()
    endif()
endif()

# --------------------------------------------------------------
# Test sources & single runner
# --------------------------------------------------------------
set(TEST_SOURCES)

# Runner TU (exactly one main to avoid LNK2005)
set(_runner "${CMAKE_BINARY_DIR}/tests/test_main.cpp")
file(WRITE "${_runner}" [=[
    #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    #include <doctest/doctest.h>
]=])
list(APPEND TEST_SOURCES "${_runner}")

# Add all tests (except any old runner or smoke with another main)
file(GLOB_RECURSE _MORE_TESTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tests/*.cpp"
)
list(FILTER _MORE_TESTS EXCLUDE REGEX ".*/(test_)?main\\.cpp$")
list(REMOVE_ITEM _MORE_TESTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
list(APPEND TEST_SOURCES ${_MORE_TESTS})

add_executable(colony_smoke ${TEST_SOURCES})
target_compile_features(colony_smoke PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(colony_smoke PRIVATE /utf-8 /W4 /permissive-)
else()
    target_compile_options(colony_smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_include_directories(colony_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/worldgen
)

# Link production libs if present
if(TARGET colony_build_options)
    target_link_libraries(colony_smoke PRIVATE colony_build_options)
endif()
if(TARGET colony_core)
    target_link_libraries(colony_smoke PRIVATE colony_core)
endif()
if(TARGET procgen)
    target_link_libraries(colony_smoke PRIVATE procgen)
endif()

# Pathfinding: prefer the real target; otherwise use OBJECT fallback
if(TARGET colony_path)
    target_link_libraries(colony_smoke PRIVATE colony_path)
elseif(TARGET colony::path)
    target_link_libraries(colony_smoke PRIVATE colony::path)
else()
    file(GLOB_RECURSE COLONY_TEST_PATH_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/pathfinding/*.cpp"
        "${CMAKE_SOURCE_DIR}/hpa/*.cpp"
    )
    if(COLONY_TEST_PATH_SOURCES)
        add_library(colony_path_obj OBJECT ${COLONY_TEST_PATH_SOURCES})
        target_include_directories(colony_path_obj PUBLIC
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/include"
            "${CMAKE_SOURCE_DIR}/pathfinding"
            "${CMAKE_SOURCE_DIR}/hpa"
        )
        target_compile_features(colony_path_obj PUBLIC cxx_std_20)

        # Bring the objects into the test exe without creating a new target name
        target_sources(colony_smoke PRIVATE $<TARGET_OBJECTS:colony_path_obj>)
    endif()
endif()

# Header-only deps
target_link_libraries(colony_smoke PRIVATE pcg doctest)

# --------------------------------------------------------------
# CTest wiring
# --------------------------------------------------------------
enable_testing()
add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
