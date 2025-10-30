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
endif()

# ------------------------------------------------------------------------------
# Project modules
# ------------------------------------------------------------------------------
include(CGOptions)
include(CGThirdParty)
include(CGShaders)

# ------------------------------------------------------------------------------
# vcpkg dependencies
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
        if(_src MATCHES "CrashDumpWin\\.cpp$")
          set_source_files_properties(${_src} PROPERTIES COMPILE_OPTIONS "/EHa")
        endif()
      endforeach()
    endif()
  endif()
endif()

# ------------------------------------------------------------------------------
# Stage / install / tests / summary
# ------------------------------------------------------------------------------
include(CGGameTarget)
include(CGStageInstall)
include(CGTests)
include(CGSummary)
