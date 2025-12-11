cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)

# ------------------------------------------------------------------------------
# Windows-only: hard-fail on non-Windows platforms
# ------------------------------------------------------------------------------
if(NOT WIN32)
  message(FATAL_ERROR "Colony-Game currently only supports Windows.")
endif()

# Modern policy behavior
if(POLICY CMP0053)
  cmake_policy(SET CMP0053 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()
if(POLICY CMP0092)
  cmake_policy(SET CMP0092 NEW)
endif()
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
# Ensure IPO/LTO is enforced when enabled
if(POLICY CMP0069)
  cmake_policy(SET CMP0069 NEW)
endif()

# ------------------------------------------------------------------------------
# Make project-local modules discoverable
# ------------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------------------
# Toolchain & policies before project()
# ------------------------------------------------------------------------------
include(CGPolicies)
include(CGToolchain)

project(ColonyGame LANGUAGES CXX)

# --- Interprocedural optimization (LTO) for Release when supported -------------
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_err LANGUAGES CXX)
if(_ipo_supported)
  # CMake will map this to /GL at compile and /LTCG (non-incremental) at link on MSVC.
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
  message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
endif()

# --- central Windows flags (optional include) ---
include(cmake/WindowsFlags.cmake OPTIONAL)

# Shader entrypoints for VS generator
include("${CMAKE_SOURCE_DIR}/cmake/ShaderEntryPoints.cmake")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Back-compat & preset hygiene
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23 CACHE STRING "C++ standard" FORCE)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(DEFINED CG_ENABLE_PCH AND NOT DEFINED COLONY_USE_PCH)
  set(COLONY_USE_PCH "${CG_ENABLE_PCH}" CACHE BOOL "Enable precompiled headers" FORCE)
endif()
if(DEFINED CG_ENABLE_UNITY AND NOT DEFINED COLONY_UNITY_BUILD)
  set(COLONY_UNITY_BUILD "${CG_ENABLE_UNITY}" CACHE BOOL "Enable unity (jumbo) builds" FORCE)
endif()

# ------------------------------------------------------------------------------
# Build options
# ------------------------------------------------------------------------------
option(COLONY_WERROR         "Treat warnings as errors on MSVC" OFF)
option(COLONY_UNITY          "Enable Unity/Jumbo builds for project targets" ON)
option(COLONY_STATIC_RUNTIME "Link static MSVC runtime (/MT) instead of DLL" OFF)

if(NOT DEFINED COLONY_USE_PCH)
  option(COLONY_USE_PCH "Enable precompiled headers" ON)
endif()
set(COLONY_PCH_HEADER "src/pch.hpp" CACHE STRING "Path to the common precompiled header used by game targets")

option(COLONY_ENABLE_TIDY "Run clang-tidy static analysis during build" OFF)
option(COLONY_ENABLE_ASAN "Enable AddressSanitizer for supported compilers/configs" OFF)
option(COLONY_SEH_CRASHDUMP "Compile CrashDumpWin.cpp with /EHa while keeping /EHsc globally" ON)
option(COLONY_ENABLE_TRACY "Build with Tracy profiler instrumentation (vcpkg port 'tracy')" OFF)

option(COLONY_BUILD_TESTS "Build unit tests" ON)
option(COLONY_RELEASE_STATIC_CRT "Use static runtime (/MT) in Release; dynamic in Debug" OFF)

set(COLONY_GAME_TARGET "" CACHE STRING
  "Name of the game's main executable target (if different from 'ColonyGame') to link with colony_path")

