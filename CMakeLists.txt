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

# ------------------------------------------------------------------------------
# vcpkg manifest feature wiring (MUST be set before first project() call)
# ------------------------------------------------------------------------------
# This option maps to the vcpkg.json manifest feature "imgui-freetype".
# Enabling it will cause vcpkg to install imgui[freetype] (and thus freetype),
# which is sometimes flaky to download in CI. Leave OFF unless you need it.
option(COLONY_ENABLE_IMGUI_FREETYPE
  "Enable ImGui FreeType font rasterization (enables vcpkg manifest feature 'imgui-freetype')"
  OFF
)

# Optional escape hatches: allow setting cache strings that get forwarded to env vars
# (useful for CI without having to edit workflow files).
set(COLONY_VCPKG_ASSET_SOURCES "" CACHE STRING
  "If set and X_VCPKG_ASSET_SOURCES is not already defined, sets it for this configure (vcpkg asset caching)."
)
set(COLONY_VCPKG_BINARY_SOURCES "" CACHE STRING
  "If set and VCPKG_BINARY_SOURCES is not already defined, sets it for this configure (vcpkg binary caching)."
)

if(NOT "${COLONY_VCPKG_ASSET_SOURCES}" STREQUAL "" AND NOT DEFINED ENV{X_VCPKG_ASSET_SOURCES})
  set(ENV{X_VCPKG_ASSET_SOURCES} "${COLONY_VCPKG_ASSET_SOURCES}")
endif()
if(NOT "${COLONY_VCPKG_BINARY_SOURCES}" STREQUAL "" AND NOT DEFINED ENV{VCPKG_BINARY_SOURCES})
  set(ENV{VCPKG_BINARY_SOURCES} "${COLONY_VCPKG_BINARY_SOURCES}")
endif()

if(COLONY_ENABLE_IMGUI_FREETYPE)
  list(APPEND VCPKG_MANIFEST_FEATURES "imgui-freetype")
  list(REMOVE_DUPLICATES VCPKG_MANIFEST_FEATURES)
endif()

# CI-friendly hints (non-fatal)
if(DEFINED ENV{CI})
  if(COLONY_ENABLE_IMGUI_FREETYPE AND NOT DEFINED ENV{X_VCPKG_ASSET_SOURCES} AND "${COLONY_VCPKG_ASSET_SOURCES}" STREQUAL "")
    message(STATUS "CI: COLONY_ENABLE_IMGUI_FREETYPE=ON. Consider setting X_VCPKG_ASSET_SOURCES (asset caching) to reduce external download flakiness.")
  endif()
  if(NOT DEFINED ENV{VCPKG_BINARY_SOURCES} AND "${COLONY_VCPKG_BINARY_SOURCES}" STREQUAL "")
    message(STATUS "CI: Consider setting VCPKG_BINARY_SOURCES (binary caching) to speed up and stabilize vcpkg installs.")
  endif()
endif()

project(ColonyGame LANGUAGES CXX)

# --------------------------------------------------------------------
# Back-compat / preset hygiene:
# Some presets/CI pass -DCOLONY_LTO=ON/OFF. Make it a real option and
# actually use it so it isn't reported as "unused".
# --------------------------------------------------------------------
option(COLONY_LTO "Enable IPO/LTO for Release when supported" ON)

# --- Interprocedural optimization (LTO) for Release when supported -------------
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_err LANGUAGES CXX)
if(COLONY_LTO)
  if(_ipo_supported)
    # CMake will map this to /GL at compile and /LTCG at link on MSVC.
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  else()
    message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
  endif()
else()
  message(STATUS "IPO/LTO disabled (COLONY_LTO=OFF).")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
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

# Back-compat: legacy unity variables
# - Some presets/scripts may set COLONY_UNITY_BUILD or CG_ENABLE_UNITY, but this root file uses COLONY_UNITY.
if(NOT DEFINED COLONY_UNITY AND DEFINED COLONY_UNITY_BUILD)
  set(COLONY_UNITY "${COLONY_UNITY_BUILD}" CACHE BOOL "Enable Unity/Jumbo builds for project targets (legacy: COLONY_UNITY_BUILD)" FORCE)
endif()
if(NOT DEFINED COLONY_UNITY AND DEFINED CG_ENABLE_UNITY)
  set(COLONY_UNITY "${CG_ENABLE_UNITY}" CACHE BOOL "Enable Unity/Jumbo builds for project targets (legacy: CG_ENABLE_UNITY)" FORCE)
endif()

# ------------------------------------------------------------------------------
# Build options
# ------------------------------------------------------------------------------

# Default warnings-as-errors to ON in CI (OFF locally). Presets/cache can override.
set(_CG_DEFAULT_WERROR OFF)
if(DEFINED ENV{CI})
  set(_CG_DEFAULT_WERROR ON)
