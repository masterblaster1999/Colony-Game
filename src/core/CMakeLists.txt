# ===========================
# File: src/core/CMakeLists.txt
# ===========================
cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)

include_guard(GLOBAL)

# Core code is intended for the Windows-only build this repo enforces.
if(NOT WIN32)
  message(FATAL_ERROR "src/core is Windows-only in this repository.")
endif()

# If someone configures *this folder* directly (rare), make it stand alone.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(colony_core LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ---- Sources (core only) ------------------------------------------------------
# NOTE: This is intentionally scoped to src/core/** to keep responsibilities clean:
# - app entrypoints live in src/app
# - Win32 platform helpers live in src/platform/win
file(GLOB_RECURSE _CORE_CPP CONFIGURE_DEPENDS
  "${_CORE_DIR}/*.cpp"
  "${_CORE_DIR}/*.c"
)
file(GLOB_RECURSE _CORE_HDR CONFIGURE_DEPENDS
  "${_CORE_DIR}/*.hpp"
  "${_CORE_DIR}/*.h"
)

# Normalize slashes for consistent filtering and VS source grouping.
set(_CORE_SOURCES "")
foreach(_f IN LISTS _CORE_CPP _CORE_HDR)
  file(TO_CMAKE_PATH "${_f}" _f_norm)
  list(APPEND _CORE_SOURCES "${_f_norm}")
endforeach()
list(REMOVE_DUPLICATES _CORE_SOURCES)

unset(_CORE_CPP)
unset(_CORE_HDR)

# ---- Target -------------------------------------------------------------------
if(NOT TARGET colony_core)
  add_library(colony_core STATIC)
  add_library(colony::core ALIAS colony_core)
endif()

target_sources(colony_core PRIVATE ${_CORE_SOURCES})

target_compile_features(colony_core PUBLIC cxx_std_23)

target_include_directories(colony_core
  PUBLIC
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
  PRIVATE
    "${_CORE_DIR}"
)

# Windows hygiene (avoid macro pollution)
target_compile_definitions(colony_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Unity build: keep core conservative unless explicitly enabled.
if(DEFINED COLONY_UNITY_BUILD AND COLONY_UNITY_BUILD)
  set_target_properties(colony_core PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
else()
  set_target_properties(colony_core PROPERTIES UNITY_BUILD OFF)
endif()

# Visual Studio folder organization
set_target_properties(colony_core PROPERTIES FOLDER "colony/libs")

# ---- PCH (best-effort; works with top-level options) --------------------------
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
      colony_enable_pch(colony_core PRIVATE "${_PCH}")
    else()
      target_precompile_headers(colony_core PRIVATE "${_PCH}")
    endif()
  endif()
  unset(_PCH)
endif()

# ---- Repo-provided helpers (safe if missing) ----------------------------------
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(colony_core)
endif()
if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(colony_core)
endif()

# Apply consistent warning level (/W4, /permissive-, etc.) when the helper is
# available (it is provided by the top-level build).
if(COMMAND colony_enable_warnings)
  colony_enable_warnings(colony_core)
endif()

# IDE grouping (optional but nice)
if(_CORE_SOURCES)
  source_group(TREE "${CMAKE_SOURCE_DIR}/src" PREFIX "Source" FILES ${_CORE_SOURCES})
endif()

unset(_CORE_DIR)
unset(_CORE_SOURCES)
