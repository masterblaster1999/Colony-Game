# cmake/CGOptions.cmake
#
# Centralizes user-facing cache variables / options for the project.
# (Project defaults like USE_FOLDERS / startup project moved to CGProjectDefaults.cmake)

include_guard(GLOBAL)

# ------------------------------ User-facing options ------------------------------

set(FRONTEND "" CACHE STRING "Frontend: '' (Win32 native) or 'sdl'")
set_property(CACHE FRONTEND PROPERTY STRINGS "" sdl)

option(ENABLE_IMGUI "Enable Dear ImGui UI (Win32 + D3D11 backends)" ON)
option(ENABLE_TRACY "Enable Tracy instrumentation client" ON)

option(SHOW_CONSOLE        "Show console for the Win32 executable(s)" OFF)
option(COLONY_USE_PCH      "Enable precompiled headers" ON)
option(COLONY_UNITY_BUILD  "Enable CMake Unity builds (jumbo)" OFF)

option(COLONY_ENABLE_COMPUTE_SHADERS
  "Compile compute shaders (*_cs.hlsl) if they define CSMain"
  OFF
)

option(COLONY_BUILD_LAUNCHER "Build native Windows launcher (separate EXE)" ON)

option(COLONY_WERROR "Treat compiler warnings as errors when compiling colony_core" OFF)

# NOTE:
# Some helper functions use COLONY_WARNINGS_AS_ERRORS, while some targets use
# COLONY_WERROR. Keep them in sync.
set(COLONY_WARNINGS_AS_ERRORS ${COLONY_WERROR})

# Path to a shared PCH header (relative to repo root).
#
# IMPORTANT:
#   This must be a *file*, not a directory. A previous default of "" (empty)
#   could resolve to the repo root directory in some subprojects and cause
#   MSVC to error out while building CMake's generated PCH include (cmake_pch.hxx)
#   with e.g. fatal error C1083.
set(COLONY_PCH_HEADER "src/pch.h" CACHE STRING "Path to a shared PCH header (relative to repo root)")

# Backwards-compatible safety net:
#   If an existing build directory cached COLONY_PCH_HEADER as empty (or a
#   directory), force a sane default so users don't need to delete their build
#   folder to recover.
if(COLONY_USE_PCH)
  if(NOT COLONY_PCH_HEADER)
    message(STATUS "COLONY_PCH_HEADER was empty; defaulting to src/pch.h")
    set(COLONY_PCH_HEADER "src/pch.h" CACHE STRING "Path to a shared PCH header (relative to repo root)" FORCE)
  endif()

  if(IS_ABSOLUTE "${COLONY_PCH_HEADER}")
    set(_cg_pch_abs "${COLONY_PCH_HEADER}")
  else()
    set(_cg_pch_abs "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
  endif()

  if(EXISTS "${_cg_pch_abs}" AND IS_DIRECTORY "${_cg_pch_abs}")
    message(WARNING "COLONY_PCH_HEADER points to a directory: ${_cg_pch_abs}. Falling back to src/pch.h")
    set(COLONY_PCH_HEADER "src/pch.h" CACHE STRING "Path to a shared PCH header (relative to repo root)" FORCE)
  endif()

  # If a user set a non-existent header (common when reusing a build dir across
  # branches), fall back rather than failing deep inside target_precompile_headers.
  if(IS_ABSOLUTE "${COLONY_PCH_HEADER}")
    set(_cg_pch_abs2 "${COLONY_PCH_HEADER}")
  else()
    set(_cg_pch_abs2 "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
  endif()

  if(NOT EXISTS "${_cg_pch_abs2}")
    message(WARNING "COLONY_PCH_HEADER does not exist: ${_cg_pch_abs2}. Falling back to src/pch.h")
    set(COLONY_PCH_HEADER "src/pch.h" CACHE STRING "Path to a shared PCH header (relative to repo root)" FORCE)
  endif()

  unset(_cg_pch_abs2)

  unset(_cg_pch_abs)
endif()

# ------------------------------ Windows-only defaults / guards ------------------------------

if(WIN32)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
  set(ENABLE_IMGUI OFF CACHE BOOL "Enable Dear ImGui UI (Win32 + D3D11 backends)" FORCE)
  set(ENABLE_TRACY OFF CACHE BOOL "Enable Tracy instrumentation client" FORCE)
  set(COLONY_BUILD_LAUNCHER OFF CACHE BOOL "Build native Windows launcher (separate EXE)" FORCE)
endif()