endif()

option(COLONY_WERROR         "Treat warnings as errors on MSVC (defaults ON in CI)" ${_CG_DEFAULT_WERROR})
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

# If ON, configuration will error if it detects duplicated Win platform .cpp basenames
# in BOTH:
#   - ${CMAKE_SOURCE_DIR}/src/platform/win
#   - ${CMAKE_SOURCE_DIR}/platform/win
# This forces the repo toward a single canonical implementation (recommended long-term).
option(COLONY_STRICT_WIN_PLATFORM_DEDUP "Hard-fail if legacy platform/win duplicates exist" OFF)

set(COLONY_GAME_TARGET "" CACHE STRING
  "Name of the game's main executable target (if different from 'ColonyGame') to link with colony_nav")

# Keep legacy cache var in sync so any other modules still reading it behave correctly.
if(DEFINED COLONY_UNITY_BUILD AND NOT "${COLONY_UNITY_BUILD}" STREQUAL "${COLONY_UNITY}")
  message(STATUS "COLONY_UNITY_BUILD is deprecated; using COLONY_UNITY=${COLONY_UNITY}.")
endif()
set(COLONY_UNITY_BUILD "${COLONY_UNITY}" CACHE BOOL "Deprecated alias (use COLONY_UNITY)" FORCE)

if(COLONY_UNITY)
  set(CMAKE_UNITY_BUILD ON)
  set(CMAKE_UNITY_BUILD_BATCH_SIZE 8)
endif()

