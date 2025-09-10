# ==================================================================================================
# Colony-Game — Windows-only, modern CMake build (front-end switch: Win32 or SDL)
#
# Drop-in replacement for your top-level CMakeLists.txt.
#
# Goals:
#   - Windows-only toolchain, tuned for MSVC (also works with Ninja + MSVC toolset).
#   - Switchable frontends (Win32 native vs SDL) via -DFRONTEND=win32|sdl
#   - Safe, low-friction source discovery. SDL-only sources are compiled ONLY for FRONTEND=sdl.
#   - Strict warnings (opt-in), fast builds (Unity), stable runtime (fixed MSVC runtime knob).
#   - Post-build: copies assets and dependent DLLs (e.g., SDL2.dll) next to the .exe
#   - Future-proof toggles: LTO, ASAN (MSVC), PCH, GUI/Console subsystem.
# ==================================================================================================

cmake_minimum_required(VERSION 3.27)

# --------------------------------------------------------------------------------------------------
# Windows-only guard
# --------------------------------------------------------------------------------------------------
if(NOT WIN32)
  message(FATAL_ERROR "This CMake configuration is Windows-only. Please configure on Windows.")
endif()

# --------------------------------------------------------------------------------------------------
# Project
# --------------------------------------------------------------------------------------------------
project(ColonyGame
  VERSION 0.1.0
  DESCRIPTION "Colony-Game (Windows)"
  LANGUAGES C CXX)

# Export compile_commands.json (handy for IDEs and tooling)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use modern policies (esp. runtime library handling)
if(POLICY CMP0091)  # MSVC runtime library selection via CMAKE_MSVC_RUNTIME_LIBRARY
  cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0141)  # MSVC debug info format control
  cmake_policy(SET CMP0141 NEW)
endif()

# --------------------------------------------------------------------------------------------------
# Cache options (tweak as needed)
# --------------------------------------------------------------------------------------------------
set(DEFAULT_BUILD "RelWithDebInfo")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD}" CACHE STRING "Build type" FORCE)
endif()

# Frontend selector: 'win32' (default) or 'sdl'
set(FRONTEND "win32" CACHE STRING "Frontend to build: 'win32' or 'sdl'")
set_property(CACHE FRONTEND PROPERTY STRINGS "win32" "sdl")

# Build subsystem: GUI (no console) or Console (with console window)
option(SHOW_CONSOLE "Build the executable as a console subsystem" OFF)

# MSVC runtime: static (/MT) or dynamic (/MD)
option(MSVC_STATIC_RUNTIME "Link the MSVC runtime statically (/MT, /MTd)" OFF)

# Warnings as errors (strict mode)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# Unity builds (faster compiles for large TU like Game.cpp)
option(ENABLE_UNITY "Enable Unity/Jumbo builds for faster compilation" ON)

# Link-Time Optimization (LTO/IPO)
option(ENABLE_LTO "Enable Interprocedural Optimization (LTO)" ON)

# AddressSanitizer for MSVC (Debug only is recommended)
option(ENABLE_ASAN "Enable AddressSanitizer on MSVC (Debug/Dev only)" OFF)

# Precompiled headers (auto-detects 'pch.h' or you can point to a custom header)
set(PCH_HEADER "" CACHE FILEPATH "Optional precompiled header (e.g., src/pch.h)")

# Where your core source lives (auto)
set(SRC_DIR "src" CACHE PATH "Path to core source directory")

# Optional explicit override for the launcher file
set(LAUNCHER_FILE "" CACHE FILEPATH "Override: path to the launcher .cpp (Win32 or SDL)")

