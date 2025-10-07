# src/CMakeLists.txt
cmake_minimum_required(VERSION 3.20) # genex in OUTPUT requires 3.20+; VS HLSL props are 3.1+/3.10+

# ------------------------------------------------------------------------------

# ---------------- HLSL helper (inline) ----------------
# Adds .hlsl to a target and tags per-file Visual Studio shader properties.
# Example:
#   colony_add_hlsl(
#     TARGET  ColonyGame
#     SOURCES "${CMAKE_SOURCE_DIR}/shaders/quad_vs.hlsl"
#     PROFILE "vs_5_0"
#     ENTRY   "VSMain"
#     OUTDIR  "${CMAKE_BINARY_DIR}/res/shaders/${CMAKE_CFG_INTDIR}"
#   )
#
# PROFILE:  [vs|ps|cs|gs|hs|ds]_[major]_[minor]  e.g., vs_5_0, ps_6_7
# ENTRY:    shader entry point (e.g., main, VSMain, PSMain)
# OUTDIR:   where to emit the .cso (defaults to build/res/shaders/$(Configuration))
#
# Notes:
# - Uses VS source-file properties: VS_SHADER_TYPE, VS_SHADER_MODEL, VS_SHADER_ENTRYPOINT,
#   VS_SHADER_FLAGS, VS_SHADER_OBJECT_FILE_NAME, VS_SHADER_OUTPUT_HEADER_FILE, VS_SHADER_VARIABLE_NAME.
#   These are officially documented CMake properties for Visual Studio generators.
#   (See CMake docs for VS_SHADER_*).  MSBuild compiles one shader per file automatically.
# ------------------------------------------------------------------------------

function(colony_add_hlsl)
  set(_opts)
  set(_one  TARGET PROFILE ENTRY OUTDIR)
  set(_many SOURCES FLAGS DEFINES INCLUDE_DIRS)
  cmake_parse_arguments(CAH "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT CAH_TARGET)
    message(FATAL_ERROR "colony_add_hlsl: TARGET is required")
  endif()
  if(NOT CAH_SOURCES)
    message(FATAL_ERROR "colony_add_hlsl: SOURCES is required")
  endif()

  # defaults
  if(NOT CAH_ENTRY)
    set(CAH_ENTRY "main")
  endif()
  if(NOT CAH_OUTDIR)
    # Multi-config safe (expands to $(Configuration) on VS). Do not use $<CONFIG> here.
    # CMAKE_CFG_INTDIR behavior is documented by CMake.
    set(CAH_OUTDIR "${CMAKE_BINARY_DIR}/res/shaders/${CMAKE_CFG_INTDIR}")
  endif()

  # derive shader type and model from PROFILE (e.g. vs_5_0 -> Vertex, 5.0)
  if(NOT (CAH_PROFILE MATCHES "^([a-z]+)_([0-9]+)_([0-9]+)$"))
    message(FATAL_ERROR "colony_add_hlsl: PROFILE must look like vs_5_0 / ps_6_7 / cs_5_0, got: '${CAH_PROFILE}'")
  endif()
  set(_kind "${CMAKE_MATCH_1}")
  set(_sm_major "${CMAKE_MATCH_2}")
  set(_sm_minor "${CMAKE_MATCH_3}")

  set(_stype "Vertex")
  if(_kind STREQUAL "ps")
    set(_stype "Pixel")
  elseif(_kind STREQUAL "cs")
    set(_stype "Compute")
  elseif(_kind STREQUAL "gs")
    set(_stype "Geometry")
  elseif(_kind STREQUAL "hs")
    set(_stype "Hull")
  elseif(_kind STREQUAL "ds")
    set(_stype "Domain")
  elseif(NOT _kind STREQUAL "vs")
    message(FATAL_ERROR "colony_add_hlsl: unknown shader kind '${_kind}' in PROFILE '${CAH_PROFILE}'")
  endif()

  set(_smodel "${_sm_major}.${_sm_minor}") # e.g. "5.0" or "6.7" for VS property

  # Build up additional flags (defines, include dirs)
  set(_extra_flags "")
  if(CAH_DEFINES)
    # transform NAME or NAME=VALUE -> /DNAME or /DNAME=VALUE
    foreach(_d IN LISTS CAH_DEFINES)
      list(APPEND _extra_flags "/D${_d}")
    endforeach()
  endif()
  if(CAH_INCLUDE_DIRS)
    foreach(_inc IN LISTS CAH_INCLUDE_DIRS)
      list(APPEND _extra_flags "/I\"${_inc}\"")
    endforeach()
  endif()
  if(CAH_FLAGS)
    list(APPEND _extra_flags ${CAH_FLAGS})
  endif()
  if(_extra_flags)
    string(REPLACE ";" " " _extra_flags "${_extra_flags}")
  endif()

  # Ensure OUTDIR exists at build time (before shader compile runs)
  # PRE_BUILD runs early in VS; path contains $(Configuration) via CMAKE_CFG_INTDIR.
  if(TARGET ${CAH_TARGET})
    add_custom_command(TARGET ${CAH_TARGET} PRE_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CAH_OUTDIR}"
      VERBATIM
    )
  endif()

  foreach(_sh IN LISTS CAH_SOURCES)
    get_filename_component(_abs "${_sh}" ABSOLUTE)
    if(NOT EXISTS "${_abs}")
      message(FATAL_ERROR "colony_add_hlsl: source not found: ${_abs}")
    endif()
    get_filename_component(_namewe "${_abs}" NAME_WE)

    # Per-file Visual Studio shader properties (CMake -> MSBuild HLSL toolchain)
    set_source_files_properties("${_abs}" PROPERTIES
      VS_SHADER_TYPE              "${_stype}"       # Vertex/Pixel/Compute/...
      VS_SHADER_MODEL             "${_smodel}"      # "5.0" / "6.7" etc
      VS_SHADER_ENTRYPOINT        "${CAH_ENTRY}"    # entry point symbol
      VS_SHADER_OBJECT_FILE_NAME  "${CAH_OUTDIR}/${_namewe}.cso"  # final .cso path
    )
    if(_extra_flags)
      # Extra flags (defines, include dirs, misc) go via VS_SHADER_FLAGS property.
      set_source_files_properties("${_abs}" PROPERTIES VS_SHADER_FLAGS "${_extra_flags}")
    endif()

    # Optional: emit a header that embeds bytecode (handy for no‑IO startup).
    # Toggle by defining COLONY_HLSL_EMIT_HEADERS at configure time.
    if(COLONY_HLSL_EMIT_HEADERS)
      set_source_files_properties("${_abs}" PROPERTIES
        VS_SHADER_OUTPUT_HEADER_FILE "${CAH_OUTDIR}/${_namewe}.h"
        VS_SHADER_VARIABLE_NAME      "${_namewe}_bytecode"
      )
    endif()

    target_sources(${CAH_TARGET} PRIVATE "${_abs}")
  endforeach()
