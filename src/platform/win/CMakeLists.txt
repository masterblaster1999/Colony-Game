# =====================================
# File: src/platform/win/CMakeLists.txt
# =====================================
#
# Canonical Windows platform implementation lives under:
#   src/platform/win
#
# A historical/legacy implementation also exists under:
#   platform/win
#
# To prevent the classic “header exists but .cpp never builds” failure mode, the
# canonical platform target (colony_platform_win) is defined from the sources in
# THIS directory. Legacy code is opt-in.
#

set(_WIN_DIR "${CMAKE_SOURCE_DIR}/src/platform/win")
set(_LEGACY_DIR "${CMAKE_SOURCE_DIR}/platform/win") # legacy (optional)

# -----------------------------------------------------
# Source discovery (canonical)
# -----------------------------------------------------
file(GLOB_RECURSE _WIN_TU CONFIGURE_DEPENDS
  "${_WIN_DIR}/*.c"
  "${_WIN_DIR}/*.cpp"
)

# Exclude entrypoints / executable glue that should not live in the platform lib.
#
# IMPORTANT:
#   CrashInitBridge.cpp defines wincrash::InitCrashHandler(...) which is also
#   defined by the ColonyGame executable (src/CrashDump.cpp). If we compile the
#   bridge into the platform library as well, MSVC will fail the link with a
#   duplicate symbol (LNK2005).
set(_cg_exclude_regex "(^|/)(HighPerfGPU|GpuPreference|WinLauncher|AppMain|EntryWinMain|EntryPoint|WinMain|win32_main|main_win|Bootstrap|WinBootstrap|CrashInitBridge)\.(c|cpp)$")
list(FILTER _WIN_TU EXCLUDE REGEX "${_cg_exclude_regex}")

# Headers (IDE visibility)
file(GLOB_RECURSE _WIN_HDR CONFIGURE_DEPENDS
  "${_WIN_DIR}/*.h"
  "${_WIN_DIR}/*.hpp"
  "${_WIN_DIR}/*.hh"
  "${_WIN_DIR}/*.inl"
)

# -----------------------------------------------------
# Legacy (opt-in)
# -----------------------------------------------------
option(
  COLONY_PLATFORM_WIN_INCLUDE_LEGACY
  "Also compile legacy sources from /platform/win into colony_platform_win (temporary migration aid)."
  OFF
)

set(_LEG_WIN_TU "")
set(_LEG_WIN_HDR "")

if(COLONY_PLATFORM_WIN_INCLUDE_LEGACY AND EXISTS "${_LEGACY_DIR}")
  file(GLOB_RECURSE _LEG_WIN_TU CONFIGURE_DEPENDS
    "${_LEGACY_DIR}/*.c"
    "${_LEGACY_DIR}/*.cpp"
  )
  list(FILTER _LEG_WIN_TU EXCLUDE REGEX "${_cg_exclude_regex}")

  file(GLOB_RECURSE _LEG_WIN_HDR CONFIGURE_DEPENDS
    "${_LEGACY_DIR}/*.h"
    "${_LEGACY_DIR}/*.hpp"
    "${_LEGACY_DIR}/*.hh"
    "${_LEGACY_DIR}/*.inl"
  )

  # Prefer canonical implementations when a basename exists in both trees.
  set(_canon_basenames "")
  foreach(_s IN LISTS _WIN_TU)
    get_filename_component(_bn "${_s}" NAME)
    list(APPEND _canon_basenames "${_bn}")
  endforeach()
  list(REMOVE_DUPLICATES _canon_basenames)

  set(_LEG_KEEP "")
  set(_dup_basenames "")
  foreach(_s IN LISTS _LEG_WIN_TU)
    get_filename_component(_bn "${_s}" NAME)
    if(_bn IN_LIST _canon_basenames)
      list(APPEND _dup_basenames "${_bn}")
    else()
      list(APPEND _LEG_KEEP "${_s}")
    endif()
  endforeach()
  set(_LEG_WIN_TU "${_LEG_KEEP}")
  list(REMOVE_DUPLICATES _dup_basenames)

  if(_dup_basenames)
    if(COLONY_STRICT_WIN_PLATFORM_DEDUP)
      message(FATAL_ERROR
        "Duplicate Windows platform .cpp basenames found in BOTH trees:\n"
        "  src/platform/win AND platform/win\n"
        "Duplicates:\n  ${_dup_basenames}\n"
        "Fix: delete/move the legacy copies under platform/win (preferred), or disable COLONY_STRICT_WIN_PLATFORM_DEDUP."
      )
    else()
      message(WARNING
        "Duplicate Windows platform .cpp basenames found in BOTH trees; legacy duplicates will be ignored.\n"
        "Duplicates:\n  ${_dup_basenames}\n"
        "Tip: delete/move the legacy copies under platform/win and enable COLONY_STRICT_WIN_PLATFORM_DEDUP to enforce."
      )
    endif()
  endif()
