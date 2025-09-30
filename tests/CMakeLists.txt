cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

# Prefer doctest from vcpkg; fall back to header-only FetchContent (no sub-build)
find_package(doctest CONFIG QUIET)

set(TEST_SOURCES)

# Optionally include a smoke test if present (may define its own doctest main)
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
  list(APPEND TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
endif()

# Collect the rest of the tests (including worldgen/test_math_and_noise.cpp)
file(GLOB_RECURSE _MORE_TESTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")
# Avoid duplicating the smoke file in the list
list(REMOVE_ITEM _MORE_TESTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
list(APPEND TEST_SOURCES ${_MORE_TESTS})

# If both our worldgen doctest (with its own main) and smoke.cpp exist, prefer worldgen and drop smoke to avoid multiple mains.
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

add_executable(colony_smoke ${TEST_SOURCES})
target_compile_features(colony_smoke PRIVATE cxx_std_20)

# Make doctest compile as UTF-8 too + tighten warnings on MSVC
if(MSVC)
  target_compile_options(colony_smoke PRIVATE /utf-8 /W4 /permissive-)
endif()

# Include production headers so tests can include repo headers directly
target_include_directories(colony_smoke PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/worldgen
)

# If your repo defines compile flags/defines in an interface target, pick it up (optional, guarded)
if(TARGET colony_build_options)
  target_link_libraries(colony_smoke PRIVATE colony_build_options)
endif()

# Link doctest if available; otherwise fetch the header-only variant
if(doctest_FOUND)
  target_link_libraries(colony_smoke PRIVATE doctest::doctest)
else()
  include(FetchContent)
  set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
  )
  FetchContent_Populate(doctest)
  target_include_directories(colony_smoke PRIVATE "${doctest_SOURCE_DIR}")
endif()

enable_testing()
add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