endfunction()

# -------- Discover sources (exclude vendored imgui if present) --------
file(GLOB_RECURSE SRC_ALL
  CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/src/*.c"
  "${CMAKE_SOURCE_DIR}/src/*.h"   "${CMAKE_SOURCE_DIR}/src/*.hpp"
  "${CMAKE_SOURCE_DIR}/platform/win/*.cpp" "${CMAKE_SOURCE_DIR}/platform/win/*.h"
)
list(FILTER SRC_ALL EXCLUDE REGEX ".*/third_party/imgui/.*")

# --- IMPORTANT: do NOT compile the standalone Windows launcher into the game EXE ---
#   The launcher has its own entry point; including it here would conflict with the
#   game's wWinMain now provided by src/app/EntryWinMain.cpp.
# (Old behavior removed)
# if(EXISTS "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
#   list(APPEND SRC_ALL "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
# endif()

# --- Remove legacy entry points and refactored code now added at top level ---
#   - Drop the old src/main.cpp to avoid duplicate main/wWinMain.
#   - Exclude src/app, src/render, src/terrain, src/core because these are now
#     injected by the top-level CMake via target_sources(ColonyGame ...).
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/main\\.cpp$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/app/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/render/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/terrain/.*\\.(c|cc|cxx|cpp)$")
list(FILTER SRC_ALL EXCLUDE REGEX ".*/src/core/.*\\.(c|cc|cxx|cpp)$")

# --- BEGIN: Enable crash-dump TU with per-file /EHa (fixes MSVC C2712) ---
# We keep /EHsc globally, but allow SEH + C++ unwinding just for CrashDumpWin.cpp.
if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp")
  # Make sure the stub isn't compiled.
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win/CrashDumpStub\\.cpp$")

  # Compile the real implementation with /EHa to support __try/__except.
  if(MSVC)
    set_source_files_properties("${CMAKE_SOURCE_DIR}/platform/win/CrashDumpWin.cpp"
      PROPERTIES COMPILE_OPTIONS "/EHa")
  endif()
endif()
# --- END: Enable crash-dump TU with per-file /EHa (fixes MSVC C2712) ---

# --- Frontend gating: build either the native Win32 frontend (default) or the SDL one ---
if(FRONTEND STREQUAL "sdl")
  # For SDL builds, drop Win32 entry points that would conflict with SDL2main
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/platform/win32_main\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/launcher/LauncherMain\\.cpp$")
else()
  # Default: Win32 frontend – exclude any SDL-only sources
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/Launcher_SDL\\.cpp$")
  list(FILTER SRC_ALL EXCLUDE REGEX ".*/SDL.*\\.(c|cpp)$")
endif()

add_executable(ColonyGame ${SRC_ALL})

# ------------------------- NEW: wire JSON-driven DXC shaders (Option B) -------------------------
# Include the helper and register shaders after the target is created.
# This expects cmake/ColonyShaders.cmake to define:
#   - colony_register_shaders(TARGET ... MANIFEST ... OUTPUT_DIR ... [INCLUDE_DIRS ...] [DXC_ARGS ...])
#   - colony_install_shaders(TARGET ... DESTINATION <dir>)
include(${CMAKE_SOURCE_DIR}/cmake/ColonyShaders.cmake OPTIONAL RESULT_VARIABLE _colony_shaders_included)

# Detect a shaders.json manifest in either renderer/Shaders or shaders
set(_COLONY_MANIFEST "")
if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
  set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
  set(_COLONY_MANIFEST "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
endif()

if(_COLONY_MANIFEST AND COMMAND colony_register_shaders)
  # Where to place compiled shader blobs during build:
  set(_COLONY_SHADER_OUT "${CMAKE_BINARY_DIR}/shaders")

  colony_register_shaders(
    TARGET       ColonyGame
    MANIFEST     "${_COLONY_MANIFEST}"
    OUTPUT_DIR   "${_COLONY_SHADER_OUT}"
    INCLUDE_DIRS
      ${CMAKE_SOURCE_DIR}/renderer/Shaders
      ${CMAKE_SOURCE_DIR}/renderer/Shaders/include
      ${CMAKE_SOURCE_DIR}/shaders
      ${CMAKE_SOURCE_DIR}/shaders/include
    DXC_ARGS     -nologo
  )

  # Optional install step for 'cmake --install'
  if(COMMAND colony_install_shaders)
    colony_install_shaders(TARGET ColonyGame DESTINATION bin/shaders)
  endif()

elseif(_COLONY_MANIFEST)
  message(WARNING
    "Found '${_COLONY_MANIFEST}', but 'colony_register_shaders()' is not defined. "
    "Ensure cmake/ColonyShaders.cmake provides it, or enable COLONY_USE_VS_HLSL=ON to let MSBuild compile HLSL."
  )
else()
  message(STATUS "No shaders.json manifest found; DXC shader registration skipped.")
endif()
# -----------------------------------------------------------------------------------------------

# Win32/Console subsystem toggle for the main EXE
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE YES)
endif()

target_compile_features(ColonyGame PRIVATE cxx_std_20)
target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_BINARY_DIR}/generated"
)