if(COLONY_UNITY)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
endif()

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
# AddressSanitizer
# ------------------------------------------------------------------------------
if(COLONY_ENABLE_ASAN)
  if(MSVC)
    add_compile_options(
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>
      $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Zi>
    )
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
# Windows macros & compiler hygiene
# ------------------------------------------------------------------------------
if(MSVC)
  add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
  )

  add_compile_options(
    /permissive-
    /W4
    /MP
    /utf-8
    /Zc:__cplusplus
    /Zc:preprocessor
    /Zc:inline
    /Zc:throwingNew
    /Zc:referenceBinding
    /Zf
    /EHsc
    $<$<BOOL:${COLONY_WERROR}>:/WX>
  )

  # PDBs in Debug/RelWithDebInfo
  add_compile_options($<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/Zi>)
  add_link_options(   $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/DEBUG:FULL>)

  # Release codegen & link-time opts (ONE FLAG PER GENEX)
  add_compile_options(
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Oi>
    $<$<CONFIG:Release>:/Ot>
    $<$<CONFIG:Release>:/GL>
    $<$<CONFIG:Release>:/Gw>
    $<$<CONFIG:Release>:/Gy>
  )
  add_link_options(
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:Release>:/LTCG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
  )

  # First-class runtime selection
  if(COLONY_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
endif()

# ---- Defensive cleanup: strip any stray /RTC* from non-Debug toolchain vars ----
if(MSVC)
  foreach(_CFG RELEASE RELWITHDEBINFO MINSIZEREL)
    foreach(_LANG C CXX)
      string(TOUPPER "${_CFG}" _CFG_UP)
      set(_VAR "CMAKE_${_LANG}_FLAGS_${_CFG_UP}")
      if(DEFINED ${_VAR} AND NOT "${${_VAR}}" STREQUAL "")
        string(REGEX REPLACE "(/RTC1|/RTCs|/RTCu|/RTCc)" "" _FILTERED "${${_VAR}}")
        if(NOT _FILTERED STREQUAL "${${_VAR}}")
          set(${_VAR} "${_FILTERED}" CACHE STRING "" FORCE)
        endif()
      endif()
    endforeach()
  endforeach()
endif()

# ---- Helpers ------------------------------------------------------------------
function(colony_enable_pch tgt)
  if(NOT COLONY_USE_PCH)
    return()
  endif()
  if(ARGC LESS 2)
    message(FATAL_ERROR "colony_enable_pch(<target> <PUBLIC|PRIVATE|INTERFACE> <header...>)")
  endif()
  target_precompile_headers(${tgt} ${ARGN})
endfunction()

function(colony_apply_msvc_runtime tgt)
  if(MSVC AND TARGET "${tgt}")
    if(COLONY_STATIC_RUNTIME)
      return()
    endif()
    if(COLONY_RELEASE_STATIC_CRT)
      set(_rt "$<IF:$<CONFIG:Debug>,MultiThreadedDebugDLL,$<IF:$<CONFIG:Release>,MultiThreaded,MultiThreadedDLL>>")
    else()
      set(_rt "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    set_property(TARGET "${tgt}" PROPERTY MSVC_RUNTIME_LIBRARY "${_rt}")
  endif()
endfunction()

function(colony_apply_debug_runtime_checks tgt)
  if(MSVC AND TARGET "${tgt}")
    target_compile_options(${tgt} PRIVATE $<$<CONFIG:Debug>:/RTC1>)
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Project modules
# ------------------------------------------------------------------------------
include(CGOptions)
include(CGThirdParty)
include(CGShaders)

# ------------------------------------------------------------------------------
# Shaders: explicit list (Windows-only)
# ------------------------------------------------------------------------------
if(WIN32)
  set(COLONY_HLSL
    "${CMAKE_SOURCE_DIR}/shaders/raster/FullScreen.vs.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/raster/ToneMap.ps.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/Terrain.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/ThermalOutflowCS.hlsl"
    "${CMAKE_SOURCE_DIR}/shaders/ThermalApplyCS.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/VolumetricClouds.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Batch2D_ps.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Common/FullScreenTriangleVS.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/WaterGerstner.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/TerrainGen.compute.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT.compute.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"
  )

  if(COLONY_HLSL)
    if(COMMAND cg_set_hlsl_properties)
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"              ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/VolumetricClouds.hlsl"            ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT.compute.hlsl"         ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl" ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"     ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"    ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/ThermalOutflowCS.hlsl"                     ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/ThermalApplyCS.hlsl"                       ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/Terrain.hlsl"                              ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/raster/ToneMap.ps.hlsl"                    ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/Batch2D_ps.hlsl"                  ENTRY "PSMain" PROFILE "ps_6_0")
    endif()

    cg_compile_hlsl(ColonyShaders
      SHADERS      ${COLONY_HLSL}
      INCLUDE_DIRS
        "${CMAKE_SOURCE_DIR}/renderer/Shaders"
        "${CMAKE_SOURCE_DIR}/shaders"
        "${CMAKE_SOURCE_DIR}/shaders/include"
      DEFINES      "COLONY_DEBUG=$<IF:$<CONFIG:Debug>,1,0>"
    )
    set_property(GLOBAL PROPERTY COLONY_SHADERS_TARGET ColonyShaders)
  endif()
endif()

# ------------------------------------------------------------------------------
# vcpkg dependencies (imported targets)
# ------------------------------------------------------------------------------
find_package(SDL2 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Tracy optional
set(_CG_TRACY_REQUEST_MODE QUIET)
if(COLONY_ENABLE_TRACY)
  set(_CG_TRACY_REQUEST_MODE REQUIRED)
endif()
find_package(tracy CONFIG ${_CG_TRACY_REQUEST_MODE})
if(NOT tracy_FOUND)
  find_package(Tracy CONFIG ${_CG_TRACY_REQUEST_MODE})
endif()
set(COLONY_TRACY_TARGET "")
if(TARGET tracy::TracyClient)
  set(COLONY_TRACY_TARGET tracy::TracyClient)
elseif(TARGET Tracy::TracyClient)
  set(COLONY_TRACY_TARGET Tracy::TracyClient)
endif()
if(COLONY_ENABLE_TRACY AND NOT COLONY_TRACY_TARGET)
  message(FATAL_ERROR "COLONY_ENABLE_TRACY=ON but Tracy client target was not found.")
endif()

# Choose spdlog target
set(COLONY_SPDLOG_TARGET "")
if(TARGET spdlog::spdlog)
  set(COLONY_SPDLOG_TARGET spdlog::spdlog)
elseif(TARGET spdlog::spdlog_header_only)
  set(COLONY_SPDLOG_TARGET spdlog::spdlog_header_only)
endif()
if(NOT COLONY_SPDLOG_TARGET)
  message(FATAL_ERROR "spdlog was found, but exported targets are missing.")
endif()

# ------------------------------------------------------------------------------
# Pathfinding library
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

  target_compile_features(colony_path PUBLIC cxx_std_23)

  if(COLONY_USE_PCH)
    target_precompile_headers(colony_path PRIVATE "${COLONY_PCH_HEADER}")
  endif()

  colony_apply_msvc_runtime(colony_path)
  colony_apply_debug_runtime_checks(colony_path)
endif()

# ------------------------------------------------------------------------------
# Core game source tree
# ------------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

# ------------------------------------------------------------------------------
# Link third-party deps to core
# ------------------------------------------------------------------------------
if(TARGET colony_core)
  target_link_libraries(colony_core PRIVATE
    SDL2::SDL2
    imgui::imgui
    ${COLONY_SPDLOG_TARGET}
  )

  if(COLONY_TRACY_TARGET AND COLONY_ENABLE_TRACY)
    target_link_libraries(colony_core PRIVATE ${COLONY_TRACY_TARGET})
    target_compile_definitions(colony_core PRIVATE TRACY_ENABLE=1)
    get_target_property(_TRACY_INC ${COLONY_TRACY_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(_TRACY_INC)
      target_include_directories(colony_core PRIVATE ${_TRACY_INC})
    endif()
  endif()

  target_compile_definitions(colony_core PRIVATE
    SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_TRACE,SPDLOG_LEVEL_INFO>
  )

  if(MSVC)
    target_link_options(colony_core PRIVATE /WX)
  endif()

  if(TARGET colony_path)
    target_link_libraries(colony_core PRIVATE colony::path)
  endif()

  if(MSVC AND COLONY_SEH_CRASHDUMP)
    get_target_property(_core_srcs colony_core SOURCES)
    if(_core_srcs)
      foreach(_src IN LISTS _core_srcs)
        if(_src MATCHES "CrashDumpWin\\.cpp$")
          set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "/EHa")
        endif()
      endforeach()
    endif()
  endif()

  if(COLONY_USE_PCH)
    target_precompile_headers(colony_core PRIVATE "${COLONY_PCH_HEADER}")
  endif()

  colony_apply_msvc_runtime(colony_core)
  colony_apply_debug_runtime_checks(colony_core)
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
colony_apply_debug_runtime_checks(procgen)

if(TARGET colony_core)
  target_link_libraries(colony_core PRIVATE procgen)
endif()

# ------------------------------------------------------------------------------
# Final game target
# ------------------------------------------------------------------------------
include(CGGameTarget)

if(TARGET ColonyGame AND TARGET SDL2::SDL2main)
  target_link_libraries(ColonyGame PRIVATE SDL2::SDL2main)
endif()

if(TARGET ColonyGame AND TARGET colony_path)
  target_link_libraries(ColonyGame PRIVATE colony::path)
endif()
if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}" AND TARGET colony_path)
  target_link_libraries("${COLONY_GAME_TARGET}" PRIVATE colony::path)
endif()

if(TARGET ColonyGame)
  colony_apply_msvc_runtime(ColonyGame)
  colony_apply_debug_runtime_checks(ColonyGame)
endif()
if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
  colony_apply_msvc_runtime("${COLONY_GAME_TARGET}")
  colony_apply_debug_runtime_checks("${COLONY_GAME_TARGET}")
endif()

# ------------------------------------------------------------------------------
# Link/copy compiled shaders next to the exe
# ------------------------------------------------------------------------------
function(_cg_first_exe_in_dir out_var start_dir)
  get_property(_tgts DIRECTORY "${start_dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_t IN LISTS _tgts)
    get_target_property(_type "${_t}" TYPE)
    if(_type STREQUAL "EXECUTABLE")
      set(${out_var} "${_t}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
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

# Job System attachment (optional)
_cg_first_exe_in_dir(_CG_EXE_TGT_JOBS "${CMAKE_SOURCE_DIR}")
if(_CG_EXE_TGT_JOBS)
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/core/JobSystem.cpp")
    target_sources(${_CG_EXE_TGT_JOBS} PRIVATE src/core/JobSystem.cpp)
    target_include_directories(${_CG_EXE_TGT_JOBS} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  else()
    message(WARNING "JobSystem: src/core/JobSystem.cpp not found.")
  endif()
else()
  message(WARNING "JobSystem: No executable target found; cannot attach JobSystem.cpp.")
endif()

if(WIN32)
  get_property(_CG_SHADER_TGT GLOBAL PROPERTY COLONY_SHADERS_TARGET)
  if(_CG_SHADER_TGT)
    _cg_first_exe_in_dir(_CG_EXE_TGT "${CMAKE_SOURCE_DIR}")
    if(_CG_EXE_TGT)
      cg_link_shaders_to_target(${_CG_SHADER_TGT} ${_CG_EXE_TGT})
    else()
      message(WARNING "CGShaders: No executable target found to link shaders to.")
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
    target_link_options(ColonyMapViewer PRIVATE /WX)
  endif()
  target_link_libraries(ColonyMapViewer PRIVATE
    procgen
    user32
    gdi32
    comdlg32
  )
  colony_apply_msvc_runtime(ColonyMapViewer)
  colony_apply_debug_runtime_checks(ColonyMapViewer)
endif()

# ------------------------------------------------------------------------------
# Tests wiring (single-runner; add only once)
# ------------------------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/CGTests.cmake")
  include(CGTests)
endif()

if(COLONY_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  if(NOT DEFINED COLONY_TESTS_ADDED)
    add_subdirectory("${CMAKE_SOURCE_DIR}/tests" "${CMAKE_BINARY_DIR}/tests")
    set(COLONY_TESTS_ADDED ON CACHE INTERNAL "tests/ added by root")
  endif()
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/CGStageInstall.cmake")
  include(CGStageInstall)
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/CGSummary.cmake")
  include(CGSummary)
endif()

# ------------------------------------------------------------------------------
# Guard & auto-fix: ensure no flags/genex slipped into SOURCES (after targets)
# ------------------------------------------------------------------------------
include(cmake/NoFlagsInSources.cmake OPTIONAL)
if(COMMAND colony_fixup_sources)
  foreach(_t colony_path procgen colony_core ColonyGame ColonyMapViewer)
    if(TARGET ${_t})
      colony_fixup_sources(${_t})
    endif()
  endforeach()
endif()

# ------------------------------------------------------------------------------
# NEW: Prune duplicate/forbidden sources after targets exist (Windows)
# - Keep entry points out of libraries (e.g., colony_core)
# - Resolve duplicate platform files (e.g., CrashHandlerWin.cpp)
# - Deduplicate any repeated source entries
# ------------------------------------------------------------------------------
function(colony_prune_sources tgt)
  if(NOT TARGET ${tgt})
    return()
  endif()
  get_target_property(_srcs ${tgt} SOURCES)
  if(NOT _srcs)
    return()
  endif()

  set(_keep "")
  foreach(_s IN LISTS _srcs)
    # 1) Never allow process entry-points inside the core library.
    if(${tgt} STREQUAL "colony_core")
      if(_s MATCHES "/(WinLauncher|AppMain|EntryWinMain)\\.cpp$")
        # Skip: entry points must live only in EXE targets
        continue()
      endif()
    endif()

    # 2) Resolve MSB8027-style duplicate object names:
    # Prefer the 'src/platform/win' CrashHandler over the top-level duplicate.
    if(_s MATCHES "/platform/win/CrashHandlerWin\\.cpp$")
      if(EXISTS "${CMAKE_SOURCE_DIR}/src/platform/win/CrashHandlerWin.cpp")
        if(_s MATCHES "^${CMAKE_SOURCE_DIR}/platform/win/CrashHandlerWin\\.cpp$")
          continue() # drop the top-level duplicate
        endif()
      endif()
    endif()

    list(APPEND _keep "${_s}")
  endforeach()

  # Extra safety: remove accidental repeats.
  list(REMOVE_DUPLICATES _keep)
  set_property(TARGET ${tgt} PROPERTY SOURCES "${_keep}")
endfunction()

# Apply the pruning to common targets (only those that exist)
foreach(_t colony_core ColonyGame ColonyLauncher ColonyMapViewer)
  if(TARGET ${_t})
    colony_prune_sources(${_t})
  endif()
endforeach()

# Ensure public headers (e.g., include/CrashHandler.h) are visible to users
if(TARGET colony_core)
  target_include_directories(colony_core PUBLIC "${CMAKE_SOURCE_DIR}/include")
endif()
if(TARGET ColonyGame)
  target_include_directories(ColonyGame PUBLIC "${CMAKE_SOURCE_DIR}/include")
endif()
if(TARGET ColonyLauncher)
  target_include_directories(ColonyLauncher PUBLIC "${CMAKE_SOURCE_DIR}/include")
endif()

# ------------------------------------------------------------------------------
# Local Unity guard for CI offenders
# ------------------------------------------------------------------------------
if(TARGET procgen)
  set_target_properties(procgen PROPERTIES UNITY_BUILD OFF)
endif()
if(TARGET colony_path)
  set_target_properties(colony_path PROPERTIES UNITY_BUILD OFF)
endif()

# ------------------------------------------------------------------------------
# Enforce link warnings-as-errors on all executable targets (MSVC)
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

# ------------------------------------------------------------------------------
# Robust source de-dup (case-insensitive, realpath) and EXE/lib deconflict
# ------------------------------------------------------------------------------
# Normalize each target's sources by REALPATH, then lowercased on Windows,
# and remove duplicates so the same .cpp isn't compiled twice under different paths.
function(colony_normalize_and_dedupe_sources tgt)
  if(NOT TARGET ${tgt})
    return()
  endif()
  get_target_property(_srcs ${tgt} SOURCES)
  if(NOT _srcs)
    return()
  endif()

  set(_seen_norm "")
  set(_keep "")
  foreach(_s IN LISTS _srcs)
    if(IS_ABSOLUTE "${_s}")
      get_filename_component(_abs "${_s}" REALPATH)
    else()
      get_target_property(_src_dir ${tgt} SOURCE_DIR)
      if(NOT _src_dir)
        set(_src_dir "${CMAKE_SOURCE_DIR}")
      endif()
      get_filename_component(_abs "${_s}" REALPATH BASE_DIR "${_src_dir}")
    endif()
    set(_norm "${_abs}")
    if(WIN32)
      string(TOLOWER "${_norm}" _norm)
    endif()

    list(FIND _seen_norm "${_norm}" _idx)
    if(_idx EQUAL -1)
      list(APPEND _seen_norm "${_norm}")
      list(APPEND _keep "${_s}")
    endif()
  endforeach()

  set_property(TARGET ${tgt} PROPERTY SOURCES "${_keep}")
endfunction()

# If a source file is already compiled into a library (e.g., colony_core),
# do not also compile it into the executable (e.g., ColonyGame/ColonyLauncher).
function(colony_remove_sources_already_in_lib exe lib)
  if(TARGET ${exe} AND TARGET ${lib})
    get_target_property(_exe_srcs ${exe} SOURCES)
    get_target_property(_lib_srcs ${lib} SOURCES)
    if(_exe_srcs AND _lib_srcs)
      # Build normalized set of lib sources
      set(_lib_norm "")
      get_target_property(_lib_dir ${lib} SOURCE_DIR)
      if(NOT _lib_dir)
        set(_lib_dir "${CMAKE_SOURCE_DIR}")
      endif()
      foreach(_s IN LISTS _lib_srcs)
        if(IS_ABSOLUTE "${_s}")
          get_filename_component(_abs "${_s}" REALPATH)
        else()
          get_filename_component(_abs "${_s}" REALPATH BASE_DIR "${_lib_dir}")
        endif()
        if(WIN32)
          string(TOLOWER "${_abs}" _abs)
        endif()
        list(APPEND _lib_norm "${_abs}")
      endforeach()

      # Keep only exe sources that are NOT already in the lib
      set(_keep "")
      get_target_property(_exe_dir ${exe} SOURCE_DIR)
      if(NOT _exe_dir)
        set(_exe_dir "${CMAKE_SOURCE_DIR}")
      endif()
      foreach(_s IN LISTS _exe_srcs)
        if(IS_ABSOLUTE "${_s}")
          get_filename_component(_abs "${_s}" REALPATH)
        else()
          get_filename_component(_abs "${_s}" REALPATH BASE_DIR "${_exe_dir}")
        endif()
        if(WIN32)
          string(TOLOWER "${_abs}" _abs)
        endif()
        list(FIND _lib_norm "${_abs}" _idx)
        if(_idx EQUAL -1)
          list(APPEND _keep "${_s}")
        endif()
      endforeach()

      set_property(TARGET ${exe} PROPERTY SOURCES "${_keep}")
    endif()
  endif()
endfunction()

# Apply normalization/dedup to common targets
foreach(_t colony_path procgen colony_core ColonyGame ColonyLauncher ColonyMapViewer)
  if(TARGET ${_t})
    colony_normalize_and_dedupe_sources(${_t})
  endif()
endforeach()

# Prevent exe/lib double-compiles
colony_remove_sources_already_in_lib(ColonyGame    colony_core)
colony_remove_sources_already_in_lib(ColonyLauncher colony_core)
