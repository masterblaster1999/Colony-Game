cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)

# ------------------------------------------------------------------------------
# Windows-only: hard-fail on non-Windows platforms
# ------------------------------------------------------------------------------
if(NOT WIN32)
  message(FATAL_ERROR "Colony-Game currently only supports Windows.")
endif()

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
# Use first-class MSVC runtime selection (controls /MT, /MD, etc.)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
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

# Configure HLSL shader entrypoints for Visual Studio generator
# (see cmake/ShaderEntryPoints.cmake)
include("${CMAKE_SOURCE_DIR}/cmake/ShaderEntryPoints.cmake")

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
# Build options added by patch
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

# Added by patch: tests & CRT toggles and optional game target name
option(COLONY_BUILD_TESTS "Build unit tests in tests/ subdir (single-runner pattern)" ON)
option(COLONY_RELEASE_STATIC_CRT
  "Use static MSVC runtime (/MT) in Release; keep dynamic (/MDd) in Debug" OFF)
set(COLONY_GAME_TARGET "" CACHE STRING
  "Name of the game's main executable target (if different from 'ColonyGame') to link with colony_path")

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
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Zi>
    )
    # ASan is not compatible with incremental linking: explicitly disable it.
    add_link_options(
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/INCREMENTAL:NO>
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
    /W4           # high warning level
    /WX           # treat warnings as errors (non-optional)
    /permissive-  # strict C++ conformance
    /Zc:__cplusplus
    /Zc:preprocessor
    /EHsc
    /utf-8
    /MP
  )

  # (PATCH) Generate full PDBs and cl debug info in Debug/RelWithDebInfo
  add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Zi>)
  add_link_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/DEBUG:FULL>)

  # Use the supported abstraction instead of editing flags
  # (CMake >= 3.15; default is MultiThreadedDLL in Release, MultiThreadedDebugDLL in Debug)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Helper to apply our CRT policy per-target (handles "Release static; Debug dynamic").