endif()

set(_PLAT_SOURCES
  ${_WIN_TU}
  ${_LEG_WIN_TU}
  ${_WIN_HDR}
  ${_LEG_WIN_HDR}
)

# -----------------------------------------------------
# Target
# -----------------------------------------------------
if(NOT TARGET colony_platform_win)
  if(_PLAT_SOURCES)
    add_library(colony_platform_win STATIC)
    target_sources(colony_platform_win PRIVATE ${_PLAT_SOURCES})
  else()
    add_library(colony_platform_win INTERFACE)
    message(STATUS "colony_platform_win: no sources found; created INTERFACE target.")
  endif()

  # Provide both the new and legacy alias spellings used around the repo.
  add_library(colony::platform_win ALIAS colony_platform_win)
  add_library(Colony::PlatformWin ALIAS colony_platform_win)

  get_target_property(_cg_type colony_platform_win TYPE)
  if(_cg_type STREQUAL "INTERFACE_LIBRARY")
    set(_cg_pub INTERFACE)
  else()
    set(_cg_pub PUBLIC)
  endif()

  target_compile_features(colony_platform_win ${_cg_pub} cxx_std_23)

  # Include order matters:
  #  - include/ : public headers
  #  - src/     : canonical internal headers (including src/platform/win)
  #  - repo root: legacy fallback (platform/win) for transitional builds / tools
  target_include_directories(colony_platform_win ${_cg_pub}
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>"
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${COLONY_INSTALL_INCLUDEDIR}>"
  )

  if(NOT _cg_type STREQUAL "INTERFACE_LIBRARY")
    # Allow local includes like "WinCommon.h" inside src/platform/win/*.cpp
    target_include_directories(colony_platform_win PRIVATE "${_WIN_DIR}")

    target_compile_definitions(colony_platform_win ${_cg_pub}
      WIN32_LEAN_AND_MEAN
      NOMINMAX
    )

    # Propagate Win32 import libs required by the platform objects to consumers.
    # (Static libs need these to be PUBLIC so final EXEs link cleanly.)
    target_link_libraries(colony_platform_win ${_cg_pub}
      dbghelp
      shlwapi
      shell32
      ole32
    )

    # Enable async exception handling for CrashDumpWin.cpp when legacy is enabled.
    if(MSVC)
      foreach(_s IN LISTS _WIN_TU _LEG_WIN_TU)
        if(_s MATCHES "(^|/)(CrashDumpWin)\.cpp$")
          set_source_files_properties("${_s}" PROPERTIES COMPILE_OPTIONS "/EHa")
        endif()
      endforeach()
    endif()

    colony_enable_warnings(colony_platform_win)
    colony_apply_msvc_runtime(colony_platform_win)
    colony_apply_debug_runtime_checks(colony_platform_win)

    if(COLONY_ENABLE_PCH)
      target_precompile_headers(colony_platform_win PRIVATE "${CMAKE_SOURCE_DIR}/cmake/pch.hpp")
    endif()

    # For /std:c++latest compatibility and better incremental linking behavior.
    set_property(TARGET colony_platform_win PROPERTY UNITY_BUILD OFF)
  endif()

  unset(_cg_type)
  unset(_cg_pub)
endif()

unset(_WIN_DIR)
unset(_LEGACY_DIR)
unset(_WIN_TU)
unset(_LEG_WIN_TU)
unset(_WIN_HDR)
unset(_LEG_WIN_HDR)
unset(_PLAT_SOURCES)
unset(_cg_exclude_regex)
