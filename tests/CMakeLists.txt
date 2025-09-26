cmake_minimum_required(VERSION 3.24)
project(ColonyGameTests LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest) # header-only

add_executable(colony_smoke tests/smoke/smoke.cpp)
target_include_directories(colony_smoke PRIVATE ${doctest_SOURCE_DIR})
target_compile_features(colony_smoke PRIVATE cxx_std_20)

add_test(NAME colony_smoke COMMAND colony_smoke)
set_tests_properties(colony_smoke PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
