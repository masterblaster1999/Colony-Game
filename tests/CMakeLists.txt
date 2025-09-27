cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

# Prefer doctest from vcpkg (header-only, provides doctest::doctest INTERFACE target)
find_package(doctest CONFIG QUIET)

set(TEST_SOURCES)

# Collect explicit/known tests first
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
  list(APPEND TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
endif()

# Optionally pick up any other *.cpp in tests/ (besides smoke.cpp we already added)
file(GLOB_RECURSE _MORE_TESTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")
list(REMOVE_ITEM _MORE_TESTS "${CMAKE_SOURCE_DIR}/tests/smoke/smoke.cpp")
list(APPEND TEST_SOURCES ${_MORE_TESTS})

# If nothing exists (e.g., someone deleted smoke.cpp), generate a stub so configure never fails.
if(TEST_SOURCES STREQUAL "")
  set(_stub "${CMAKE_BINARY_DIR}/tests/auto_stub_smoke.cpp")
  file(WRITE "${_stub}" [=[
    #define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
    #include <doctest/doctest.h>
    TEST_CASE("auto-stub") { CHECK(true); }
  ]=])
  list(APPEND TEST_SOURCES "${_stub}")
  message(WARNING "No test sources found in 'tests/'. Generated a stub: ${_stub}")
endif()

add_executable(colony_smoke ${TEST_SOURCES})
target_compile_features(colony_smoke PRIVATE cxx_std_20)

if(doctest_FOUND)
  # vcpkg config path: use the INTERFACE target for include dirs/defines
  target_link_libraries(colony_smoke PRIVATE doctest::doctest)
else()
  # Fallback: header-only via FetchContent (download only; no sub-build)
  include(FetchContent)
  set(FETCHCONTENT_QUIET ON)
  set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
  )
  FetchContent_Populate(doctest)
  target_include_directories(colony_smoke PRIVATE "${doctest_SOURCE_DIR}")
endif()

# Let ctest discover and run the smoke target
enable_testing()
add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