# ------------------------------------------------------------------------------
# vcpkg + runtime linkage sanity (manifest is configured for dynamic CRT)
# ------------------------------------------------------------------------------
# Your vcpkg.json supports clause excludes staticcrt. If you turn on /MT here while
# using vcpkg dynamic CRT triplets, you'll typically get runtime-library mismatch warnings
# and potentially hard-to-debug crashes. Fail early when vcpkg toolchain is detected.
if((COLONY_STATIC_RUNTIME OR COLONY_RELEASE_STATIC_CRT) AND DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
  message(FATAL_ERROR
    "Static MSVC runtime requested (COLONY_STATIC_RUNTIME or COLONY_RELEASE_STATIC_CRT), "
    "but this repo's vcpkg manifest is constrained to dynamic CRT triplets (!staticcrt). "
    "Fix: keep these OFF (recommended), or switch to a static CRT vcpkg triplet and update vcpkg.json supports accordingly.")
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

  # Reduce warning noise from external headers when using /W4 + /WX.
  # Only enable if the compiler actually supports these flags.
  include(CheckCXXCompilerFlag)
  set(_CG_SAVE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS}")
  set(CMAKE_REQUIRED_FLAGS "/WX")
  check_cxx_compiler_flag("/external:anglebrackets" _CG_HAS_EXTERNAL_ANGLEBRACKETS)
  check_cxx_compiler_flag("/external:W0" _CG_HAS_EXTERNAL_W0)
  set(CMAKE_REQUIRED_FLAGS "${_CG_SAVE_REQUIRED_FLAGS}")

  if(_CG_HAS_EXTERNAL_ANGLEBRACKETS AND _CG_HAS_EXTERNAL_W0)
    add_compile_options(
      /external:anglebrackets
      /external:W0
    )
  endif()

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

  # Release codegen & link opts (LTO flags are handled by IPO property above when supported)
  add_compile_options(
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/Oi>
    $<$<CONFIG:Release>:/Ot>
    $<$<CONFIG:Release>:/Gw>
    $<$<CONFIG:Release>:/Gy>
  )
  add_link_options(
    $<$<CONFIG:Release>:/INCREMENTAL:NO>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
  )

  # First-class runtime selection (CMP0091=NEW required; already set above)
  # This sets the default MSVC runtime for all targets created afterwards.
  if(COLONY_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  elseif(COLONY_RELEASE_STATIC_CRT)
    # Debug: DLL CRT (/MDd). Release: static CRT (/MT). Others: DLL CRT (/MD).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "$<IF:$<CONFIG:Debug>,MultiThreadedDebugDLL,$<IF:$<CONFIG:Release>,MultiThreaded,MultiThreadedDLL>>")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
endif()

# ---- Defensive cleanup: strip any stray /RTC* from non-Debug toolchain vars ----
# Additionally, when ASan is enabled on MSVC, /RTC* is incompatible and must be stripped from Debug too.
if(MSVC)
  set(_CG_RTC_STRIP_CONFIGS RELEASE RELWITHDEBINFO MINSIZEREL)
  if(COLONY_ENABLE_ASAN)
    list(APPEND _CG_RTC_STRIP_CONFIGS DEBUG)
  endif()

  foreach(_CFG IN LISTS _CG_RTC_STRIP_CONFIGS)
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

  unset(_CG_RTC_STRIP_CONFIGS)
endif()

# ---- Helpers ------------------------------------------------------------------
function(colony_enable_pch tgt)
  if(NOT COLONY_USE_PCH)
    return()
  endif()
  # Expected: colony_enable_pch(<target> <PUBLIC|PRIVATE|INTERFACE> <header...>)
  if(ARGC LESS 3)
    message(FATAL_ERROR "colony_enable_pch(<target> <PUBLIC|PRIVATE|INTERFACE> <header...>)")
  endif()
  target_precompile_headers(${tgt} ${ARGN})
endfunction()

function(colony_apply_msvc_runtime tgt)
  if(MSVC AND TARGET "${tgt}")
    if(COLONY_STATIC_RUNTIME)
      set(_rt "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    elseif(COLONY_RELEASE_STATIC_CRT)
      set(_rt "$<IF:$<CONFIG:Debug>,MultiThreadedDebugDLL,$<IF:$<CONFIG:Release>,MultiThreaded,MultiThreadedDLL>>")
    else()
      set(_rt "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    set_property(TARGET "${tgt}" PROPERTY MSVC_RUNTIME_LIBRARY "${_rt}")
  endif()
endfunction()

function(colony_apply_debug_runtime_checks tgt)
  if(MSVC AND TARGET "${tgt}")
    # MSVC AddressSanitizer is incompatible with /RTC* runtime checks.
    if(COLONY_ENABLE_ASAN)
      return()
    endif()
    target_compile_options(${tgt} PRIVATE $<$<CONFIG:Debug>:/RTC1>)
  endif()
endfunction()

# Add a source file only if it isn't already present in the target (REALPATH, case-insensitive on Windows).
function(colony_add_source_if_missing tgt src_path)
  if(NOT TARGET "${tgt}")
    return()
  endif()
  if(NOT EXISTS "${src_path}")
    return()
  endif()

  get_target_property(_srcs "${tgt}" SOURCES)
  if(NOT _srcs)
    set(_srcs "")
  endif()

  get_filename_component(_want "${src_path}" REALPATH)
  if(WIN32)
    string(TOLOWER "${_want}" _want_key)
  else()
    set(_want_key "${_want}")
  endif()

  set(_found FALSE)
  get_target_property(_tgt_dir "${tgt}" SOURCE_DIR)
  if(NOT _tgt_dir)
    set(_tgt_dir "${CMAKE_SOURCE_DIR}")
  endif()

  foreach(_s IN LISTS _srcs)
    if(IS_ABSOLUTE "${_s}")
      get_filename_component(_abs "${_s}" REALPATH)
    else()
      get_filename_component(_abs "${_s}" REALPATH BASE_DIR "${_tgt_dir}")
    endif()
    if(WIN32)
      string(TOLOWER "${_abs}" _abs_key)
    else()
      set(_abs_key "${_abs}")
    endif()
    if(_abs_key STREQUAL _want_key)
      set(_found TRUE)
      break()
    endif()
  endforeach()

  if(NOT _found)
    target_sources("${tgt}" PRIVATE "${src_path}")
  endif()
endfunction()

# Apply /EHa only to CrashDumpWin.cpp for a given target (if present).
# Also skip PCH + unity inclusion for that TU to avoid MSVC C4652 warnings when
# /EHa differs from the target's PCH flags (/EHsc), and to prevent unity/ODR edge cases.
function(colony_apply_seh_for_crashdump tgt)
  if(NOT (MSVC AND COLONY_SEH_CRASHDUMP))
    return()
  endif()
  if(NOT TARGET "${tgt}")
    return()
  endif()

  get_target_property(_srcs "${tgt}" SOURCES)
  if(NOT _srcs)
    return()
  endif()

  # Resolve relative source paths against the directory where the target was created.
  get_target_property(_tgt_dir "${tgt}" SOURCE_DIR)
  if(NOT _tgt_dir)
    set(_tgt_dir "${CMAKE_SOURCE_DIR}")
  endif()

  foreach(_src IN LISTS _srcs)
    if(_src MATCHES "CrashDumpWin\\.cpp$")
      # Normalize to an absolute path (safer across directory scopes).
      if(IS_ABSOLUTE "${_src}")
        set(_abs_candidate "${_src}")
      else()
        set(_abs_candidate "${_tgt_dir}/${_src}")
      endif()

      if(EXISTS "${_abs_candidate}")
        get_filename_component(_abs "${_abs_candidate}" REALPATH)
      else()
        set(_abs "${_abs_candidate}")
      endif()

      # IMPORTANT: set properties in the directory scope where the target was created.
      set_source_files_properties("${_abs}"
        TARGET_DIRECTORY "${tgt}"
        PROPERTIES
          COMPILE_OPTIONS "/EHa"
          SKIP_PRECOMPILE_HEADERS ON
          SKIP_UNITY_BUILD_INCLUSION ON
      )
    endif()
  endforeach()
endfunction()

# ------------------------------------------------------------------------------
# Project modules
# ------------------------------------------------------------------------------
include(CGOptions)
include(CGThirdParty)
include(CGShaders)

# ------------------------------------------------------------------------------
# A) Install layout (path install names) - MUST be included before add_subdirectory()
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/CGInstallLayout.cmake")

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
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/AtmosphereSky.hlsl"                 ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/VolumetricClouds.hlsl"               ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT.compute.hlsl"            ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/VoxelIso_MT_Materials.compute.hlsl"  ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_flow_cs.hlsl"        ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/erosion_thermal_apply_cs.hlsl"       ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/ThermalOutflowCS.hlsl"                        ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/ThermalApplyCS.hlsl"                          ENTRY "CSMain" PROFILE "cs_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/Terrain.hlsl"                                 ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/shaders/raster/ToneMap.ps.hlsl"                       ENTRY "PSMain" PROFILE "ps_6_0")
      cg_set_hlsl_properties("${CMAKE_SOURCE_DIR}/renderer/Shaders/Batch2D_ps.hlsl"                     ENTRY "PSMain" PROFILE "ps_6_0")
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
# B) Target-split modules (replace in-root target creation with add_subdirectory)
# ------------------------------------------------------------------------------
add_subdirectory(core)           # colony_core
add_subdirectory(platform/win)   # colony_platform_win

# ------------------------------------------------------------------------------
# FIX: If colony_platform_win was created with no .c/.cpp sources, CMake cannot
# infer a linker language and generation fails. Force CXX and (if needed) inject
# a tiny dummy TU so the target is well-formed.
# ------------------------------------------------------------------------------
if(TARGET colony_platform_win)
  get_target_property(_CG_PLAT_SOURCES colony_platform_win SOURCES)
  if(NOT _CG_PLAT_SOURCES)
    message(WARNING
      "colony_platform_win target exists but has no SOURCES. "
      "This usually means the source glob/list in platform/win/CMakeLists.txt matched nothing. "
      "Injecting a tiny dummy TU so CMake can determine the linker language. "
      "You should fix platform/win/CMakeLists.txt to include the real Win sources."
    )
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/cmake")
    set(_CG_PLAT_DUMMY "${CMAKE_BINARY_DIR}/cmake/colony_platform_win_dummy.cpp")
    if(NOT EXISTS "${_CG_PLAT_DUMMY}")
      file(WRITE "${_CG_PLAT_DUMMY}" "void colony_platform_win_dummy() {}\n")
    endif()
    target_sources(colony_platform_win PRIVATE "${_CG_PLAT_DUMMY}")
    set_source_files_properties("${_CG_PLAT_DUMMY}"
      PROPERTIES
        GENERATED TRUE
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
    )
  endif()
  set_target_properties(colony_platform_win PROPERTIES LINKER_LANGUAGE CXX)
endif()
unset(_CG_PLAT_SOURCES)
unset(_CG_PLAT_DUMMY)

add_subdirectory(pathfinding)    # colony_nav
add_subdirectory(renderer)       # colony_renderer
add_subdirectory(engine)         # colony_engine

if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)          # ColonyGame (preferred) + any game-only targets
endif()

# Optional legacy fallback: only run the old CGGameTarget if the game target wasn't created by src/
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/CGGameTarget.cmake" AND NOT TARGET ColonyGame)
  include(CGGameTarget)
endif()

# ------------------------------------------------------------------------------
# C) Third-party wiring update (recommended)
#   - spdlog + tracy -> core
#   - SDL2 + imgui   -> renderer
# ------------------------------------------------------------------------------
if(TARGET colony_core)
  target_link_libraries(colony_core PUBLIC ${COLONY_SPDLOG_TARGET})

  if(COLONY_TRACY_TARGET AND COLONY_ENABLE_TRACY)
    target_link_libraries(colony_core PUBLIC ${COLONY_TRACY_TARGET})
    target_compile_definitions(colony_core PRIVATE TRACY_ENABLE=1)
    get_target_property(_TRACY_INC ${COLONY_TRACY_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
    if(_TRACY_INC)
      target_include_directories(colony_core PRIVATE ${_TRACY_INC})
    endif()
  endif()

  target_compile_definitions(colony_core PRIVATE
    SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_TRACE,SPDLOG_LEVEL_INFO>
  )

  if(MSVC AND COLONY_WERROR)
    target_link_options(colony_core PRIVATE /WX)
  endif()

  # Convenience: make platform lib transitive from core so EXEs/tests get it automatically.
  if(TARGET colony_platform_win)
    target_link_libraries(colony_core PUBLIC colony::platform_win)
  endif()

  # Ensure CrashDumpWin.cpp (if present) is compiled with /EHa, regardless of which target owns it.
  colony_apply_seh_for_crashdump(colony_core)
endif()

if(TARGET colony_renderer)
  target_link_libraries(colony_renderer PUBLIC
    SDL2::SDL2
    imgui::imgui
  )
endif()

# Also apply /EHa to CrashDumpWin.cpp if it lives in the platform lib.
if(TARGET colony_platform_win)
  colony_apply_seh_for_crashdump(colony_platform_win)
endif()

# ------------------------------------------------------------------------------
# Final game target post-link fixups (works whether ColonyGame comes from src/ or CGGameTarget)
# ------------------------------------------------------------------------------
if(TARGET ColonyGame AND TARGET SDL2::SDL2main)
  target_link_libraries(ColonyGame PRIVATE SDL2::SDL2main)
endif()

# If you still use COLONY_GAME_TARGET for alternate main exe names:
if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}" AND TARGET SDL2::SDL2main)
  target_link_libraries("${COLONY_GAME_TARGET}" PRIVATE SDL2::SDL2main)
endif()

# If the platform lib exists, link it into executables as a safety net even if colony_core isn't linked.
if(TARGET colony_platform_win)
  foreach(_exe ColonyGame ColonyLauncher ColonyMapViewer)
    if(TARGET ${_exe})
      target_link_libraries(${_exe} PRIVATE colony::platform_win)
    endif()
  endforeach()
  if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
    target_link_libraries("${COLONY_GAME_TARGET}" PRIVATE colony::platform_win)
  endif()
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
if(WIN32)
  get_property(_CG_SHADER_TGT GLOBAL PROPERTY COLONY_SHADERS_TARGET)
  if(_CG_SHADER_TGT)
    set(_CG_SHADER_EXES "")
    foreach(_t ColonyGame ColonyLauncher ColonyMapViewer)
      if(TARGET ${_t})
        list(APPEND _CG_SHADER_EXES "${_t}")
      endif()
    endforeach()
    if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
      list(APPEND _CG_SHADER_EXES "${COLONY_GAME_TARGET}")
    endif()
    list(REMOVE_DUPLICATES _CG_SHADER_EXES)

    if(_CG_SHADER_EXES)
      foreach(_exe IN LISTS _CG_SHADER_EXES)
        cg_link_shaders_to_target(${_CG_SHADER_TGT} ${_exe})
      endforeach()
    else()
      message(WARNING "CGShaders: No executable targets found to link shaders to.")
    endif()
  endif()
endif()

# ------------------------------------------------------------------------------
# Job System ownership fix:
# Do NOT inject JobSystem.cpp into a random executable.
# If a job system TU exists, it should be owned by the core library target.
# ------------------------------------------------------------------------------
if(TARGET colony_core)
  # Adjust the path if your repo stores it elsewhere.
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/core/JobSystem.cpp")
    colony_add_source_if_missing(colony_core "${CMAKE_SOURCE_DIR}/src/core/JobSystem.cpp")
    target_include_directories(colony_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
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
    if(COLONY_WERROR)
      target_link_options(ColonyMapViewer PRIVATE /WX)
    endif()
  endif()
  target_link_libraries(ColonyMapViewer PRIVATE
    colony::engine
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
# D) Install rules
# ------------------------------------------------------------------------------
include("${CMAKE_SOURCE_DIR}/cmake/CGInstallRules.cmake")
colony_install_all()

# ------------------------------------------------------------------------------
# Guard & auto-fix: ensure no flags/genex slipped into SOURCES (after targets)
# ------------------------------------------------------------------------------
include(cmake/NoFlagsInSources.cmake OPTIONAL)
if(COMMAND colony_fixup_sources)
  foreach(_t colony_nav colony_renderer colony_engine colony_core colony_platform_win ColonyGame ColonyMapViewer)
    if(TARGET ${_t})
      colony_fixup_sources(${_t})
    endif()
  endforeach()
endif()

# ------------------------------------------------------------------------------
# NEW: Prune duplicate/forbidden sources after targets exist (Windows)
# - Keep entry points out of libraries (e.g., colony_core)
# - Prefer src/platform/win over legacy platform/win for same-basename files
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

  # Load global list of legacy win basenames that must be pruned everywhere.
  get_property(_legacy_dup_names GLOBAL PROPERTY COLONY_WIN_LEGACY_DUP_BASENAMES)
  if(NOT _legacy_dup_names)
    set(_legacy_dup_names "")
  endif()

  set(_keep "")
  foreach(_s IN LISTS _srcs)
    # Normalize to absolute realpath when possible (needed for directory checks).
    if(IS_ABSOLUTE "${_s}")
      set(_abs_candidate "${_s}")
    else()
      get_target_property(_src_dir ${tgt} SOURCE_DIR)
      if(NOT _src_dir)
        set(_src_dir "${CMAKE_SOURCE_DIR}")
      endif()
      set(_abs_candidate "${_src_dir}/${_s}")
    endif()

    # If the file doesn't exist yet (generated), keep it.
    if(EXISTS "${_abs_candidate}")
      get_filename_component(_abs "${_abs_candidate}" REALPATH)
    else()
      set(_abs "${_abs_candidate}")
    endif()

    # 1) Never allow process entry-points inside the core library.
    if("${tgt}" STREQUAL "colony_core")
      if(_abs MATCHES "/(WinLauncher|AppMain|EntryWinMain)\\.(c|cpp)$")
        continue()
      endif()
    endif()

    # 2) Globally prune legacy platform/win duplicates if a src/platform/win twin exists.
    if(_abs MATCHES "/platform/win/")
      get_filename_component(_bn "${_abs}" NAME)
      list(FIND _legacy_dup_names "${_bn}" _dup_idx)
      if(NOT _dup_idx EQUAL -1)
        # Drop legacy duplicate everywhere.
        continue()
      endif()
    endif()

    list(APPEND _keep "${_s}")
  endforeach()

  list(REMOVE_DUPLICATES _keep)
  set_property(TARGET ${tgt} PROPERTY SOURCES "${_keep}")
endfunction()

# Apply pruning to common targets (only those that exist)
foreach(_t colony_nav colony_renderer colony_engine colony_platform_win colony_core ColonyGame ColonyLauncher ColonyMapViewer)
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
if(TARGET colony_engine)
  set_target_properties(colony_engine PROPERTIES UNITY_BUILD OFF)
endif()
if(TARGET colony_nav)
  set_target_properties(colony_nav PROPERTIES UNITY_BUILD OFF)
endif()
if(TARGET colony_platform_win)
  set_target_properties(colony_platform_win PROPERTIES UNITY_BUILD OFF)
endif()

# ------------------------------------------------------------------------------
# Enforce link warnings-as-errors on all executable targets (MSVC) (optional)
# ------------------------------------------------------------------------------
if(MSVC AND COLONY_WERROR)
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
# Wire discrete-GPU hint exports into executables (Windows only)
# Ensure HighPerfGPU.cpp is compiled into EXEs and not into libraries to avoid
# duplicate exported symbols and to follow vendor guidance (exports in the EXE).
# ------------------------------------------------------------------------------
function(cg_wire_gpu_exports_to_exe exe)
  if(NOT TARGET ${exe})
    return()
  endif()

  set(_gpu_src "${CMAKE_SOURCE_DIR}/src/platform/win/HighPerfGPU.cpp")
  if(NOT EXISTS "${_gpu_src}")
    # Adjust this path if your tree stores the file elsewhere.
    return()
  endif()

  # Remove the GPU exports from colony_core if present (avoid duplicates)
  if(TARGET colony_core)
    get_target_property(_core_srcs colony_core SOURCES)
    if(_core_srcs)
      get_filename_component(_gpu_real "${_gpu_src}" REALPATH)
      if(WIN32)
        string(TOLOWER "${_gpu_real}" _gpu_key)
      else()
        set(_gpu_key "${_gpu_real}")
      endif()

      get_target_property(_core_dir colony_core SOURCE_DIR)
      if(NOT _core_dir)
        set(_core_dir "${CMAKE_SOURCE_DIR}")
      endif()

      set(_filtered_core "")
      foreach(_s IN LISTS _core_srcs)
        if(IS_ABSOLUTE "${_s}")
          get_filename_component(_s_real "${_s}" REALPATH)
        else()
          get_filename_component(_s_real "${_s}" REALPATH BASE_DIR "${_core_dir}")
        endif()
        if(WIN32)
          string(TOLOWER "${_s_real}" _s_key)
        else()
          set(_s_key "${_s_real}")
        endif()
        if(NOT _s_key STREQUAL _gpu_key)
          list(APPEND _filtered_core "${_s}")
        endif()
      endforeach()
      set_property(TARGET colony_core PROPERTY SOURCES "${_filtered_core}")
    endif()
  endif()

  # Remove the GPU exports from colony_platform_win if present (avoid duplicates)
  if(TARGET colony_platform_win)
    get_target_property(_plat_srcs colony_platform_win SOURCES)
    if(_plat_srcs)
      get_filename_component(_gpu_real "${_gpu_src}" REALPATH)
      if(WIN32)
        string(TOLOWER "${_gpu_real}" _gpu_key)
      else()
        set(_gpu_key "${_gpu_real}")
      endif()

      get_target_property(_plat_dir colony_platform_win SOURCE_DIR)
      if(NOT _plat_dir)
        set(_plat_dir "${CMAKE_SOURCE_DIR}")
      endif()

      set(_filtered_plat "")
      foreach(_s IN LISTS _plat_srcs)
        if(IS_ABSOLUTE "${_s}")
          get_filename_component(_s_real "${_s}" REALPATH)
        else()
          get_filename_component(_s_real "${_s}" REALPATH BASE_DIR "${_plat_dir}")
        endif()
        if(WIN32)
          string(TOLOWER "${_s_real}" _s_key)
        else()
          set(_s_key "${_s_real}")
        endif()
        if(NOT _s_key STREQUAL _gpu_key)
          list(APPEND _filtered_plat "${_s}")
        endif()
      endforeach()
      set_property(TARGET colony_platform_win PROPERTY SOURCES "${_filtered_plat}")
    endif()
  endif()

  # Add GPU exports to the executable target
  target_sources(${exe} PRIVATE "${_gpu_src}")
endfunction()

# Attach the GPU exports to known EXE targets (add more if you have others)
foreach(_exe ColonyGame ColonyLauncher ColonyMapViewer)
  if(TARGET ${_exe})
    cg_wire_gpu_exports_to_exe(${_exe})
  endif()
endforeach()
if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
  cg_wire_gpu_exports_to_exe("${COLONY_GAME_TARGET}")
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
      if(EXISTS "${_s}")
        get_filename_component(_abs "${_s}" REALPATH)
      else()
        set(_abs "${_s}")
      endif()
    else()
      get_target_property(_src_dir ${tgt} SOURCE_DIR)
      if(NOT _src_dir)
        set(_src_dir "${CMAKE_SOURCE_DIR}")
      endif()
      if(EXISTS "${_src_dir}/${_s}")
        get_filename_component(_abs "${_s}" REALPATH BASE_DIR "${_src_dir}")
      else()
        set(_abs "${_src_dir}/${_s}")
      endif()
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

# If a source file is already compiled into a library, do not also compile it into another target.
# (Despite name, this works for any pair of targets.)
function(colony_remove_sources_already_in_lib consumer lib)
  if(TARGET ${consumer} AND TARGET ${lib})
    get_target_property(_c_srcs ${consumer} SOURCES)
    get_target_property(_l_srcs ${lib} SOURCES)
    if(_c_srcs AND _l_srcs)
      # Build normalized set of lib sources
      set(_l_norm "")
      get_target_property(_l_dir ${lib} SOURCE_DIR)
      if(NOT _l_dir)
        set(_l_dir "${CMAKE_SOURCE_DIR}")
      endif()
      foreach(_s IN LISTS _l_srcs)
        if(IS_ABSOLUTE "${_s}")
          if(EXISTS "${_s}")
            get_filename_component(_abs "${_s}" REALPATH)
          else()
            set(_abs "${_s}")
          endif()
        else()
          if(EXISTS "${_l_dir}/${_s}")
            get_filename_component(_abs "${_s}" REALPATH BASE_DIR "${_l_dir}")
          else()
            set(_abs "${_l_dir}/${_s}")
          endif()
        endif()
        if(WIN32)
          string(TOLOWER "${_abs}" _abs)
        endif()
        list(APPEND _l_norm "${_abs}")
      endforeach()

      # Keep only consumer sources that are NOT already in the lib
      set(_keep "")
      get_target_property(_c_dir ${consumer} SOURCE_DIR)
      if(NOT _c_dir)
        set(_c_dir "${CMAKE_SOURCE_DIR}")
      endif()
      foreach(_s IN LISTS _c_srcs)
        if(IS_ABSOLUTE "${_s}")
          if(EXISTS "${_s}")
            get_filename_component(_abs "${_s}" REALPATH)
          else()
            set(_abs "${_s}")
          endif()
        else()
          if(EXISTS "${_c_dir}/${_s}")
            get_filename_component(_abs "${_s}" REALPATH BASE_DIR "${_c_dir}")
          else()
            set(_abs "${_c_dir}/${_s}")
          endif()
        endif()
        if(WIN32)
          string(TOLOWER "${_abs}" _abs)
        endif()
        list(FIND _l_norm "${_abs}" _idx)
        if(_idx EQUAL -1)
          list(APPEND _keep "${_s}")
        endif()
      endforeach()

      set_property(TARGET ${consumer} PROPERTY SOURCES "${_keep}")
    endif()
  endif()
endfunction()

# Apply normalization/dedup to common targets
foreach(_t colony_nav colony_renderer colony_engine colony_platform_win colony_core ColonyGame ColonyLauncher ColonyMapViewer)
  if(TARGET ${_t})
    colony_normalize_and_dedupe_sources(${_t})
  endif()
endforeach()

# Prevent cross-target double-compiles:
# 1) Keep platform sources in colony_platform_win only (except HighPerfGPU.cpp which is in EXEs).
if(TARGET colony_platform_win)
  foreach(_consumer colony_core ColonyGame ColonyLauncher ColonyMapViewer)
    if(TARGET ${_consumer})
      colony_remove_sources_already_in_lib(${_consumer} colony_platform_win)
    endif()
  endforeach()
  if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
    colony_remove_sources_already_in_lib("${COLONY_GAME_TARGET}" colony_platform_win)
  endif()
endif()

# 2) Prevent exe/lib double-compiles between ColonyGame/Launcher and colony_core
colony_remove_sources_already_in_lib(ColonyGame      colony_core)
colony_remove_sources_already_in_lib(ColonyLauncher  colony_core)
if(COLONY_GAME_TARGET AND TARGET "${COLONY_GAME_TARGET}")
  colony_remove_sources_already_in_lib("${COLONY_GAME_TARGET}" colony_core)
endif()

# Re-apply CrashDump /EHa after source moves
if(TARGET colony_core)
  colony_apply_seh_for_crashdump(colony_core)
endif()
if(TARGET colony_platform_win)
  colony_apply_seh_for_crashdump(colony_platform_win)
endif()

# ------------------------------------------------------------------------------
# Visual Studio/MSBuild: Fix MSB8027 object filename collisions in generated .vcxproj
# ------------------------------------------------------------------------------
# MSB8027 happens when two or more sources share the same basename and MSBuild places
# all .obj outputs into the same $(IntDir), overwriting earlier outputs. Microsoft’s
# suggested fix is to change Object File Name to $(IntDir)%(RelativeDir).
#
# With CMake-generated VS projects, <ClCompile Include="..."> is often an absolute path,
# and MSBuild documents that %(RelativeDir) begins with %(RootDir) in that case.
# To avoid "C:\" appearing inside $(IntDir), we use %(Directory), which is the directory
# of the item without the root directory.
#
# We import a generated *.user.props file into every generated vcxproj using CMake's
# VS_USER_PROPS target property.
option(COLONY_VS_UNIQUE_OBJ_OUTPUTS
  "Visual Studio only: route object files into unique per-source directories to avoid MSB8027 collisions"
  ON
)

if(MSVC AND COLONY_VS_UNIQUE_OBJ_OUTPUTS AND CMAKE_GENERATOR MATCHES "Visual Studio")
  set(_CG_OBJ_PROPS_DIR "${CMAKE_BINARY_DIR}/cmake")
  file(MAKE_DIRECTORY "${_CG_OBJ_PROPS_DIR}")
  set(_CG_OBJ_USER_PROPS "${_CG_OBJ_PROPS_DIR}/ColonyGame.ObjectFileName.user.props")

  # Generate (or overwrite) a deterministic props file into the build tree.
  file(WRITE "${_CG_OBJ_USER_PROPS}" [=[<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">

  <!-- Preserve standard per-user Visual Studio settings if they exist -->
  <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props"
          Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" />

  <ItemDefinitionGroup>
    <ClCompile>
      <!-- Fix MSB8027: avoid .obj collisions when sources share the same basename -->
      <!-- Use %(Directory) instead of %(RelativeDir) to avoid drive letters when Includes are absolute -->
      <ObjectFileName>$(IntDir)%(Directory)</ObjectFileName>
    </ClCompile>
  </ItemDefinitionGroup>

</Project>
]=])

  # Apply to every non-imported, non-alias C++ target generated by this build.
  function(_cg_apply_vs_user_props_recursive dir)
    get_property(_tgts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _tgts)
      if(NOT TARGET "${_t}")
        continue()
      endif()

      # Skip ALIAS targets (cannot set properties on them).
      get_target_property(_aliased "${_t}" ALIASED_TARGET)
      if(_aliased)
        continue()
      endif()

      # Skip imported targets (vcpkg, system libs, etc.).
      get_target_property(_imported "${_t}" IMPORTED)
      if(_imported)
        continue()
      endif()

      get_target_property(_type "${_t}" TYPE)

      # Only targets that generate a .vcxproj benefit from this.
      if(_type STREQUAL "EXECUTABLE" OR
         _type STREQUAL "STATIC_LIBRARY" OR
         _type STREQUAL "SHARED_LIBRARY" OR
         _type STREQUAL "MODULE_LIBRARY" OR
         _type STREQUAL "OBJECT_LIBRARY")
        set_target_properties("${_t}" PROPERTIES VS_USER_PROPS "${_CG_OBJ_USER_PROPS}")
      endif()
    endforeach()

    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sd IN LISTS _subs)
      _cg_apply_vs_user_props_recursive("${_sd}")
    endforeach()
  endfunction()

  _cg_apply_vs_user_props_recursive("${CMAKE_SOURCE_DIR}")
endif()