# --------------------------------------------------------------------------------------------------
# MSVC toolchain tuning
# --------------------------------------------------------------------------------------------------
if(MSVC)
  # /MP: parallel compile
  add_compile_options(/MP)

  # Default to UTF-8 source/exec charsets
  add_compile_options(/utf-8)

  # Strongly conforming mode & consistent preprocessor
  add_compile_options(/permissive- /Zc:__cplusplus /Zc:preprocessor)

  # Avoid <windows.h> slow includes, avoid std::min/max macro collisions
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

  # Quiet some noisy CRT deprecations (safe, project-local)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

  # Bigger obj for jumbo/templated code
  add_compile_options(/bigobj)

  # Warning level
  add_compile_options(/W4)
  if(WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()

  # MSVC runtime library selection
  if(MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()

  # Debug info format - fastlink for debug by default
  if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
  endif()
endif()

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Unity / IPO toggles
if(ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT has_ipo OUTPUT ipo_msg)
  if(has_ipo)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
  else()
    message(WARNING "IPO/LTO not supported by the current toolchain: ${ipo_msg}")
  endif()
endif()

# ASAN (MSVC supports /fsanitize=address in recent versions)
if(MSVC AND ENABLE_ASAN)
  add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
  add_link_options($<$<CONFIG:Debug>:/fsanitize=address>)
endif()

# Output folders (bin/{config}, lib/{config})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach(_cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${_cfg}" _CFG)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_cfg}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${_cfg}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/${_cfg}")
endforeach()

# --------------------------------------------------------------------------------------------------
# Source discovery (core engine)
#   - Pulls all sources under ${SRC_DIR}
#   - Excludes launchers by pattern
#   - SDL-only sources are identified below and added back ONLY for FRONTEND=sdl.
# --------------------------------------------------------------------------------------------------
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  "${SRC_DIR}/*.c" "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
  "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp")

# Exclude anything that looks like a launcher from core (keeps core reusable)
foreach(_rx IN ITEMS
  ".*/Launcher.*\\.cpp$"
  ".*/.*Launcher_.*\\.cpp$"
  ".*/SDL.*Launcher.*\\.cpp$"
  ".*/Win.*Launcher.*\\.cpp$"
  ".*/Main\\.cpp$"
  ".*/main\\.cpp$"
  # Also exclude the Win32 single-file launcher from the core:
  ".*/colonygame\\.cpp$"
)
  list(FILTER CORE_SOURCES EXCLUDE REGEX "${_rx}")
endforeach()

# ---- SDL-only translation units (compile ONLY for FRONTEND=sdl) --------------
set(SDL_ONLY_SOURCES "")
foreach(_cand IN ITEMS
  "src/game/Game.cpp"
)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
    list(APPEND SDL_ONLY_SOURCES "${CMAKE_SOURCE_DIR}/${_cand}")
  endif()
endforeach()
if(SDL_ONLY_SOURCES)
  # remove them from the default core set; added back later only for FRONTEND=sdl
  list(REMOVE_ITEM CORE_SOURCES ${SDL_ONLY_SOURCES})
endif()

# Create a static library for the core
add_library(colony_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(colony_core PUBLIC "${SRC_DIR}")

# Unity builds for the core (optional)
if(ENABLE_UNITY)
  set_property(TARGET colony_core PROPERTY UNITY_BUILD ON)
endif()

# PCH (optional, autodetect pch.h if not explicitly provided)
if(NOT PCH_HEADER)
  foreach(_cand IN ITEMS
    "${SRC_DIR}/pch.h"
    "${SRC_DIR}/PCH.h"
    "pch.h" "PCH.h")
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
      set(PCH_HEADER "${_cand}")
      break()
    endif()
  endforeach()
endif()
if(PCH_HEADER AND EXISTS "${PCH_HEADER}")
  target_precompile_headers(colony_core PRIVATE "${PCH_HEADER}")
endif()

# Useful defines for the core (frontend selection will add more below)
target_compile_definitions(colony_core PUBLIC
  $<$<CONFIG:Debug>:CG_DEBUG=1>
  $<$<CONFIG:RelWithDebInfo>:CG_RELWITHDEBINFO=1>
)

# --------------------------------------------------------------------------------------------------
# Version header (auto-generated; no repo file required)
# --------------------------------------------------------------------------------------------------
set(_ver_in  "${CMAKE_BINARY_DIR}/build_config.h.in")
set(_ver_out "${CMAKE_BINARY_DIR}/generated/build_config.h")
file(WRITE "${_ver_in}" [=[
#pragma once
#define CG_VERSION           "@PROJECT_VERSION@"
#define CG_FRONTEND          "@CG_FRONTEND@"
#define CG_BUILD_TYPE        "@CMAKE_BUILD_TYPE@"
#define CG_GIT_SHA           "@CG_GIT_SHA@"
#define CG_GIT_IS_DIRTY      @CG_GIT_IS_DIRTY@
#define CG_TOOLCHAIN         "@CMAKE_CXX_COMPILER_ID@ @CMAKE_CXX_COMPILER_VERSION@"
]=])

# try to get git sha (best-effort)
set(CG_GIT_SHA "unknown")
set(CG_GIT_IS_DIRTY 0)
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE CG_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" diff --quiet
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE GIT_DIFF_RESULT
    ERROR_QUIET)
  if(GIT_DIFF_RESULT EQUAL 1)
    set(CG_GIT_IS_DIRTY 1)
  endif()
endif()

# FRONTEND text goes in after we resolve the launcher; just configure_file later.
# Include dir for generated header:
target_include_directories(colony_core PUBLIC "${CMAKE_BINARY_DIR}/generated")

# --------------------------------------------------------------------------------------------------
# Launcher discovery / frontend target
#   - FRONTEND=win32: native Win32 launcher (no extra deps)
#   - FRONTEND=sdl:   SDL2 launcher (requires SDL2 installed)
#   - You can override with -DLAUNCHER_FILE="path/to/Launcher.cpp"
# --------------------------------------------------------------------------------------------------

# Potential launchers (auto-detection list)
set(_KNOWN_WIN_LAUNCHERS
  "WinLauncher.cpp"
  "colonygame.cpp"
  "src/colonygame.cpp"
  "src/platform/win32/WinLauncher.cpp"
  "platform/win32/WinLauncher.cpp"
  "src/Launcher_Win32.cpp"
  "Launcher_Win32.cpp"
  "src/WinLauncher.cpp"
)
set(_KNOWN_SDL_LAUNCHERS
  "Launcher_SDL.cpp"
  "SDLLauncher.cpp"
  "src/Launcher_SDL.cpp"
  "src/platform/sdl/Launcher_SDL.cpp"
  "platform/sdl/Launcher_SDL.cpp"
  "src/SDLLauncher.cpp"
)

# Decide subsystem (GUI or Console)
set(_WIN32_FLAG WIN32)
if(SHOW_CONSOLE)
  set(_WIN32_FLAG) # empty -> console subsystem (console window visible)
endif()

# Resolve launcher file
set(_LAUNCHER "")
set(CG_FRONTEND_STR "${FRONTEND}") # carries into header

# If overridden explicitly, trust it
if(LAUNCHER_FILE)
  if(EXISTS "${LAUNCHER_FILE}")
    set(_LAUNCHER "${LAUNCHER_FILE}")
  else()
    message(FATAL_ERROR "LAUNCHER_FILE does not exist: ${LAUNCHER_FILE}")
  endif()
elseif(FRONTEND STREQUAL "win32")
  foreach(_cand IN LISTS _KNOWN_WIN_LAUNCHERS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
      set(_LAUNCHER "${_cand}")
      break()
    endif()
  endforeach()
  if(NOT _LAUNCHER)
    message(FATAL_ERROR
      "Could not find a Win32 launcher source.\n"
      "Looked for: ${_KNOWN_WIN_LAUNCHERS}\n"
      "Override with -DLAUNCHER_FILE=path/to/WinLauncher.cpp")
  endif()
elseif(FRONTEND STREQUAL "sdl")
  foreach(_cand IN LISTS _KNOWN_SDL_LAUNCHERS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
      set(_LAUNCHER "${_cand}")
      break()
    endif()
  endforeach()
  if(NOT _LAUNCHER)
    message(FATAL_ERROR
      "Could not find an SDL launcher source.\n"
      "Looked for: ${_KNOWN_SDL_LAUNCHERS}\n"
      "Override with -DLAUNCHER_FILE=path/to/Launcher_SDL.cpp")
  endif()
else()
  message(FATAL_ERROR "FRONTEND must be 'win32' or 'sdl' (got: ${FRONTEND})")
endif()

# Resource (.rc) auto-discovery (optional icon/manifest)
set(_RES "")
foreach(_rc IN LISTS
  "app.rc" "App.rc"
  "src/app.rc" "src/App.rc"
  "res/app.rc" "res/App.rc"
  "resources/app.rc" "resources/App.rc")
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_rc}")
    set(_RES "${_rc}")
    break()
  endif()
endforeach()

# Configure version header now that FRONTEND is resolved
set(CG_FRONTEND "${CG_FRONTEND_STR}")
configure_file("${_ver_in}" "${_ver_out}" @ONLY)

# --------------------------------------------------------------------------------------------------
# Executable
# --------------------------------------------------------------------------------------------------
add_executable(ColonyGame ${_WIN32_FLAG} "${_LAUNCHER}" ${_RES})

# Set startup project for Visual Studio
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)

# Link core
target_link_libraries(ColonyGame PRIVATE colony_core)

# Helpful definitions for the launcher/plat layer
target_compile_definitions(ColonyGame PRIVATE
  CG_FRONTEND_${FRONTEND}=1
  $<$<BOOL:${SHOW_CONSOLE}>:CG_CONSOLE_APP=1>
)

# Win32 libs (common set; harmless if some are unused)
target_link_libraries(ColonyGame PRIVATE
  user32 gdi32 shell32 shlwapi comdlg32 comctl32 advapi32
  ole32 oleaut32 imm32 winmm version uuid)

# --------------------------------------------------------------------------------------------------
# SDL2 integration (FRONTEND=sdl)
#   - Robust: try Config (vcpkg/official) first, then legacy module.
#   - If only module variables are available, synthesize imported targets for target-based linking.
#   - Ensure core sees SDL headers when building SDL-only sources.
#   - Add SDL-only sources back into core.
# --------------------------------------------------------------------------------------------------
if(FRONTEND STREQUAL "sdl")
  # Add SDL-only files to core
  if(SDL_ONLY_SOURCES)
    target_sources(colony_core PRIVATE ${SDL_ONLY_SOURCES})
  endif()

  set(_SDL2_FOUND_KIND "none")
  find_package(SDL2 CONFIG QUIET)
  if(SDL2_FOUND)
    set(_SDL2_FOUND_KIND "config")
  else()
    # Try legacy module
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
      set(_SDL2_FOUND_KIND "module")
    endif()
  endif()

  if(NOT SDL2_FOUND)
    message(FATAL_ERROR
      "FRONTEND=sdl selected but SDL2 was not found.\n"
      "Install SDL2 (e.g., via vcpkg) or set SDL2_DIR / CMAKE_PREFIX_PATH.")
  endif()

  message(STATUS "SDL2 found via: ${_SDL2_FOUND_KIND}")

  # If we have imported targets (Config packages), prefer them
  set(_SDL_TARGET "")
  if(TARGET SDL2::SDL2)
    set(_SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2::SDL2-static)
    set(_SDL_TARGET SDL2::SDL2-static)
  endif()

  if(_SDL_TARGET)
    # Propagate headers to core (Game.cpp compiles there)
    target_link_libraries(colony_core PUBLIC ${_SDL_TARGET})
    target_link_libraries(ColonyGame PRIVATE ${_SDL_TARGET})
  else()
    # Legacy module: use SDL2_INCLUDE_DIRS/SDL2_LIBRARIES and synthesize imported targets.
    if(SDL2_INCLUDE_DIRS)
      target_include_directories(colony_core PUBLIC ${SDL2_INCLUDE_DIRS})
    endif()
    if(SDL2_LIBRARIES)
      target_link_libraries(ColonyGame PRIVATE ${SDL2_LIBRARIES})
    endif()
    # Create an INTERFACE imported target for consistent downstream usage
    if(NOT TARGET SDL2::SDL2)
      add_library(SDL2::SDL2 INTERFACE IMPORTED)
      if(SDL2_INCLUDE_DIRS)
        set_target_properties(SDL2::SDL2 PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}")
      endif()
      if(SDL2_LIBRARIES)
        set_target_properties(SDL2::SDL2 PROPERTIES
          INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}")
      endif()
    endif()
    target_link_libraries(colony_core PUBLIC SDL2::SDL2)
  endif()

  # SDL2main if available (helps on Windows for 'main' shim). Harmless if not present.
  if(TARGET SDL2::SDL2main)
    target_link_libraries(ColonyGame PRIVATE SDL2::SDL2main)
  elseif(DEFINED SDL2MAIN_LIBRARY AND SDL2MAIN_LIBRARY)
    target_link_libraries(ColonyGame PRIVATE "${SDL2MAIN_LIBRARY}")
  endif()

  # Flag for code that wants to know we built the SDL path
  target_compile_definitions(colony_core PUBLIC CG_USE_SDL=1)
endif()

# Unity for the executable (small benefit; core is where most value is)
if(ENABLE_UNITY)
  set_property(TARGET ColonyGame PROPERTY UNITY_BUILD ON)
endif()

# --------------------------------------------------------------------------------------------------
# Nice-to-haves for dev ergonomics
# --------------------------------------------------------------------------------------------------
# Working directory to the binary dir (VS debugger)
set_property(TARGET ColonyGame PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ColonyGame>")

# Post-build: copy assets/resources (if present) next to the .exe
set(_ASSET_DIRS assets Assets data Data res resources)
foreach(_d IN LISTS _ASSET_DIRS)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_d}")
    add_custom_command(TARGET ColonyGame POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
              "${CMAKE_SOURCE_DIR}/${_d}"
              "$<TARGET_FILE_DIR:ColonyGame>/${_d}"
      VERBATIM)
  endif()
endforeach()

# Post-build: copy dependent DLLs (e.g., SDL2.dll) next to the .exe
# Requires CMake 3.21+ for TARGET_RUNTIME_DLLS generator expression.
add_custom_command(TARGET ColonyGame POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          $<TARGET_RUNTIME_DLLS:ColonyGame>
          $<TARGET_FILE_DIR:ColonyGame>
  COMMAND_EXPAND_LISTS
  VERBATIM)

# Group sources in IDE by directory tree
get_target_property(_exe_sources ColonyGame SOURCES)
get_target_property(_core_sources colony_core SOURCES)
if(_exe_sources OR _core_sources)
  source_group(TREE "${CMAKE_SOURCE_DIR}" FILES ${_exe_sources} ${_core_sources} ${CORE_HEADERS})
endif()

# --------------------------------------------------------------------------------------------------
# Install / Package (optional; produces a portable folder with exe + assets)
#   - 'cmake --install <build_dir> --config Release'
#   - Generates: install root with bin/ColonyGame.exe and copied assets
# --------------------------------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS ColonyGame
  RUNTIME DESTINATION .
  COMPONENT runtime)

