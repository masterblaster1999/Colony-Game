cmake_minimum_required(VERSION 3.20)
project(colony_pathfinding LANGUAGES CXX)

option(COLONY_PF_BUILD_TESTS "Build pathfinding unit tests" ON)
option(COLONY_PF_BUILD_BENCH "Build pathfinding microbenchmarks" ON)

add_library(colony_pathfinding STATIC
    src/GridMap.cpp
)
target_include_directories(colony_pathfinding PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(colony_pathfinding PUBLIC cxx_std_20)

# -------- Windows-only MSVC hygiene: strict conformance, clear diagnostics --------
if (MSVC)
  # Consolidated options & defs so we can reuse them across targets.
  set(COLONY_MSVC_OPTS
      /permissive-        # standards-conforming mode
      /W4                 # strong warnings (prefer over /Wall)
      /MP                 # parallel file compilation
      /utf-8              # UTF-8 source/execution charset
      /Zc:__cplusplus     # correct __cplusplus value
      /Zc:preprocessor    # standards-conforming preprocessor
      /EHsc               # C++ exceptions model
  )
  set(COLONY_MSVC_DEFS
      WIN32_LEAN_AND_MEAN # trim <Windows.h>
      NOMINMAX            # avoid macro-clobbering std::min/max
  )

  target_compile_options(colony_pathfinding PRIVATE ${COLONY_MSVC_OPTS})
  target_compile_definitions(colony_pathfinding PRIVATE ${COLONY_MSVC_DEFS})

  # Optional: precompiled headers for faster iteration (uncomment and provide your PCH)
  # target_precompile_headers(colony_pathfinding PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.hpp")
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

    if (MSVC)
      target_compile_options(pathfinding_tests PRIVATE ${COLONY_MSVC_OPTS})
      target_compile_definitions(pathfinding_tests PRIVATE ${COLONY_MSVC_DEFS})
    endif()

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

  if (MSVC)
    target_compile_options(pathfinding_bench PRIVATE ${COLONY_MSVC_OPTS})
    target_compile_definitions(pathfinding_bench PRIVATE ${COLONY_MSVC_DEFS})
  endif()
endif()