function(colony_apply_msvc_runtime tgt)
  if(MSVC AND TARGET "${tgt}")
    if(COLONY_RELEASE_STATIC_CRT)
      # Debug  -> MDd
      # Release-> MT
      # Others (RelWithDebInfo/MinSizeRel)-> MD
      set(_rt "$<IF:$<CONFIG:Debug>,MultiThreadedDebugDLL,$<IF:$<CONFIG:Release>,MultiThreaded,MultiThreadedDLL>>")
    else()
      # Dynamic CRT everywhere (MD/MDd).
      set(_rt "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    set_property(TARGET "${tgt}" PROPERTY MSVC_RUNTIME_LIBRARY "${_rt}")
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Project modules
# ------------------------------------------------------------------------------
include(CGOptions)     # your cmake/CGOptions.cmake
include(CGThirdParty)  # your cmake/CGThirdParty.cmake
include(CGShaders)     # your cmake/CGShaders.cmake (defines cg_compile_hlsl, cg_link_shaders_to_target)

# ------------------------------------------------------------------------------
# (PATCH) Wire shader compilation (Windows-only) — explicit list, no GLOB
# ------------------------------------------------------------------------------
if(WIN32)

  # Explicit list of HLSL files that actually contain entry points.
  # Add to this list as you create new shaders. Do NOT add helper-only
  # .hlsl/.hlsli files that are only intended to be #include'd.
  set(COLONY_HLSL
    # Fullscreen / raster shaders
    "${CMAKE_SOURCE_DIR}/shaders/raster/FullScreen.vs.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/raster/ToneMap.ps.hlsl"

    # World / terrain shaders
    "${CMAKE_SOURCE_DIR}/shaders/Terrain.hlsl"
    # NOTE: TerrainSplatExample is now an include-only .hlsli helper and is
    # intentionally not listed here.

    # Thermal erosion compute shaders (legacy)
    "${CMAKE_SOURCE_DIR}/shaders/ThermalOutflowCS.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/ThermalApplyCS.hlsl"

    # Renderer shaders (engine-side)
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/VolumetricClouds.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Batch2D_ps.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Common/FullScreenTriangleVS.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/WaterGerstner.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/TerrainGen.compute.hlsl"

    # Voxel/isosurface compute shaders
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT.compute.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl"

    # New GPU thermal erosion compute shaders
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"
  )

  if(COLONY_HLSL)

    # Per-file shader metadata for shaders whose entry point is NOT 'main'.
    # These were already in your project; we keep them unchanged, but bump
    # profiles to Shader Model 6.0 to avoid DXC upgrade warnings.
    if(COMMAND cg_set_hlsl_properties)
      # Atmosphere & sky rendering (fullscreen PS entry)
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"
        ENTRY   "PSMain"
        PROFILE "ps_6_0"
      )

      # Volumetric clouds (fullscreen PS entry)
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/VolumetricClouds.hlsl"
        ENTRY   "PSMain"
        PROFILE "ps_6_0"
      )

      # Voxel isosurface compute shaders
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT.compute.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_6_0"
      )

      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_6_0"
      )

      # GPU thermal erosion compute shaders under renderer/Shaders
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_6_0"
      )

      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_6_0"
      )

      # Legacy thermal erosion compute shaders under /shaders
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/shaders/ThermalOutflowCS.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_6_0"
      )

      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/shaders/ThermalApplyCS.hlsl"
        ENTRY   "CSMain"
        PROFILE "cs_6_0"
      )

      # Terrain pixel shader – uses PSMain in Terrain.hlsl
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/shaders/Terrain.hlsl"
        ENTRY   "PSMain"
        PROFILE "ps_6_0"
      )

      # Tone mapping pixel shader – uses PSMain in ToneMap.ps.hlsl
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/shaders/raster/ToneMap.ps.hlsl"
        ENTRY   "PSMain"
        PROFILE "ps_6_0"
      )

      # 2D batch pixel shader – uses PSMain in Batch2D_ps.hlsl
      cg_set_hlsl_properties(
        "${CMAKE_SOURCE_DIR}/renderer/Shaders/Batch2D_ps.hlsl"
        ENTRY   "PSMain"
        PROFILE "ps_6_0"
      )
    endif()

    # Compile shaders into a dedicated target; we'll link/copy them to the exe later
    cg_compile_hlsl(ColonyShaders
      SHADERS      ${COLONY_HLSL}
      INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/renderer/Shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders/include"
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
find_package(spdlog CONFIG REQUIRED) # logging

# Tracy wiring (optional)
# Prefer the lowercase 'tracy' package name (vcpkg port name), but accept 'Tracy' too.
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

# Choose spdlog target (static/compiled vs header-only)
set(COLONY_SPDLOG_TARGET "")
if(TARGET spdlog::spdlog)
  set(COLONY_SPDLOG_TARGET spdlog::spdlog)
elseif(TARGET spdlog::spdlog_header_only)
  set(COLONY_SPDLOG_TARGET spdlog::spdlog_header_only)
endif()
if(NOT COLONY_SPDLOG_TARGET)
  message(FATAL_ERROR "spdlog was found, but exported targets are missing. Expecting spdlog::spdlog or spdlog::spdlog_header_only.")
endif()

# ------------------------------------------------------------------------------
# (PATCH) Pathfinding library for the game (JPS/HPA) — adds colony_path target
# ------------------------------------------------------------------------------
file(GLOB_RECURSE COLONY_PATH_SOURCES
     "${CMAKE_SOURCE_DIR}/pathfinding/*.cpp"
     "${CMAKE_SOURCE_DIR}/hpa/*.cpp")

if(COLONY_PATH_SOURCES)
  add_library(colony_path STATIC ${COLONY_PATH_SOURCES})
  add_library(colony::path ALIAS colony_path)

  target_include_directories(colony_path PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/pathfinding"
    "${CMAKE_SOURCE_DIR}/hpa")

  target_compile_features(colony_path PUBLIC cxx_std_20)
  colony_apply_msvc_runtime(colony_path)
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
  # Hygiene: link SDL2 itself to the core, but never SDL2main (entry-point shim).
  target_link_libraries(colony_core PRIVATE
    SDL2::SDL2
    imgui::imgui
    ${COLONY_SPDLOG_TARGET}
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

  # Optional: spdlog runtime log level (trace in Debug, info otherwise)
  target_compile_definitions(colony_core PRIVATE
    SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_TRACE,SPDLOG_LEVEL_INFO>
  )

  # Treat linker warnings as errors on the core lib too (catches duplicate symbols early)
  if(MSVC)
    target_link_options(colony_core PRIVATE /WX)
  endif()

  # (PATCH) Link the pathfinding library into the core if available
  if(TARGET colony_path)
    target_link_libraries(colony_core PRIVATE colony::path)
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

  # Apply CRT policy
  colony_apply_msvc_runtime(colony_core)
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
colony_apply_msvc_runtime(procgen)

# Link the add-ons into the core/game target if it exists
if(TARGET colony_core)
  target_link_libraries(colony_core PRIVATE procgen)
endif()

# ------------------------------------------------------------------------------
# Generate the final game target, install rules, tests, summary
# ------------------------------------------------------------------------------
include(CGGameTarget)   # your cmake/CGGameTarget.cmake

# If the main game executable exists, link SDL2main there (never in the core lib)
if(TARGET ColonyGame AND TARGET SDL2::SDL2main)
  target_link_libraries(ColonyGame PRIVATE SDL2::SDL2main)
endif()

# (PATCH) Link colony_path into the main game exe (if present)
if(TARGET ColonyGame AND TARGET colony_path)
  target_link_libraries(ColonyGame PRIVATE colony::path)
endif()
if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}" AND TARGET colony_path)
  target_link_libraries("${COLONY_GAME_TARGET}" PRIVATE colony::path)
endif()

# Apply CRT policy to executables if they exist
if(TARGET ColonyGame)
  colony_apply_msvc_runtime(ColonyGame)
endif()
if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
  colony_apply_msvc_runtime("${COLONY_GAME_TARGET}")
endif()

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

# ------------------------------------------------------------------------------
# Map viewer tool (Windows-only)
# ------------------------------------------------------------------------------
if(WIN32)
  add_executable(ColonyMapViewer WIN32
    tools/map_viewer/MapViewerWin.cpp
  )

  if(MSVC)
    target_compile_definitions(ColonyMapViewer PRIVATE UNICODE _UNICODE)
    target_link_options(ColonyMapViewer PRIVATE /WX) # treat link warnings as errors
  endif()

  # Link against world-gen (procgen) and Win32 GUI libs
  target_link_libraries(ColonyMapViewer PRIVATE
    procgen
    user32
    gdi32
    comdlg32
  )

  colony_apply_msvc_runtime(ColonyMapViewer)
endif()

# ------------------------------------------------------------------------------
# Tests wiring (single-runner subdirectory) — added by patch
# ------------------------------------------------------------------------------
if(COLONY_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  # Avoid duplicate definition if CGTests also declares the same targets
  if(NOT TARGET colony_smoke)
    add_subdirectory(tests)
  endif()
endif()

include(CGStageInstall) # your cmake/CGStageInstall.cmake
include(CGTests)        # your cmake/CGTests.cmake
include(CGSummary)      # your cmake/CGSummary.cmake

# ------------------------------------------------------------------------------
# Enforce link warnings-as-errors on all executable targets (MSVC)
# (Catches duplicate entry points, COMDAT collisions, etc.)
# ------------------------------------------------------------------------------
if(MSVC)
  function(_cg_apply_link_wx start_dir)
    get_property(_tgts DIRECTORY "${start_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _tgts)
      get_target_property(_type "${_t}" TYPE)
      if(_type STREQUAL "EXECUTABLE")
        target_link_options(${_t} PRIVATE /WX)
      endif()
    endforeach()
    get_property(_subs DIRECTORY "${start_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sd IN LISTS _subs)
      _cg_apply_link_wx("${_sd}")
    endforeach()
  endfunction()
  _cg_apply_link_wx("${CMAKE_SOURCE_DIR}")
endif()
