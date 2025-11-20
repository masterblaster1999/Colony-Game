cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

# We'll use FetchContent for header-only fallbacks when packages aren't available.
include(FetchContent)

# --------------------------------------------------------------------------------------
# PCG RNG: create a header-only INTERFACE target named 'pcg' (self-sufficient)
# Try config package, then system include discovery, then FetchContent (imneme/pcg-cpp).
# --------------------------------------------------------------------------------------
if(NOT TARGET pcg)
  find_package(pcg CONFIG QUIET)
endif()

if(NOT TARGET pcg)
  # Try to find an installed header (works when vcpkg or system includes are present)
  find_path(PCG_INCLUDE_DIR NAMES pcg_random.hpp)
  if(PCG_INCLUDE_DIR)
    add_library(pcg INTERFACE)
    target_include_directories(pcg INTERFACE "${PCG_INCLUDE_DIR}")
  else()
    # FetchContent fallback: upstream header-only reference implementation
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(pcg_cpp
      GIT_REPOSITORY https://github.com/imneme/pcg-cpp.git
      GIT_TAG        master
      GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(pcg_cpp)

    add_library(pcg INTERFACE)
    target_include_directories(pcg INTERFACE "${pcg_cpp_SOURCE_DIR}/include")
  endif()
endif()

# --------------------------------------------------------------------------------------
# Gather tests
# --------------------------------------------------------------------------------------
set(TEST_SOURCES)

# Optional smoke test
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
  list(APPEND TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
endif()

# Collect the rest (e.g., tests/worldgen/test_math_and_noise.cpp, etc.)
file(GLOB_RECURSE _MORE_TESTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")
list(REMOVE_ITEM _MORE_TESTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
list(APPEND TEST_SOURCES ${_MORE_TESTS})

# If both our worldgen doctest (with its own main) and smoke.cpp exist, avoid duplicate mains.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/worldgen/test_math_and_noise.cpp" AND
   EXISTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
  list(REMOVE_ITEM TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
endif()

# Auto-stub if no tests were found
if(TEST_SOURCES STREQUAL "")
  set(_stub "${CMAKE_BINARY_DIR}/tests/auto_stub_smoke.cpp")
  file(WRITE "${_stub}" [=[
    #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    #include <doctest/doctest.h>
    TEST_CASE("auto-stub") { CHECK(true); }
  ]=])
  list(APPEND TEST_SOURCES "${_stub}")
  message(WARNING "No tests found; generated a stub: ${_stub}")
endif()

# --------------------------------------------------------------------------------------
# Build the unified test executable
# --------------------------------------------------------------------------------------
add_executable(colony_smoke ${TEST_SOURCES})
target_compile_features(colony_smoke PRIVATE cxx_std_23)

# Windows/MSVC friendliness; reasonable defaults elsewhere
if(MSVC)
  target_compile_options(colony_smoke PRIVATE /utf-8 /W4 /permissive-)
else()
  target_compile_options(colony_smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Include production headers (so tests can include repo headers directly)
# + external/ so doctest is visible as <doctest/doctest.h>
target_include_directories(colony_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/worldgen
  ${CMAKE_SOURCE_DIR}/external      # provides <doctest/doctest.h>
)

# If your repo defines common compile flags/defines in an interface target, pick it up (optional)
if(TARGET colony_build_options)
  target_link_libraries(colony_smoke PRIVATE colony_build_options)
endif()

# Link optional engine libs if present (harmless if they don't exist)
if(TARGET procgen)
  target_link_libraries(colony_smoke PRIVATE procgen)
endif()
if(TARGET colony_core)
  target_link_libraries(colony_smoke PRIVATE colony_core)
endif()

# Link the (header-only) PCG target we created/discovered above
target_link_libraries(colony_smoke PRIVATE pcg)

# --------------------------------------------------------------------------------------
# CTest integration
# --------------------------------------------------------------------------------------
enable_testing()
add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