# Make sure this target is /utf-8 too (fmt 11.x requires it)
if(MSVC)
  target_compile_options(ColonyGame PRIVATE /utf-8)
endif()

# A4: Precompiled header (optional, gated by COLONY_USE_PCH)
if(COLONY_USE_PCH AND EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/src/pch.h")
endif()

# A4: Unity builds (opt‑in; speeds up large translation units)
if(COLONY_UNITY_BUILD)
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
endif()

# ---- (OLD PATH) Native VS HLSL compilation block (disabled by default) ----
# If you prefer Visual Studio/MSBuild to compile .hlsl files via VS_SHADER_* properties,
# enable this with: -DCOLONY_USE_VS_HLSL=ON
option(COLONY_USE_VS_HLSL "Use Visual Studio native HLSL compile of .hlsl sources" OFF)
if(COLONY_USE_VS_HLSL AND COMMAND colony_add_hlsl)
  # Location for compiled CSO/DXIL at build time (per-config).
  set(_SHADER_OUT "${CMAKE_BINARY_DIR}/res/shaders/${CMAKE_CFG_INTDIR}")

  # Pick shader profile set based on renderer
  if(COLONY_RENDERER STREQUAL "d3d12")
    set(_VS_PROF "vs_6_7")
    set(_PS_PROF "ps_6_7")
    set(_CS_PROF "cs_6_7")
  else()
    set(_VS_PROF "vs_5_0")
    set(_PS_PROF "ps_5_0")
    set(_CS_PROF "cs_5_0")
  endif()

  # Examples: Vertex + Pixel shaders (entrypoints VSMain / PSMain)
  if(EXISTS "${CMAKE_SOURCE_DIR}/shaders/quad_vs.hlsl")
    colony_add_hlsl(
      TARGET  ColonyGame
      SOURCES "${CMAKE_SOURCE_DIR}/shaders/quad_vs.hlsl"
      PROFILE "${_VS_PROF}"
      ENTRY   "VSMain"
      OUTDIR  "${_SHADER_OUT}")
  endif()

  if(EXISTS "${CMAKE_SOURCE_DIR}/shaders/quad_ps.hlsl")
    colony_add_hlsl(
      TARGET  ColonyGame
      SOURCES "${CMAKE_SOURCE_DIR}/shaders/quad_ps.hlsl"
      PROFILE "${_PS_PROF}"
      ENTRY   "PSMain"
      OUTDIR  "${_SHADER_OUT}")
  endif()

  # Compute shader: entrypoint is 'main'
  if(EXISTS "${CMAKE_SOURCE_DIR}/src/pcg/shaders/noise_fbm_cs.hlsl")
    colony_add_hlsl(
      TARGET  ColonyGame
      SOURCES "${CMAKE_SOURCE_DIR}/src/pcg/shaders/noise_fbm_cs.hlsl"
      PROFILE "${_CS_PROF}"
      ENTRY   "main"
      OUTDIR  "${_SHADER_OUT}")
  endif()
endif()

# Compile-time defines for optional systems + deterministic frontend switch
# Define COLONY_USE_SDL only when building the SDL frontend.
target_compile_definitions(ColonyGame PRIVATE
  $<$<STREQUAL:${FRONTEND},sdl>:COLONY_USE_SDL=1>
  $<$<BOOL:${ENABLE_IMGUI}>:CG_ENABLE_IMGUI=1>
  $<$<BOOL:${ENABLE_TRACY}>:TRACY_ENABLE=1>
)

# Link libs (propagate Windows flags/defs from interface target)
# Note: link SDL2 only for the SDL frontend; make SDL2main optional if the package doesn't provide it.
target_link_libraries(ColonyGame
  PRIVATE
    colony_build_options
    $<$<STREQUAL:${FRONTEND},sdl>:$<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>>
    $<$<STREQUAL:${FRONTEND},sdl>:SDL2::SDL2>
)

# Add Windows platform resources and helpers (manifest, version info, crash handler, GPU preference)
if(WIN32)
  if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
  endif()
  if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
    target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  endif()
  foreach(_f IN ITEMS
      "${CMAKE_SOURCE_DIR}/platform/win/CrashHandler.cpp"
      "${CMAKE_SOURCE_DIR}/src/platform/win/GpuPreference.cpp"
    )
    if(EXISTS "${_f}")
      target_sources(ColonyGame PRIVATE "${_f}")
    endif()
  endforeach()
  target_link_libraries(ColonyGame PRIVATE dbghelp shell32 ole32)
endif()

# Make the game the startup project in Visual Studio (must be set on the directory with 'project()')
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)

