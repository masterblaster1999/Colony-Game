cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)

# Use modern policy behavior for predictable parsing & variable expansion
# (cmake_minimum_required sets policy defaults; these ensure key ones are NEW
# even if older bootstrap tools are used.)
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)  # Simpler variable/escape rules ($ENV{}, backslashes)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)  # Only unquoted if() args act as vars/keywords
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)  # option() honors normal variables
endif()
if(POLICY CMP0092)
  cmake_policy(SET CMP0092 NEW)  # Do not add /W3 implicitly for MSVC
endif()

# ------------------------------------------------------------------------------
# Make project-local modules discoverable
# ------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------------------
# Policies & vcpkg toolchain must be configured before project()/language enable
# ------------------------------------------------------------------------------
include(CGPolicies)   # your cmake/CGPolicies.cmake
include(CGToolchain)  # your cmake/CGToolchain.cmake (e.g., vcpkg toolchain)

project(ColonyGame LANGUAGES CXX)

# Helpful for IDE tooling (JSON compile commands) and folder organization
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Back-compat & preset hygiene
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Map legacy options (safe to remove later)
if(DEFINED CG_ENABLE_PCH AND NOT DEFINED COLONY_USE_PCH)
  set(COLONY_USE_PCH "${CG_ENABLE_PCH}" CACHE BOOL "Enable precompiled headers" FORCE)
endif()
if(DEFINED CG_ENABLE_UNITY AND NOT DEFINED COLONY_UNITY_BUILD)
  set(COLONY_UNITY_BUILD "${CG_ENABLE_UNITY}" CACHE BOOL "Enable unity (jumbo) builds" FORCE)
endif()

# ------------------------------------------------------------------------------
# 11.1: ASan, PCH, and clang-tidy toggles (Windows-first)
# ------------------------------------------------------------------------------
if(NOT DEFINED COLONY_USE_PCH)
  option(COLONY_USE_PCH "Enable precompiled headers" ON)
endif()
set(COLONY_PCH_HEADER "src/pch.hpp" CACHE STRING "Path to the common precompiled header used by game targets")

option(COLONY_ENABLE_TIDY "Run clang-tidy static analysis during build (may slow builds)" OFF)
option(COLONY_ENABLE_ASAN "Enable AddressSanitizer for supported compilers/configs" OFF)
option(COLONY_SEH_CRASHDUMP "Compile CrashDumpWin.cpp with /EHa while keeping /EHsc globally" ON)

# NEW: Make Tracy optional (OFF by default)
option(COLONY_ENABLE_TRACY "Build with Tracy profiler instrumentation (vcpkg port 'tracy')" OFF)

# ------------------------------------------------------------------------------
# clang-tidy
# ------------------------------------------------------------------------------
if(COLONY_ENABLE_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy.exe)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};-warnings-as-errors=*")
  else()
    message(WARNING "COLONY_ENABLE_TIDY is ON, but clang-tidy was not found on PATH.")
  endif()
endif()

# ------------------------------------------------------------------------------
# AddressSanitizer (MSVC & clang-cl)
# ------------------------------------------------------------------------------
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
# Windows macros and compiler hygiene
# ------------------------------------------------------------------------------
if(MSVC)
  add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
  )
  add_compile_options(
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /EHsc
    /utf-8
    /W4
    /MP
  )
  # CI-only: treat warnings as errors (local builds remain friendly)
  if(DEFINED ENV{GITHUB_ACTIONS} OR DEFINED ENV{CI})
    add_compile_options(/WX)
  endif()

  # Use the supported abstraction instead of editing flags
  # (CMake >= 3.15; default is MultiThreadedDLL in Release, MultiThreadedDebugDLL in Debug)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# ------------------------------------------------------------------------------
# Project modules
# ------------------------------------------------------------------------------
include(CGOptions)     # your cmake/CGOptions.cmake
include(CGThirdParty)  # your cmake/CGThirdParty.cmake
include(CGShaders)     # your cmake/CGShaders.cmake (defines cg_compile_hlsl, cg_link_shaders_to_target)