foreach(_d IN LISTS _ASSET_DIRS)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_d}")
    install(DIRECTORY "${_d}/" DESTINATION "${_d}" COMPONENT runtime)
  endif()
endforeach()

# --------------------------------------------------------------------------------------------------
# Summary (printed once at configure time)
# --------------------------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "================ Colony-Game: Configure Summary ================")
message(STATUS "  Generator            : ${CMAKE_GENERATOR}")
message(STATUS "  Build type           : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Frontend             : ${FRONTEND}")
message(STATUS "  Launcher             : ${_LAUNCHER}")
message(STATUS "  Show console         : ${SHOW_CONSOLE}")
message(STATUS "  MSVC static runtime  : ${MSVC_STATIC_RUNTIME}")
message(STATUS "  Warnings as errors   : ${WARNINGS_AS_ERRORS}")
message(STATUS "  Unity builds         : ${ENABLE_UNITY}")
message(STATUS "  LTO/IPO              : ${ENABLE_LTO}")
message(STATUS "  ASAN                 : ${ENABLE_ASAN}")
message(STATUS "  Source dir           : ${CMAKE_SOURCE_DIR}")
message(STATUS "  Binary dir           : ${CMAKE_BINARY_DIR}")
if(FRONTEND STREQUAL "sdl")
  if(SDL2_FOUND)
    message(STATUS "  SDL2                 : FOUND (${_SDL2_FOUND_KIND})")
  else()
    message(STATUS "  SDL2                 : NOT FOUND")
  endif()
endif()
message(STATUS "===============================================================")
message(STATUS "")

# --------------------------------------------------------------------------------------------------
# Usage hints:
#   - Default build (Win32 frontend, GUI app):
#       cmake -S . -B build -G "Visual Studio 17 2022"
#       cmake --build build --config RelWithDebInfo
#
#   - Show console window:
#       cmake -S . -B build -DFRONTEND=win32 -DSHOW_CONSOLE=ON
#
#   - Build SDL frontend (requires SDL2 in path or via vcpkg):
#       cmake -S . -B build -DFRONTEND=sdl
#
#   - Pick a specific launcher file:
#       cmake -S . -B build -DLAUNCHER_FILE=WinLauncher.cpp
#
#   - Static MSVC runtime (/MT):
#       cmake -S . -B build -DMSVC_STATIC_RUNTIME=ON
#
#   - Treat warnings as errors:
#       cmake -S . -B build -DWARNINGS_AS_ERRORS=ON
# --------------------------------------------------------------------------------------------------
