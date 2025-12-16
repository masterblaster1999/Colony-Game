# =====================================
# File: src/platform/win/CMakeLists.txt
# =====================================
cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)

include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR "src/platform/win is Windows-only.")
endif()

# Standalone convenience (rare).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(colony_platform_win LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(_WIN_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_LEGACY_DIR "${CMAKE_SOURCE_DIR}/platform/win") # optional legacy folder

# ---- Collect src/platform/win -------------------------------------------------
file(GLOB_RECURSE _SRC_WIN_TU CONFIGURE_DEPENDS
  "${_WIN_DIR}/*.cpp"
  "${_WIN_DIR}/*.c"
)
file(GLOB_RECURSE _SRC_WIN_HDR CONFIGURE_DEPENDS
  "${_WIN_DIR}/*.hpp"
  "${_WIN_DIR}/*.h"
)

# ---- Collect legacy platform/win (optional) -----------------------------------
set(_LEG_WIN_TU "")
set(_LEG_WIN_HDR "")
if(EXISTS "${_LEGACY_DIR}")
  file(GLOB_RECURSE _LEG_WIN_TU CONFIGURE_DEPENDS
    "${_LEGACY_DIR}/*.cpp"
    "${_LEGACY_DIR}/*.c"
  )
  file(GLOB_RECURSE _LEG_WIN_HDR CONFIGURE_DEPENDS
    "${_LEGACY_DIR}/*.hpp"
    "${_LEGACY_DIR}/*.h"
  )
endif()

# Normalize paths to forward slashes.
function(_cg_norm_list out_var)
  set(_out "")
  foreach(_f IN LISTS ARGN)
    file(TO_CMAKE_PATH "${_f}" _n)
    list(APPEND _out "${_n}")
  endforeach()
  set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

_cg_norm_list(_SRC_WIN_TU_N ${_SRC_WIN_TU})
_cg_norm_list(_SRC_WIN_HDR_N ${_SRC_WIN_HDR})
_cg_norm_list(_LEG_WIN_TU_N ${_LEG_WIN_TU})
_cg_norm_list(_LEG_WIN_HDR_N ${_LEG_WIN_HDR})

unset(_SRC_WIN_TU)
unset(_SRC_WIN_HDR)
unset(_LEG_WIN_TU)
unset(_LEG_WIN_HDR)

# ---- Exclusions ---------------------------------------------------------------
# Keep these out of the platform library:
# - EXE entrypoints (WinMain / launcher mains)
# - GPU preference exports (must live in the EXE)
# - Bootstrap "app glue" TUs (should live in the EXE)
set(_EXCLUDE_REGEX "(^|/)(HighPerfGPU|GpuPreference|WinLauncher|AppMain|EntryWinMain|EntryPoint|WinMain|win32_main|main_win|Bootstrap|WinBootstrap)\\.(c|cpp)$")

list(FILTER _SRC_WIN_TU_N EXCLUDE REGEX "${_EXCLUDE_REGEX}")
list(FILTER _LEG_WIN_TU_N EXCLUDE REGEX "${_EXCLUDE_REGEX}")

# ---- Dedup legacy vs src by basename ------------------------------------------
# If the same basename exists in BOTH trees, keep src/platform/win and drop legacy.
set(_SRC_BASENAMES "")
foreach(_f IN LISTS _SRC_WIN_TU_N)
  get_filename_component(_bn "${_f}" NAME)
  list(APPEND _SRC_BASENAMES "${_bn}")
endforeach()
list(REMOVE_DUPLICATES _SRC_BASENAMES)

