cmake_minimum_required(VERSION 3.20)
project(colony_pathfinding LANGUAGES CXX)

option(COLONY_PF_BUILD_TESTS "Build pathfinding unit tests" ON)
option(COLONY_PF_BUILD_BENCH "Build pathfinding microbenchmarks" ON)

add_library(colony_pathfinding STATIC
    src/GridMap.cpp
)
target_include_directories(colony_pathfinding PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(colony_pathfinding PUBLIC cxx_std_20)
if (MSVC)
  target_compile_options(colony_pathfinding PUBLIC /W4 /permissive- /EHsc)
endif()

# -------- Tests (GoogleTest) ----------
if (COLONY_PF_BUILD_TESTS)
  include(CTest)
  if (BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE) # MSVC: use /MD
    FetchContent_MakeAvailable(googletest)

    add_executable(pathfinding_tests
      tests/test_astar.cpp
      tests/test_jps.cpp
      tests/test_integration.cpp
    )
    target_link_libraries(pathfinding_tests PRIVATE colony_pathfinding GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(pathfinding_tests)
  endif()
endif()

# -------- Microbench (Google Benchmark) ----------
if (COLONY_PF_BUILD_BENCH)
  include(FetchContent)
  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googlebenchmark)

  add_executable(pathfinding_bench bench/bench_pathfinding.cpp)
  target_link_libraries(pathfinding_bench PRIVATE colony_pathfinding benchmark::benchmark)
endif()
