# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(colony_pathfinding LANGUAGES CXX)

# ---- Options ---------------------------------------------------------------
option(COLONY_PF_BUILD_TESTS "Build pathfinding unit tests" ON)
option(COLONY_PF_BUILD_BENCH "Build pathfinding microbenchmarks" ON)

# Apply (2) PCH suggestion: opt-in, guarded by file existence.
option(COLONY_PF_USE_PCH "Use precompiled headers (target_precompile_headers)" OFF)

# Apply (3) Unity/Jumbo build suggestion per target (safe default ON, toggle if needed).
option(COLONY_PF_UNITY "Enable CMake Unity (Jumbo) builds per target" ON)

# Optional: promote warnings to errors for the pathfinding library only.
option(COLONY_PF_WERROR "Treat warnings as errors for colony_path (MSVC /WX)" OFF)

# ---- Library ---------------------------------------------------------------
add_library(colony_path STATIC
    src/GridMap.cpp
)
target_include_directories(colony_path PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(colony_path PUBLIC cxx_std_20)

# -------- Windows-only MSVC hygiene: strict conformance, clear diagnostics ---
if (MSVC)
  # (1) Conformance & warnings (per-target).
  # Keep this list concise and reusable across targets.
  set(COLONY_MSVC_OPTS
      /permissive-        # standards-conforming mode
      /W4                 # strong warnings (prefer over /Wall)
      /MP                 # parallel file compilation
      /utf-8              # UTF-8 source/execution charset
      /Zc:__cplusplus     # correct __cplusplus value
      /Zc:preprocessor    # standards-conforming preprocessor
      /Zc:inline          # remove unreferenced COMDATs with internal linkage
      /Zc:throwingNew     # assume operator new throws on failure
      /EHsc               # C++ exceptions model
      $<$<CONFIG:Debug>:/RTC1> # runtime checks in Debug
  )

  set(COLONY_MSVC_DEFS
      WIN32_LEAN_AND_MEAN # trim <Windows.h>
      NOMINMAX            # avoid macro-clobbering std::min/max
  )

  target_compile_options(colony_path PRIVATE ${COLONY_MSVC_OPTS})
  target_compile_definitions(colony_path PRIVATE ${COLONY_MSVC_DEFS})

  if (COLONY_PF_WERROR)
    target_compile_options(colony_path PRIVATE /WX)
  endif()

  # Timers & profiling switches for pathfinding (Windows-only)
  target_compile_definitions(colony_path PRIVATE
    $<$<CONFIG:Debug>:JPS_PROFILE=1;JPS_PROFILE_LOG=1;COLONY_PF_TIMERS=1>
    $<$<CONFIG:RelWithDebInfo>:JPS_PROFILE=1;COLONY_PF_TIMERS=1>
  )

  # Extra Release-only codegen/link optimization
  target_compile_options(colony_path PRIVATE
    $<$<CONFIG:Release>:/O2 /Oi /Ot /GL /Gw /Gy>
  )
  target_link_options(colony_path PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
  )
endif()

# (2) PCH: opt-in, guarded. Reused by tests/bench if enabled and header exists.
set(COLONY_PF_PCH_READY FALSE)
if (COLONY_PF_USE_PCH)
  set(PCH_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/pch.hpp")
  if (EXISTS "${PCH_HEADER}")
    target_precompile_headers(colony_path PRIVATE
      "$<$<COMPILE_LANGUAGE:CXX>:${PCH_HEADER}>"
    )
    set(COLONY_PF_PCH_READY TRUE)
  else()
    message(WARNING "COLONY_PF_USE_PCH=ON but ${PCH_HEADER} not found; skipping PCH.")
  endif()
endif()

# (3) Unity/Jumbo build: per-target property, conservative batch size.
if (COLONY_PF_UNITY)
  set_target_properties(colony_path PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 8
  )
endif()

# -------- Tests (GoogleTest) -----------------------------------------------
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
    target_link_libraries(pathfinding_tests PRIVATE colony_path GTest::gtest_main)

    if (MSVC)
      target_compile_options(pathfinding_tests PRIVATE ${COLONY_MSVC_OPTS})
      target_compile_definitions(pathfinding_tests PRIVATE ${COLONY_MSVC_DEFS})
      # Ensure LTCG at the final link step to match /GL objects in Release.
      target_link_options(pathfinding_tests PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
      )
      # Optional: uncomment if you want tests to be strict as well
      # if (COLONY_PF_WERROR)
      #   target_compile_options(pathfinding_tests PRIVATE /WX)
      # endif()
    endif()

    # Reuse PCH from the library if enabled and ready.
    if (COLONY_PF_USE_PCH AND COLONY_PF_PCH_READY)
      target_precompile_headers(pathfinding_tests REUSE_FROM colony_path)
    endif()

    # Unity build for tests, too (optional).
    if (COLONY_PF_UNITY)
      set_target_properties(pathfinding_tests PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 8
      )
    endif()

    include(GoogleTest)
    gtest_discover_tests(pathfinding_tests)
  endif()
endif()

# -------- Microbench (Google Benchmark) ------------------------------------
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
  target_link_libraries(pathfinding_bench PRIVATE colony_path benchmark::benchmark)

  if (MSVC)
    target_compile_options(pathfinding_bench PRIVATE ${COLONY_MSVC_OPTS})
    target_compile_definitions(pathfinding_bench PRIVATE ${COLONY_MSVC_DEFS})
    target_link_options(pathfinding_bench PRIVATE
      $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
    # Optional: uncomment if you want bench to be strict as well
    # if (COLONY_PF_WERROR)
    #   target_compile_options(pathfinding_bench PRIVATE /WX)
    # endif()
  endif()

  # Reuse PCH from the library if enabled and ready.
  if (COLONY_PF_USE_PCH AND COLONY_PF_PCH_READY)
    target_precompile_headers(pathfinding_bench REUSE_FROM colony_path)
  endif()

  # Unity build for bench, too (optional).
  if (COLONY_PF_UNITY)
    set_target_properties(pathfinding_bench PROPERTIES
      UNITY_BUILD ON
      UNITY_BUILD_BATCH_SIZE 8
    )
  endif()
endif()
