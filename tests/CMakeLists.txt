cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

include(FetchContent)

# --------------------------------------------------------------
# CTest wiring (make tests visible to ctest from the build root)
# --------------------------------------------------------------
include(CTest)            # defines BUILD_TESTING and auto-calls enable_testing() unless BUILD_TESTING=OFF
enable_testing()          # ensure a CTestTestfile.cmake exists for this dir (important for ctest)  # docs: cmake.org/cmake/help/latest/command/enable_testing.html

if(NOT BUILD_TESTING)
    message(STATUS "BUILD_TESTING=OFF => skipping unit tests configuration")
    return()
endif()

# --------------------------------------------------------------
# doctest (header-only) as an INTERFACE target
# --------------------------------------------------------------
if(NOT TARGET doctest)
    find_package(doctest CONFIG QUIET)
    if(TARGET doctest::doctest)
        # Use the imported target from a package manager / system install.
        add_library(doctest INTERFACE)
        target_link_libraries(doctest INTERFACE doctest::doctest)
        # Imported targets should propagate INTERFACE_INCLUDE_DIRECTORIES.
        # Nothing else is needed here.
    else()
        # FetchContent fallback: bring in upstream and publish its includes
        set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
        FetchContent_Declare(doctest_upstream
            GIT_REPOSITORY https://github.com/doctest/doctest.git
            GIT_TAG        v2.4.11
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(doctest_upstream)

        add_library(doctest INTERFACE)
        # Publish the repository root so <doctest/doctest.h> works.
        target_include_directories(doctest INTERFACE
            "${doctest_upstream_SOURCE_DIR}"
        )
    endif()
endif()

# Try to load doctest's CMake helper for test discovery if we fetched the repo.
# (Falls back to a manual add_test() if the module isn't available.)
set(_DOCTEST_CMAKE_FOUND OFF)
if(DEFINED doctest_upstream_SOURCE_DIR AND EXISTS "${doctest_upstream_SOURCE_DIR}/scripts/cmake/doctest.cmake")
    list(APPEND CMAKE_MODULE_PATH "${doctest_upstream_SOURCE_DIR}/scripts/cmake")
    include(doctest)      # defines doctest_discover_tests()
    set(_DOCTEST_CMAKE_FOUND ON)
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

# Runner TU (exactly one main to avoid multiple definitions)
set(_runner "${CMAKE_CURRENT_BINARY_DIR}/test_main.cpp")
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

# --------------------------------------------------------------
# Unity builds:
# The root project may enable UNITY_BUILD globally for speed.
# Tests frequently define helper types with common names across multiple .cpp files.
# When unity builds are enabled, multiple test .cpp files can be merged into one TU,
# causing ODR/redefinition errors (e.g., 'TestGrid' defined in test_jps.cpp and test_nav.cpp).
# Disable unity builds for this test target to keep each test .cpp isolated.
# --------------------------------------------------------------
set_target_properties(colony_smoke PROPERTIES UNITY_BUILD OFF)

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

# ------------------------------------------------------------------
# Link pathfinding into tests (and also the game exe) if available.
# ------------------------------------------------------------------
if(TARGET Colony::Path)
    target_link_libraries(colony_smoke PRIVATE Colony::Path)
elseif(TARGET colony_path)
    target_link_libraries(colony_smoke PRIVATE colony_path)
elseif(TARGET colony::path)
    target_link_libraries(colony_smoke PRIVATE colony::path)
else()
    # OBJECT fallback for CI/dev when the lib isn't defined as a target yet
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
        target_sources(colony_smoke PRIVATE $<TARGET_OBJECTS:colony_path_obj>)
    endif()
endif()

# Also link the main game executable if it exists (safe, conditional).
if(TARGET ColonyGame)
    if(TARGET Colony::Path)
        target_link_libraries(ColonyGame PRIVATE Colony::Path)
    elseif(TARGET colony_path)
        target_link_libraries(ColonyGame PRIVATE colony_path)
    elseif(TARGET colony::path)
        target_link_libraries(ColonyGame PRIVATE colony::path)
    endif()
elseif(TARGET colony_game)
    if(TARGET Colony::Path)
        target_link_libraries(colony_game PRIVATE Colony::Path)
    elseif(TARGET colony_path)
        target_link_libraries(colony_game PRIVATE colony_path)
    elseif(TARGET colony::path)
        target_link_libraries(colony_game PRIVATE colony::path)
    endif()
endif()

# Header-only deps (publish include dirs transitively via INTERFACE)
target_link_libraries(colony_smoke PRIVATE pcg doctest)

# ---- Ensure the upstream doctest header is visible on the test target ----
# If we fetched doctest ourselves, add its repo root to the test's includes so
# <doctest/doctest.h> is always found, even with unusual toolchains.
if(DEFINED doctest_upstream_SOURCE_DIR)
    target_include_directories(colony_smoke PRIVATE "${doctest_upstream_SOURCE_DIR}")
endif()

# --------------------------------------------------------------
# Test registration with CTest
# Prefer doctest_discover_tests(); fall back to a single add_test()
# --------------------------------------------------------------
if(_DOCTEST_CMAKE_FOUND AND COMMAND doctest_discover_tests)
    # Enumerate each TEST_CASE into its own CTest entry; no reconfigure needed when tests change.
    doctest_discover_tests(colony_smoke
        TEST_PREFIX      "smoke."
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DISCOVERY_MODE   POST_BUILD
    )
else()
    add_test(NAME colony_smoke COMMAND colony_smoke)
    set_tests_properties(colony_smoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

# --------------------------------------------------------------
# Bridge: if the root didn't call enable_testing(), create a root
# CTestTestfile.cmake that forwards to this tests dir so that:
#   ctest --test-dir build -C Release
# still finds the tests. (No effect if root already has one.)
# --------------------------------------------------------------
if(NOT EXISTS "${CMAKE_BINARY_DIR}/CTestTestfile.cmake")
    file(RELATIVE_PATH _rel_from_root "${CMAKE_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    file(WRITE "${CMAKE_BINARY_DIR}/CTestTestfile.cmake" "subdirs(\"${_rel_from_root}\")\n")
endif()