set(_LEG_KEEP "")
set(_DUP_BASENAMES "")
foreach(_f IN LISTS _LEG_WIN_TU_N)
  get_filename_component(_bn "${_f}" NAME)
  list(FIND _SRC_BASENAMES "${_bn}" _idx)
  if(_idx EQUAL -1)
    list(APPEND _LEG_KEEP "${_f}")
  else()
    list(APPEND _DUP_BASENAMES "${_bn}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _DUP_BASENAMES)

if(_DUP_BASENAMES)
  if(DEFINED COLONY_STRICT_WIN_PLATFORM_DEDUP AND COLONY_STRICT_WIN_PLATFORM_DEDUP)
    message(FATAL_ERROR
      "Duplicate Win platform TU basenames exist in BOTH:\n"
      "  ${_WIN_DIR}\n"
      "  ${_LEGACY_DIR}\n"
      "Duplicates:\n  ${_DUP_BASENAMES}\n"
      "Fix: delete/move the legacy copies under platform/win."
    )
  else()
    message(WARNING
      "Duplicate Win platform TU basenames exist in BOTH:\n"
      "  ${_WIN_DIR}\n"
      "  ${_LEGACY_DIR}\n"
      "Legacy duplicates will be ignored:\n  ${_DUP_BASENAMES}\n"
      "Recommendation: delete/move the legacy copies under platform/win."
    )
  endif()
endif()

set(_PLAT_TU ${_SRC_WIN_TU_N} ${_LEG_KEEP})
set(_PLAT_HDR ${_SRC_WIN_HDR_N} ${_LEG_WIN_HDR_N})

list(REMOVE_DUPLICATES _PLAT_TU)
list(REMOVE_DUPLICATES _PLAT_HDR)

unset(_LEG_KEEP)
unset(_DUP_BASENAMES)
unset(_SRC_BASENAMES)
unset(_idx)
unset(_bn)
unset(_f)

# ---- Target -------------------------------------------------------------------
if(NOT TARGET colony_platform_win)
  add_library(colony_platform_win STATIC)
  add_library(colony::platform_win ALIAS colony_platform_win)
endif()

target_sources(colony_platform_win PRIVATE ${_PLAT_TU} ${_PLAT_HDR})

target_compile_features(colony_platform_win PUBLIC cxx_std_23)

target_include_directories(colony_platform_win
  PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
  PRIVATE
    "${_WIN_DIR}"
)

if(MSVC)
  target_compile_definitions(colony_platform_win PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# Common Win32 libs used by typical platform helpers / crash utils / path utils.
target_link_libraries(colony_platform_win PRIVATE dbghelp shlwapi shell32)

# Unity build: default OFF for platform code unless explicitly enabled.
if(DEFINED COLONY_UNITY_BUILD AND COLONY_UNITY_BUILD)
  set_target_properties(colony_platform_win PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
else()
  set_target_properties(colony_platform_win PROPERTIES UNITY_BUILD OFF)
endif()

set_target_properties(colony_platform_win PROPERTIES FOLDER "colony/libs")

# PCH (best-effort)
if(DEFINED COLONY_USE_PCH AND COLONY_USE_PCH)
  set(_PCH "")
  if(DEFINED COLONY_PCH_HEADER AND EXISTS "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
    set(_PCH "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.hpp")
    set(_PCH "${CMAKE_SOURCE_DIR}/src/pch.hpp")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.h")
    set(_PCH "${CMAKE_SOURCE_DIR}/src/pch.h")
  endif()

  if(_PCH)
    if(COMMAND colony_enable_pch)
      colony_enable_pch(colony_platform_win PRIVATE "${_PCH}")
    else()
      target_precompile_headers(colony_platform_win PRIVATE "${_PCH}")
    endif()
  endif()
  unset(_PCH)
endif()

# Ensure CrashDumpWin.cpp uses /EHa if repo enables that pattern.
if(COMMAND colony_apply_seh_for_crashdump)
  colony_apply_seh_for_crashdump(colony_platform_win)
elseif(MSVC AND (NOT DEFINED COLONY_SEH_CRASHDUMP OR COLONY_SEH_CRASHDUMP))
  foreach(_s IN LISTS _PLAT_TU)
    if(_s MATCHES "CrashDumpWin\\.cpp$")
      set_source_files_properties("${_s}" PROPERTIES COMPILE_OPTIONS "/EHa")
    endif()
  endforeach()
endif()

# Repo helpers (safe if missing)
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(colony_platform_win)
endif()
if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(colony_platform_win)
endif()

# IDE grouping
if(_PLAT_TU OR _PLAT_HDR)
  source_group(TREE "${CMAKE_SOURCE_DIR}" PREFIX "Platform\\Win" FILES ${_PLAT_TU} ${_PLAT_HDR})
endif()

unset(_WIN_DIR)
unset(_LEGACY_DIR)
unset(_PLAT_TU)
unset(_PLAT_HDR)
unset(_SRC_WIN_TU_N)
unset(_SRC_WIN_HDR_N)
unset(_LEG_WIN_TU_N)
unset(_LEG_WIN_HDR_N)
unset(_EXCLUDE_REGEX)