# Output directories per configuration (for multi-config generators like VS)
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${cfg}" CFGU)
    set_target_properties(ColonyGame PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_${CFGU} "${CMAKE_BINARY_DIR}/bin/${cfg}")
  endforeach()
endif()

# Stage assets next to the EXE after build (only if the source dirs exist)
if(EXISTS "${CMAKE_SOURCE_DIR}/res")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res" "$<TARGET_FILE_DIR:ColonyGame>/res"
    VERBATIM
  )
endif()

# ---- Copy shared shader header (common.hlsli) next to shipped shaders ----
if(EXISTS "${CMAKE_SOURCE_DIR}/shaders/common/common.hlsli")
  set(COLONY_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
  set(COLONY_SHADER_DST_DIR "$<TARGET_FILE_DIR:ColonyGame>/res/shaders")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${COLONY_SHADER_DST_DIR}/common"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${COLONY_SHADER_SRC_DIR}/common/common.hlsli"
            "${COLONY_SHADER_DST_DIR}/common/common.hlsli"
    VERBATIM
  )
endif()

# Stage compiled shader blobs (Option B output) next to the EXE.
# These were generated under ${CMAKE_BINARY_DIR}/shaders/$<CONFIG> by colony_register_shaders.
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>"
          "$<TARGET_FILE_DIR:ColonyGame>/res/shaders"
  VERBATIM
)

# ---------------- Quality/security toggles you can enable upstream ----------------
# For faster debugging, consider MSVC AddressSanitizer in Debug builds:
#   target_compile_options(ColonyGame PRIVATE $<$<CONFIG:Debug>:/fsanitize=address>)
#   target_link_options(ColonyGame    PRIVATE $<$<CONFIG:Debug>:/INFERASANLIBS>)
#
# For hardened Release builds, enable Control Flow Guard:
#   target_compile_options(ColonyGame PRIVATE $<$<CONFIG:Release>:/guard:cf>)
#   target_link_options(ColonyGame    PRIVATE $<$<CONFIG:Release>:/guard:cf>)
# ------------------------------------------------------------------------------  End of file