# ------------------------------------------------------------------------------
# (PATCH) Wire shader compilation (Windows-only)
# ------------------------------------------------------------------------------
if(WIN32)
  # Collect all HLSL sources from the repo's shader folders
  file(GLOB_RECURSE COLONY_HLSL CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/*.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/*.hlsl"
  )

  if(COLONY_HLSL)
    # Set per-file shader metadata for HLSL files whose entry point
    # is not named 'main' (e.g. VSMain / PSMain / CSMain).
    if(COMMAND cg_set_hlsl_properties)
      # Atmosphere & sky rendering (fullscreen PS entry)
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"
        ENTRY   "PSMain"
        PROFILE "ps_5_0"
      )

      # Volumetric clouds (fullscreen PS entry)
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/VolumetricClouds.hlsl"
        ENTRY   "PSMain"
        PROFILE "ps_5_0"
      )

      # Voxel isosurface compute shaders
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT.compute.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_5_0"
      )

      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_5_0"
      )

      # NEW: GPU thermal erosion compute shaders under renderer/Shaders
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_5_0"
      )

      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_5_0"
      )

      # Legacy thermal erosion compute shaders under /shaders
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/shaders/ThermalOutflowCS.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_5_0"
      )

      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/shaders/ThermalApplyCS.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_5_0"
      )
    endif()

    # Compile shaders into a dedicated target; we'll link/copy them to the exe later
    cg_compile_hlsl(ColonyShaders
      SHADERS      ${COLONY_HLSL}
      INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/renderer/Shaders" "${CMAKE_SOURCE_DIR}/shaders"
      # 1 in Debug, 0 otherwise — expand at build time so multi-config IDEs work
      DEFINES      "COLONY_DEBUG=$<IF:$<CONFIG:Debug>,1,0>"
    )
    # Stash the shader target name globally so we can attach it to the final exe once it exists
    set_property(GLOBAL PROPERTY COLONY_SHADERS_TARGET ColonyShaders)
  endif()
endif()

# ------------------------------------------------------------------------------
# vcpkg dependencies (imported targets)
# ------------------------------------------------------------------------------
find_package(SDL2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# Tracy wiring (optional)
# Prefer the lowercase 'tracy' package name (vcpkg port), but accept 'Tracy' too.
# When COLONY_ENABLE_TRACY=ON, we require it; otherwise we try quietly and skip if absent.
set(_CG_TRACY_REQUEST_MODE QUIET)
if(COLONY_ENABLE_TRACY)
  set(_CG_TRACY_REQUEST_MODE REQUIRED)
endif()

# Try lowercase first (vcpkg port name)
find_package(tracy CONFIG ${_CG_TRACY_REQUEST_MODE})
# Fallback to capitalized package name if not found
if(NOT tracy_FOUND)
  find_package(Tracy CONFIG ${_CG_TRACY_REQUEST_MODE})
endif()

# Normalize the imported target name if present
set(COLONY_TRACY_TARGET "")
if(TARGET tracy::TracyClient)
  set(COLONY_TRACY_TARGET tracy::TracyClient)
elseif(TARGET Tracy::TracyClient)
  set(COLONY_TRACY_TARGET Tracy::TracyClient)
endif()

# If the option is ON but no client target was found, fail early
if(COLONY_ENABLE_TRACY AND NOT COLONY_TRACY_TARGET)
  message(FATAL_ERROR
    "COLONY_ENABLE_TRACY=ON but Tracy client target was not found.\n"
    "Install the vcpkg port 'tracy' (manifest mode recommended) or vendor Tracy.\n"
    "See: https://vcpkg.io/en/package/tracy")
endif()

# ------------------------------------------------------------------------------
# Core game source tree
# ------------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

# ------------------------------------------------------------------------------
# Link third-party dependencies
# ------------------------------------------------------------------------------
if(TARGET colony_core)
  target_link_libraries(colony_core PRIVATE
    SDL2::SDL2
    SDL2::SDL2main
    imgui::imgui
  )

  # Link Tracy only when enabled and available
  if(COLONY_TRACY_TARGET)
    if(COLONY_ENABLE_TRACY)
      target_link_libraries(colony_core PRIVATE ${COLONY_TRACY_TARGET})
      target_compile_definitions(colony_core PRIVATE TRACY_ENABLE=1)
      # Ensure include directories from the imported target are visible, just in case
      get_target_property(_TRACY_INC ${COLONY_TRACY_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
      if(_TRACY_INC)
        target_include_directories(colony_core PRIVATE ${_TRACY_INC})
      endif()
    endif()
  endif()

  # --------------------------------------------------------------------------
  # Compile CrashDumpWin.cpp with /EHa (Structured Exception Handling)
  # --------------------------------------------------------------------------
  if(MSVC AND COLONY_SEH_CRASHDUMP)
    get_target_property(_core_srcs colony_core SOURCES)
    if(_core_srcs)
      foreach(_src IN LISTS _core_srcs)
        if(_src MATCHES "CrashDumpWin\\.cpp$")  # exact filename match
          set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "/EHa")
        endif()
      endforeach()
    endif()
  endif()
endif()

# ------------------------------------------------------------------------------
# Procedural generation add-ons (procgen)
# ------------------------------------------------------------------------------
add_library(procgen
  src/procgen/WorldGen.cpp
  src/procgen/Rivers.cpp
  src/procgen/PoissonDisk.cpp
  src/procgen/ThermalErosion.cpp
  src/procgen/ResourceGen.cpp
  src/procgen/SettlementGen.cpp
)
target_include_directories(procgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Link the add-ons into the core/game target if it exists
if(TARGET colony_core)
  target_link_libraries(colony_core PRIVATE procgen)
endif()

# ------------------------------------------------------------------------------
# Generate the final game target, install rules, tests, summary
# ------------------------------------------------------------------------------
include(CGGameTarget)   # your cmake/CGGameTarget.cmake

# ------------------------------------------------------------------------------
# (PATCH) After the executable exists, link/copy compiled shaders next to it
# ------------------------------------------------------------------------------
# Helper: recursively discover the first EXECUTABLE target starting at a dir
function(_cg_first_exe_in_dir out_var start_dir)
  # Targets defined directly in this directory
  get_property(_tgts DIRECTORY "${start_dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_t IN LISTS _tgts)
    get_target_property(_type "${_t}" TYPE)
    if(_type STREQUAL "EXECUTABLE")
      set(${out_var} "${_t}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  # Recurse into processed subdirectories
  get_property(_subs DIRECTORY "${start_dir}" PROPERTY SUBDIRECTORIES)
  foreach(_sd IN LISTS _subs)
    _cg_first_exe_in_dir(_found "${_sd}")
    if(_found)
      set(${out_var} "${_found}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------------------------
# (PATCH A) Job System — add directly to the main executable target
# ------------------------------------------------------------------------------
# This attaches the tiny Job System to your first executable target and exposes
# the public headers under ${CMAKE_SOURCE_DIR}/include. If you haven't created
# the files yet, you'll see a friendly warning.
_cg_first_exe_in_dir(_CG_EXE_TGT_JOBS "${CMAKE_SOURCE_DIR}")
if(_CG_EXE_TGT_JOBS)
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/core/JobSystem.cpp")
    target_sources(${_CG_EXE_TGT_JOBS} PRIVATE
      src/core/JobSystem.cpp
    )
    target_include_directories(${_CG_EXE_TGT_JOBS} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
  else()
    message(WARNING "JobSystem: src/core/JobSystem.cpp not found. Create it (and include/core/JobSystem.h) to enable the tiny job system.")
  endif()
else()
  message(WARNING "JobSystem: No executable target found; cannot attach JobSystem.cpp.")
endif()

if(WIN32)
  # Retrieve the shader target we created earlier (if any)
  get_property(_CG_SHADER_TGT GLOBAL PROPERTY COLONY_SHADERS_TARGET)
  if(_CG_SHADER_TGT)
    # Find an executable target to attach shaders to (walk from top-level ".")
    _cg_first_exe_in_dir(_CG_EXE_TGT "${CMAKE_SOURCE_DIR}")
    if(_CG_EXE_TGT)
      cg_link_shaders_to_target(${_CG_SHADER_TGT} ${_CG_EXE_TGT})
    else()
      message(WARNING "CGShaders: No executable target found to link shaders to; compiled shaders will not be copied.")
    endif()
  endif()
endif()

include(CGStageInstall) # your cmake/CGStageInstall.cmake
include(CGTests)        # your cmake/CGTests.cmake
include(CGSummary)      # your cmake/CGSummary.cmake
