cmake_minimum_required(VERSION 3.27)

# ------------------------------------------------------------------------------
# Make project-local modules discoverable
# ------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------------------
# Policies & vcpkg toolchain must be configured before project()/language enable
# ------------------------------------------------------------------------------
include(CGPolicies)
include(CGToolchain)

project(ColonyGame LANGUAGES CXX)

# ------------------------------------------------------------------------------
# Back-compat & preset hygiene
# - Force C++23 to match project policy and avoid mismatches from older presets.
# - Accept older presets that used CG_ENABLE_* and map them to the real options.
#   (Safe to remove this block once all presets are updated.)
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Older presets used CG_ENABLE_PCH / CG_ENABLE_UNITY; real options are
# COLONY_USE_PCH / COLONY_UNITY_BUILD. Map them if present.
if(DEFINED CG_ENABLE_PCH AND NOT DEFINED COLONY_USE_PCH)
  set(COLONY_USE_PCH "${CG_ENABLE_PCH}" CACHE BOOL "Enable precompiled headers" FORCE)
endif()
if(DEFINED CG_ENABLE_UNITY AND NOT DEFINED COLONY_UNITY_BUILD)
  set(COLONY_UNITY_BUILD "${CG_ENABLE_UNITY}" CACHE BOOL "Enable unity (jumbo) builds" FORCE)
endif()

# ------------------------------------------------------------------------------
# 11.1: ASan, PCH, and clang-tidy toggles (Windows-first)
# - COLONY_USE_PCH: already supported by project; provide a default + header path.
# - COLONY_ENABLE_TIDY: wire up clang-tidy if found (off by default).
# - COLONY_ENABLE_ASAN: enable AddressSanitizer for MSVC/Clang on Windows.
# ------------------------------------------------------------------------------
if(NOT DEFINED COLONY_USE_PCH)
  option(COLONY_USE_PCH "Enable precompiled headers" ON)
endif()
set(COLONY_PCH_HEADER "src/pch.hpp" CACHE STRING "Path to the common precompiled header used by game targets")

option(COLONY_ENABLE_TIDY "Run clang-tidy static analysis during build (may slow builds)" OFF)
option(COLONY_ENABLE_ASAN "Enable AddressSanitizer for supported compilers/configs" OFF)

# clang-tidy (global, applies to targets created after this point)
if(COLONY_ENABLE_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy.exe)
  if(CLANG_TIDY_EXE)
    # Treat all diagnostics as errors inside tidy to keep the codebase clean.
    # Projects can override per-target if needed.
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-warnings-as-errors=*")
  else()
    message(WARNING "COLONY_ENABLE_TIDY is ON, but clang-tidy was not found on PATH.")
  endif()
endif()

# AddressSanitizer (directory-wide flags; affect targets created later)
# NOTE:
#   * MSVC supports /fsanitize=address for x64. Prefer Debug/RelWithDebInfo.
#   * clang-cl uses the GCC-style -fsanitize=address flags.
if(COLONY_ENABLE_ASAN)
  if(MSVC)
    add_compile_options(
      $<$<CONFIG:Debug,RelWithDebInfo>:/fsanitize=address>
      $<$<CONFIG:Debug,RelWithDebInfo>:/Zi>
    )
    add_link_options(
      $<$<CONFIG:Debug,RelWithDebInfo>:/fsanitize=address>
    )
    add_compile_definitions(ASAN_ENABLED=1)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    add_compile_definitions(ASAN_ENABLED=1)
  endif()
endif()

# ------------------------------------------------------------------------------
# Windows: unify common macros & MSVC options at the top level.
# This suppresses per-file #define spam (WIN32_LEAN_AND_MEAN/NOMINMAX/etc.)
# and ensures consistent compiler behavior across all subdirectories/targets.
# ------------------------------------------------------------------------------
if(MSVC)
  add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
  )
  add_compile_options(
    /permissive-        # conforming mode
    /Zc:__cplusplus     # proper __cplusplus
    /Zc:preprocessor    # modern preprocessor
    /EHsc               # C++ exceptions (synchronous)
    /utf-8              # UTF-8 source and execution char set
    /W4                 # strong warnings
    /MP                 # parallel compilation
  )
endif()

# ------------------------------------------------------------------------------
# Modules: options, third-party deps, shader pipeline
# ------------------------------------------------------------------------------
include(CGOptions)        # options, MSVC defaults, helpers
include(CGThirdParty)     # SDL2, ImGui, Tracy, DXC (optional)
include(CGShaders)        # unified HLSL pipeline (VS/MSBuild, FXC, DXC)

# ------------------------------------------------------------------------------
# Manual vcpkg package imports (SDL2, ImGui, Tracy)
# - Kept explicit here so the top-level always knows about these key deps.
# - These work with vcpkg manifest mode (CONFIG packages).
# ------------------------------------------------------------------------------
find_package(SDL2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Tracy CONFIG QUIET)  # optional; only links if present

# ------------------------------------------------------------------------------
# NOTE: Stop defining/creating core/platform targets at the top level.
# Those targets must be defined once inside subdirectories to avoid CMP0002
# collisions.
# (Removed: include(CGCoreTargets))
# ------------------------------------------------------------------------------

# ------------------------------------------------------------------------------
# Your real game target (and libraries) are defined under src/
# ------------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

# ------------------------------------------------------------------------------
# Link third-party libs to the core game target once it's defined
# ------------------------------------------------------------------------------
if(TARGET colony_core)
  target_link_libraries(colony_core PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    imgui::imgui
  )
  if(TARGET Tracy::TracyClient)
    target_link_libraries(colony_core PRIVATE Tracy::TracyClient)
    target_compile_definitions(colony_core PRIVATE TRACY_ENABLE=1)
  endif()
endif()

# ------------------------------------------------------------------------------
# Target finalization, staging/installer, tests, and summary
# ------------------------------------------------------------------------------
include(CGGameTarget)     # link libs, attach shaders, per-target PCH/defs
include(CGStageInstall)   # stage_win target + installer rules
include(CGTests)          # tests if available
include(CGSummary)        # final pretty summary
