cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

# Prefer the vcpkg port (header-only, imported config)
find_package(doctest CONFIG QUIET)

if(doctest_FOUND)
  add_executable(colony_smoke tests/smoke/smoke.cpp)
  target_compile_features(colony_smoke PRIVATE cxx_std_20)
  target_link_libraries(colony_smoke PRIVATE doctest::doctest)
else()
  # Fallback: header-only via FetchContent (no sub-build, no 'cmake --build')
  include(FetchContent)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
  )
  set(FETCHCONTENT_QUIET ON)
  set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
  FetchContent_Populate(doctest)
  add_executable(colony_smoke tests/smoke/smoke.cpp)
  target_compile_features(colony_smoke PRIVATE cxx_std_20)
  target_include_directories(colony_smoke PRIVATE "${doctest_SOURCE_DIR}")
endif()

add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
