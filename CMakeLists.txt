# ==================================================================================================
# Colony-Game — Windows-only, modern CMake build (Win32 or SDL front-end)
#
# Massive-upgrade edition:
#   * Deterministic & tuned MSVC release, parallel link, reproducible-ish binaries
#   * Optional static analysis + clang-tidy
#   * Optional sccache/ccache compiler launcher
#   * PCH for core and (optionally) the executable
#   * Stronger Windows policy knobs (min OS version define)
#   * Unity knobs (batch sizes + selective skip)
#   * Bootstrap isolation (win/win32) — keeps platform bootstrap out of colony_core
#   * Rich staging & packaging (includes frontend + git sha in the ZIP name)
#   * Clean integration with EnableWinQoL (resources, installs, asset staging)
#   * Optional code signing hook for Release artifacts
#   * (PATCH) Separate WinLauncher bootstrapper exe that spawns the game exe
# ==================================================================================================

cmake_minimum_required(VERSION 3.16)

# --------------------------------------------------------------------------------------------------
# Optional: auto-enable vcpkg toolchain before project() if available (nice for SDL2)
# --------------------------------------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  if(EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file" FORCE)
  endif()
endif()

# Prefer config packages when available (SDL2, etc.)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Group targets into folders in Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

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

# Export compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Modern policies
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

# --------------------------------------------------------------------------------------------------
# Cache options
# --------------------------------------------------------------------------------------------------
set(DEFAULT_BUILD "RelWithDebInfo")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD}" CACHE STRING "Build type" FORCE)
endif()

set(FRONTEND "win32" CACHE STRING "Frontend to build: 'win32' or 'sdl'")
set_property(CACHE FRONTEND PROPERTY STRINGS "win32" "sdl")

option(SHOW_CONSOLE "Build the executable as a console subsystem" OFF)
set(CG_MIN_WINVER "0x0A00" CACHE STRING "Min Windows version (e.g. 0x0601=Win7, 0x0A00=Win10)")
option(MSVC_STATIC_RUNTIME "Link the MSVC runtime statically (/MT, /MTd)" OFF)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(ENABLE_UNITY "Enable Unity/Jumbo builds for faster compilation" ON)
set(UNITY_BATCH_SIZE_CORE 32 CACHE STRING "Unity batch size for colony_core")
set(UNITY_BATCH_SIZE_EXE  16 CACHE STRING "Unity batch size for ColonyGame")
option(ENABLE_LTO "Enable Interprocedural Optimization (LTO)" ON)
option(ENABLE_ASAN "Enable AddressSanitizer on MSVC (Debug/Dev only)" OFF)
set(PCH_HEADER "" CACHE FILEPATH "Optional PCH for the core (e.g., src/pch.h)")
set(PCH_HEADER_EXE "" CACHE FILEPATH "Optional PCH for the executable (e.g., src/pch.h)")
option(ENABLE_RELEASE_PDB "Emit Program Database (PDB) in Release" ON)
option(SUPPRESS_MSVC_C4505 "Suppress MSVC warning C4505 on the launcher target" ON)
option(ENABLE_MSVC_ANALYZE "Enable MSVC /analyze (can be slow)" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy if available" OFF)
option(ENABLE_COMPILER_CACHE "Use compiler cache (sccache/ccache) if detected in PATH" ON)
set(SRC_DIR "src" CACHE PATH "Path to core source directory")

# (PATCH) Split: game entry vs. bootstrapper
set(LAUNCHER_FILE "" CACHE FILEPATH "Override: path to the GAME ENTRY .cpp (win32_main/SDL main)")
set(BOOTSTRAP_FILE "" CACHE FILEPATH "Override: path to the bootstrapper .cpp (WinLauncher.cpp)")
option(BUILD_BOOTSTRAPPER "Build a separate Windows bootstrap exe (WinLauncher) that launches ColonyGame.exe" ON)

# Optional code signing knobs (ignored if not configured)
option(ENABLE_CODE_SIGNING "Sign Release binaries if signtool/cert are configured" OFF)
set(SIGNTOOL_PATH "" CACHE FILEPATH "Path to signtool.exe (optional if in PATH)")
set(SIGNING_CERT_PATH "" CACHE FILEPATH "Path to .pfx certificate file")
set(SIGNING_CERT_PASSWORD "" CACHE STRING "Password for the code signing certificate (PFX)")
set(SIGNING_TIMESTAMP_URL "http://timestamp.digicert.com" CACHE STRING "RFC3161 timestamp URL")

# --------------------------------------------------------------------------------------------------
# Compiler cache launcher (sccache/ccache)
# --------------------------------------------------------------------------------------------------
if(ENABLE_COMPILER_CACHE)
  foreach(_c IN ITEMS sccache ccache)
    find_program(_${_c}_BIN NAMES ${_c})
    if(_${_c}_BIN)
      message(STATUS "Compiler cache detected: ${_${_c}_BIN}")
      set(CMAKE_C_COMPILER_LAUNCHER   ${_${_c}_BIN})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${_${_c}_BIN})
      break()
    endif()
  endforeach()
endif()

# --------------------------------------------------------------------------------------------------
# MSVC toolchain tuning
# --------------------------------------------------------------------------------------------------
if(MSVC)
  add_compile_options(/MP /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /Zc:throwingNew /Zc:inline /bigobj)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=${CG_MIN_WINVER} WINVER=${CG_MIN_WINVER})
  add_compile_options(/external:anglebrackets /external:W0)
  add_compile_options(/EHsc /W4)
  if(WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
  if(ENABLE_MSVC_ANALYZE)
    add_compile_options(/analyze)
  endif()
  # ---------- Static runtime default (merged patch) ----------
  if(MSVC_STATIC_RUNTIME)
    # /MT for Release, /MTd for Debug
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  else()
    # /MD for Release, /MDd for Debug
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
  # ----------------------------------------------------------

  if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
  endif()
  add_link_options(/DYNAMICBASE /NXCOMPAT /guard:cf)
  add_link_options("$<$<CONFIG:Release>:/INCREMENTAL:NO>")
  add_link_options("$<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO>")
  add_link_options("$<$<CONFIG:Release>:/OPT:REF;/OPT:ICF;/Brepro>")
  add_link_options("$<$<CONFIG:RelWithDebInfo>:/OPT:REF;/OPT:ICF>")
  include(ProcessorCount)
  ProcessorCount(_cg_nproc)
  if(NOT _cg_nproc EQUAL 0)
    add_link_options("/cgthreads:${_cg_nproc}")
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

if(MSVC AND ENABLE_ASAN)
  add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
  add_link_options($<$<CONFIG:Debug>:/fsanitize=address>)
endif()

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
# Core sources
# --------------------------------------------------------------------------------------------------
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.c" "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS "${SRC_DIR}/*.h" "${SRC_DIR}/*.hpp")

foreach(_rx IN ITEMS
  ".*/Launcher.*\\.cpp$"
  ".*/.*Launcher_.*\\.cpp$"
  ".*/SDL.*Launcher.*\\.cpp$"
  ".*/Win.*Launcher.*\\.cpp$"
  ".*/Main\\.cpp$"
  ".*/main\\.cpp$"
  ".*/colonygame\\.cpp$"
  ".*/win32_main\\.cpp$"
)
  list(FILTER CORE_SOURCES EXCLUDE REGEX "${_rx}")
endforeach()

set(SDL_ONLY_SOURCES "")
foreach(_cand IN ITEMS "src/game/Game.cpp" "src/ui/HudOverlays.cpp")
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
    list(APPEND SDL_ONLY_SOURCES "${CMAKE_SOURCE_DIR}/${_cand}")
  endif()
endforeach()
if(SDL_ONLY_SOURCES)
  list(REMOVE_ITEM CORE_SOURCES ${SDL_ONLY_SOURCES})
endif()

list(FILTER CORE_SOURCES EXCLUDE REGEX ".*[\\/]render[\\/]d3d11_sprite_batch\\.cpp$")
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*[\\\\/](os|platform)[\\\\/]win(32)?[\\\\/](WinBootstrap|SingleInstance|CrashHandler)\\.cpp$")

add_library(colony_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(colony_core PUBLIC "${SRC_DIR}")
target_compile_features(colony_core PUBLIC cxx_std_20)

if(ENABLE_UNITY)
  set_property(TARGET colony_core PROPERTY UNITY_BUILD ON)
  set_property(TARGET colony_core PROPERTY UNITY_BUILD_BATCH_SIZE ${UNITY_BATCH_SIZE_CORE})
endif()

if(NOT PCH_HEADER)
  foreach(_cand IN ITEMS "${SRC_DIR}/pch.h" "${SRC_DIR}/PCH.h" "pch.h" "PCH.h")
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
      set(PCH_HEADER "${_cand}")
      break()
    endif()
  endforeach()
endif()
if(PCH_HEADER AND EXISTS "${PCH_HEADER}")
  target_precompile_headers(colony_core PRIVATE "${PCH_HEADER}")
endif()

target_compile_definitions(colony_core PUBLIC
  $<$<CONFIG:Debug>:CG_DEBUG=1>
  $<$<CONFIG:RelWithDebInfo>:CG_RELWITHDEBINFO=1>
)

if(WIN32)
  target_link_libraries(colony_core PUBLIC winmm)
endif()

# --------------------------------------------------------------------------------------------------
# Generated version header
# --------------------------------------------------------------------------------------------------
set(_ver_in  "${CMAKE_BINARY_DIR}/build_config.h.in")
set(_ver_out "${CMAKE_BINARY_DIR}/generated/build_config.h")
file(WRITE "${_ver_in}" [=[
// Auto-generated at configure-time
#pragma once
#define CG_VERSION           "@PROJECT_VERSION@"
#define CG_FRONTEND          "@CG_FRONTEND@"
#define CG_BUILD_TYPE        "@CMAKE_BUILD_TYPE@"
#define CG_GIT_SHA           "@CG_GIT_SHA@"
#define CG_GIT_IS_DIRTY      @CG_GIT_IS_DIRTY@
#define CG_TOOLCHAIN         "@CMAKE_CXX_COMPILER_ID@ @CMAKE_CXX_COMPILER_VERSION@"
#define CG_BUILD_TIMESTAMP   "@CG_BUILD_TIMESTAMP@"
#define CG_MIN_WINVER        "@CG_MIN_WINVER@"
]=])

set(CG_GIT_SHA "unknown")
set(CG_GIT_IS_DIRTY 0)
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                  OUTPUT_VARIABLE CG_GIT_SHA
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet
                  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                  RESULT_VARIABLE GIT_DIFF_RESULT
                  ERROR_QUIET)
  if(GIT_DIFF_RESULT EQUAL 1)
    set(CG_GIT_IS_DIRTY 1)
  endif()
endif()
string(TIMESTAMP CG_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)
target_include_directories(colony_core PUBLIC "${CMAKE_BINARY_DIR}/generated")

# --------------------------------------------------------------------------------------------------
# Entry discovery (GAME ENTRY vs. BOOTSTRAPPER)
# --------------------------------------------------------------------------------------------------
# Known game entry points (do NOT include WinLauncher.cpp here)
set(_KNOWN_WIN_GAME_ENTRIES
  "src/platform/win32_main.cpp" "platform/win32_main.cpp" "src/platform/win32/win32_main.cpp"
  "colonygame.cpp" "src/colonygame.cpp" "src/platform/win32/colonygame.cpp"
  "src/Launcher_Win32.cpp" "Launcher_Win32.cpp"
)
set(_KNOWN_SDL_LAUNCHERS
  "Launcher_SDL.cpp" "SDLLauncher.cpp" "src/Launcher_SDL.cpp"
  "src/platform/sdl/Launcher_SDL.cpp" "platform/sdl/Launcher_SDL.cpp" "src/SDLLauncher.cpp"
)
# Known bootstrap sources
set(_KNOWN_WIN_BOOTSTRAPPERS
  "WinLauncher.cpp" "src/WinLauncher.cpp"
  "src/platform/win32/WinLauncher.cpp" "platform/win32/WinLauncher.cpp"
)

set(_WIN32_FLAG WIN32)
if(SHOW_CONSOLE)
  set(_WIN32_FLAG)
endif()

# Resolve GAME ENTRY
set(_GAME_ENTRY "")
set(CG_FRONTEND_STR "${FRONTEND}")

# Back-compat: if user passed -DLAUNCHER_FILE=... and it points to WinLauncher.cpp, treat it as BOOTSTRAP_FILE.
if(LAUNCHER_FILE)
  if(EXISTS "${LAUNCHER_FILE}")
    if(LAUNCHER_FILE MATCHES ".*[Ww]in[Ll]auncher.*\\.cpp$")
      if(NOT BOOTSTRAP_FILE)
        set(BOOTSTRAP_FILE "${LAUNCHER_FILE}")
      endif()
    else()
      set(_GAME_ENTRY "${LAUNCHER_FILE}")
    endif()
  else()
    message(FATAL_ERROR "LAUNCHER_FILE does not exist: ${LAUNCHER_FILE}")
  endif()
endif()

if(NOT _GAME_ENTRY)
  if(FRONTEND STREQUAL "win32")
    foreach(_cand IN LISTS _KNOWN_WIN_GAME_ENTRIES)
      if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
        set(_GAME_ENTRY "${_cand}")
        break()
      endif()
    endforeach()
    if(NOT _GAME_ENTRY)
      message(FATAL_ERROR "Could not find a Win32 game entry source (e.g., win32_main.cpp). Override with -DLAUNCHER_FILE=...")
    endif()
  elseif(FRONTEND STREQUAL "sdl")
    foreach(_cand IN LISTS _KNOWN_SDL_LAUNCHERS)
      if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
        set(_GAME_ENTRY "${_cand}")
        break()
      endif()
    endforeach()
    if(NOT _GAME_ENTRY)
      message(FATAL_ERROR "Could not find an SDL game entry source. Override with -DLAUNCHER_FILE=...")
    endif()
  else()
    message(FATAL_ERROR "FRONTEND must be 'win32' or 'sdl'")
  endif()
endif()

# Resolve BOOTSTRAPPER (WinLauncher.cpp)
set(_BOOTSTRAP "")
if(BUILD_BOOTSTRAPPER)
  if(BOOTSTRAP_FILE)
    if(EXISTS "${BOOTSTRAP_FILE}")
      set(_BOOTSTRAP "${BOOTSTRAP_FILE}")
    else()
      message(FATAL_ERROR "BOOTSTRAP_FILE does not exist: ${BOOTSTRAP_FILE}")
    endif()
  endif()
  if(NOT _BOOTSTRAP)
    foreach(_cand IN LISTS _KNOWN_WIN_BOOTSTRAPPERS)
      if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
        set(_BOOTSTRAP "${_cand}")
        break()
      endif()
    endforeach()
  endif()
endif()

set(CG_FRONTEND "${CG_FRONTEND_STR}")
set(CG_MIN_WINVER_TXT "${CG_MIN_WINVER}")
configure_file("${_ver_in}" "${_ver_out}" @ONLY)

# --------------------------------------------------------------------------------------------------
# Game executable (real game entry)
# --------------------------------------------------------------------------------------------------
add_executable(ColonyGame ${_WIN32_FLAG} "${_GAME_ENTRY}")
target_link_libraries(ColonyGame PRIVATE colony_core)
target_compile_features(ColonyGame PRIVATE cxx_std_20)

# PCH for game exe
if(NOT PCH_HEADER_EXE)
  foreach(_cand IN ITEMS "${SRC_DIR}/pch.h" "${SRC_DIR}/PCH.h" "pch.h" "PCH.h")
    if(EXISTS "${CMAKE_SOURCE_DIR}/${_cand}")
      set(PCH_HEADER_EXE "${_cand}")
      break()
    endif()
  endforeach()
endif()
if(PCH_HEADER_EXE AND EXISTS "${PCH_HEADER_EXE}")
  target_precompile_headers(ColonyGame PRIVATE "${PCH_HEADER_EXE}")
endif()

# -------- EnableWinQoL (resources, bootstrap, installs, asset copy) ----------
include(cmake/EnableWinQoL.cmake)

set(CG_APP_VERSION "${PROJECT_VERSION}.0")
# Asset folders to mirror next to the exe
set(_ASSET_DIRS assets Assets data Data res resources)

set(_WQ_OPTS
  COMPANY_NAME     "Colony-Game"
  FILE_DESCRIPTION "Colony-Game"
  ICON             "${CMAKE_SOURCE_DIR}/res/app.ico"
  MANIFEST         "${CMAKE_SOURCE_DIR}/res/app.manifest"
  WORKING_DIR      "$<TARGET_FILE_DIR:ColonyGame>"
  ASSET_DIRS       ${_ASSET_DIRS}
  SKIP_UNITY_FOR_BOOTSTRAP
  SECURITY_HARDENING
  FASTLINK_DEBUG
)
if(WARNINGS_AS_ERRORS)
  list(APPEND _WQ_OPTS WARNINGS_AS_ERRORS)
endif()
if(MSVC_STATIC_RUNTIME)
  list(APPEND _WQ_OPTS USE_STATIC_RUNTIME)
endif()
if(ENABLE_LTO)
  list(APPEND _WQ_OPTS LTO)
endif()
if(ENABLE_ASAN)
  list(APPEND _WQ_OPTS ENABLE_ASAN)
endif()

enable_win_qol(ColonyGame "Colony-Game" "${CG_APP_VERSION}" ${_WQ_OPTS})

# Helpful defines for the game
target_compile_definitions(ColonyGame PRIVATE
  CG_FRONTEND_${FRONTEND}=1
  $<$<BOOL:${SHOW_CONSOLE}>:CG_CONSOLE_APP=1>
)

# Core Win32 libs (EnableWinQoL may link Shell32; we link explicitly to be safe)
# (Merged patch) Ensure Dbghelp & Shell32 are linked explicitly alongside Shlwapi.
target_link_libraries(ColonyGame PRIVATE
  user32 gdi32 shlwapi Dbghelp shell32 comdlg32 comctl32 advapi32
  ole32 oleaut32 imm32 winmm version uuid
  dwmapi xinput9_1_0
)

if(ENABLE_UNITY)
  set_property(TARGET ColonyGame PROPERTY UNITY_BUILD ON)
  set_property(TARGET ColonyGame PROPERTY UNITY_BUILD_BATCH_SIZE ${UNITY_BATCH_SIZE_EXE})
endif()

if(MSVC AND ENABLE_RELEASE_PDB)
  target_compile_options(ColonyGame PRIVATE "$<$<CONFIG:Release>:/Zi>")
  target_link_options(ColonyGame    PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
endif()

if(MSVC AND SUPPRESS_MSVC_C4505)
  target_compile_options(ColonyGame PRIVATE /wd4505)
endif()

# --------------------------------------------------------------------------------------------------
# Bootstrapper executable (PATCH: WinLauncher that spawns ColonyGame.exe)
# --------------------------------------------------------------------------------------------------
if(BUILD_BOOTSTRAPPER AND _BOOTSTRAP)
  add_executable(WinLauncher WIN32 "${_BOOTSTRAP}")
  set_property(TARGET WinLauncher PROPERTY FOLDER "Bootstrap")
  target_compile_features(WinLauncher PRIVATE cxx_std_20)
  # UNICODE and lean headers already set globally; keep explicit links needed by the launcher
  target_link_libraries(WinLauncher PRIVATE Shell32 Ole32 User32)

  # Give the launcher the same QoL (icons, manifest, asset copy into the same out dir)
  # It shares the same output dir as ColonyGame by our global RUNTIME_OUTPUT_DIRECTORY.
  enable_win_qol(WinLauncher "Colony-Game Launcher" "${CG_APP_VERSION}" ${_WQ_OPTS})

  if(MSVC AND ENABLE_RELEASE_PDB)
    target_compile_options(WinLauncher PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(WinLauncher    PRIVATE "$<$<CONFIG:Release>:/DEBUG:FULL>")
  endif()

  if(MSVC AND SUPPRESS_MSVC_C4505)
    target_compile_options(WinLauncher PRIVATE /wd4505)
  endif()
endif()

# Visual Studio debugger settings and startup project
if(MSVC)
  # Ensure PATH includes the binary dir at debug-time
  set(_vs_env "PATH=%PATH%;$<TARGET_FILE_DIR:ColonyGame>")
  set_property(TARGET ColonyGame PROPERTY VS_DEBUGGER_ENVIRONMENT "${_vs_env}")
  set_property(TARGET ColonyGame PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ColonyGame>")
  if(TARGET WinLauncher)
    set_property(TARGET WinLauncher PROPERTY VS_DEBUGGER_ENVIRONMENT "${_vs_env}")
    set_property(TARGET WinLauncher PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:WinLauncher>")
  endif()
endif()

# Choose startup project (prefer launcher if built)
if(TARGET WinLauncher)
  set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT WinLauncher)
else()
  set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ColonyGame)
endif()

# --------------------------------------------------------------------------------------------------
# SDL2 integration (FRONTEND=sdl)
# --------------------------------------------------------------------------------------------------
if(FRONTEND STREQUAL "sdl")
  if(SDL_ONLY_SOURCES)
    target_sources(colony_core PRIVATE ${SDL_ONLY_SOURCES})
  endif()

  set(_SDL2_FOUND_KIND "none")
  find_package(SDL2 CONFIG QUIET)
  if(SDL2_FOUND)
    set(_SDL2_FOUND_KIND "config")
  else()
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
      set(_SDL2_FOUND_KIND "module")
    endif()
  endif()

  if(NOT SDL2_FOUND)
    message(FATAL_ERROR "FRONTEND=sdl selected but SDL2 was not found.")
  endif()
  message(STATUS "SDL2 found via: ${_SDL2_FOUND_KIND}")

  set(_SDL_TARGET "")
  if(TARGET SDL2::SDL2)
    set(_SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2::SDL2-static)
    set(_SDL_TARGET SDL2::SDL2-static)
  endif()

  if(_SDL_TARGET)
    target_link_libraries(colony_core PUBLIC ${_SDL_TARGET})
    target_link_libraries(ColonyGame PRIVATE ${_SDL_TARGET})
  else()
    if(SDL2_INCLUDE_DIRS)
      target_include_directories(colony_core PUBLIC ${SDL2_INCLUDE_DIRS})
    endif()
    if(SDL2_LIBRARIES)
      target_link_libraries(ColonyGame PRIVATE ${SDL2_LIBRARIES})
    endif()
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

  if(TARGET SDL2::SDL2main)
    target_link_libraries(ColonyGame PRIVATE SDL2::SDL2main)
  elseif(DEFINED SDL2MAIN_LIBRARY AND SDL2MAIN_LIBRARY)
    target_link_libraries(ColonyGame PRIVATE "${SDL2MAIN_LIBRARY}")
  endif()

  target_compile_definitions(colony_core PUBLIC CG_USE_SDL=1)
endif()

# --------------------------------------------------------------------------------------------------
# Source grouping
# --------------------------------------------------------------------------------------------------
get_target_property(_exe_sources ColonyGame SOURCES)
get_target_property(_core_sources colony_core SOURCES)
if(TARGET WinLauncher)
  get_target_property(_launcher_sources WinLauncher SOURCES)
endif()
if(_exe_sources OR _core_sources OR _launcher_sources)
  source_group(TREE "${CMAKE_SOURCE_DIR}" FILES ${_exe_sources} ${_core_sources} ${_launcher_sources} ${CORE_HEADERS})
endif()
source_group("Generated" FILES "${_ver_out}")
target_include_directories(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated")
if(TARGET WinLauncher)
  target_include_directories(WinLauncher PRIVATE "${CMAKE_BINARY_DIR}/generated")
endif()

# --------------------------------------------------------------------------------------------------
# (Merged patch) Enforce MSVC runtime choice on all targets post-definition
# --------------------------------------------------------------------------------------------------
if(MSVC)
  function(_cg_collect_targets dir out_var)
    get_property(_tgts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(sd IN LISTS _subs)
      _cg_collect_targets("${sd}" _sub_tgts)
      list(APPEND _tgts ${_sub_tgts})
    endforeach()
    set(${out_var} "${_tgts}" PARENT_SCOPE)
  endfunction()

  _cg_collect_targets("${CMAKE_CURRENT_BINARY_DIR}" _ALL_TGTS)

  foreach(_t IN LISTS _ALL_TGTS)
    # Only meaningful for real build targets
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "STATIC_LIBRARY" OR
       _type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
      if(MSVC_STATIC_RUNTIME)
        # /MT or /MTd
        set_property(TARGET ${_t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      else()
        # /MD or /MDd
        set_property(TARGET ${_t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
      endif()
    endif()
  endforeach()
endif()

# ==================================================================================================
# Staging & packaging (installs handled by EnableWinQoL for the executable)
# ==================================================================================================

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/stage" CACHE PATH "Install prefix (staging root)" FORCE)
include(GNUInstallDirs)

# Install assets (EnableWinQoL copies post-build too; this covers install prefix)
foreach(_d IN LISTS _ASSET_DIRS)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${_d}")
    install(DIRECTORY "${_d}/" DESTINATION "${_d}" COMPONENT ColonyRuntime)
  endif()
endforeach()

# Optional: install redists (UCRT/MSVC) app-local
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
include(InstallRequiredSystemLibraries)
if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
  install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
          DESTINATION .
          COMPONENT ColonyRuntime)
endif()

# Optional: sign the installed executables (Release only; requires signtool + pfx)
if(ENABLE_CODE_SIGNING AND CMAKE_BUILD_TYPE MATCHES "Release")
  if(NOT SIGNTOOL_PATH)
    find_program(SIGNTOOL_PATH NAMES signtool signtool.exe)
  endif()
  if(SIGNTOOL_PATH AND EXISTS "${SIGNING_CERT_PATH}")
    # Sign ColonyGame.exe
    install(CODE
"execute_process(COMMAND \"${SIGNTOOL_PATH}\"
    sign /fd SHA256 /tr \"${SIGNING_TIMESTAMP_URL}\" /td SHA256
    /f \"${SIGNING_CERT_PATH}\" /p \"${SIGNING_CERT_PASSWORD}\"
    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/$<CONFIG>/ColonyGame.exe\")
 message(STATUS \"Signed: \${CMAKE_INSTALL_PREFIX}/$<CONFIG>/ColonyGame.exe\")")
    # Sign WinLauncher.exe if present
    if(TARGET WinLauncher)
      install(CODE
"execute_process(COMMAND \"${SIGNTOOL_PATH}\"
    sign /fd SHA256 /tr \"${SIGNING_TIMESTAMP_URL}\" /td SHA256
    /f \"${SIGNING_CERT_PATH}\" /p \"${SIGNING_CERT_PASSWORD}\"
    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/$<CONFIG>/WinLauncher.exe\")
 message(STATUS \"Signed: \${CMAKE_INSTALL_PREFIX}/$<CONFIG>/WinLauncher.exe\")")
    endif()
  else()
    message(WARNING "ENABLE_CODE_SIGNING=ON but signtool or cert not configured; skipping.")
  endif()
endif()

# Stage target
if(TARGET WinLauncher)
  set(_stage_deps ColonyGame WinLauncher)
else()
  set(_stage_deps ColonyGame)
endif()
add_custom_target(stage_win ALL
  COMMAND "${CMAKE_COMMAND}" -E echo "Staging ColonyGame to: ${CMAKE_INSTALL_PREFIX}/$<CONFIG>"
  COMMAND "${CMAKE_COMMAND}" -DCMAKE_INSTALL_CONFIG_NAME=$<CONFIG> -P "${CMAKE_BINARY_DIR}/cmake_install.cmake"
  DEPENDS ${_stage_deps}
  VERBATIM
)

# Zip packaging
set(CPACK_PACKAGE_NAME "ColonyGame")
set(CPACK_PACKAGE_VENDOR "Colony-Game")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CG_FRONTEND}-${CG_GIT_SHA}-$<CONFIG>")
install(CODE "message(STATUS \"Staged at: ${CMAKE_INSTALL_PREFIX}/$<CONFIG>\")")
include(CPack)

# --------------------------------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "================ Colony-Game: Configure Summary ================")
message(STATUS "  Generator            : ${CMAKE_GENERATOR}")
message(STATUS "  Build type           : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Frontend             : ${FRONTEND}")
message(STATUS "  Game entry           : ${_GAME_ENTRY}")
if(TARGET WinLauncher)
  message(STATUS "  Bootstrapper         : ${_BOOTSTRAP}")
else()
  message(STATUS "  Bootstrapper         : (disabled or not found)")
endif()
message(STATUS "  Show console         : ${SHOW_CONSOLE}")
message(STATUS "  Min WinVer           : ${CG_MIN_WINVER}")
message(STATUS "  MSVC static runtime  : ${MSVC_STATIC_RUNTIME}")
message(STATUS "  Warnings as errors   : ${WARNINGS_AS_ERRORS}")
message(STATUS "  Unity builds         : ${ENABLE_UNITY} (core=${UNITY_BATCH_SIZE_CORE}, exe=${UNITY_BATCH_SIZE_EXE})")
message(STATUS "  LTO/IPO              : ${ENABLE_LTO}")
message(STATUS "  ASAN                 : ${ENABLE_ASAN}")
message(STATUS "  MSVC /analyze        : ${ENABLE_MSVC_ANALYZE}")
message(STATUS "  clang-tidy           : ${ENABLE_CLANG_TIDY}")
message(STATUS "  Compiler cache       : ${ENABLE_COMPILER_CACHE}")
message(STATUS "  Release PDB          : ${ENABLE_RELEASE_PDB}")
message(STATUS "  Git SHA              : ${CG_GIT_SHA} (dirty=${CG_GIT_IS_DIRTY})")
message(STATUS "  Build timestamp      : ${CG_BUILD_TIMESTAMP}")
message(STATUS "  Source dir           : ${CMAKE_SOURCE_DIR}")
message(STATUS "  Binary dir           : ${CMAKE_BINARY_DIR}")
message(STATUS "  Install prefix       : ${CMAKE_INSTALL_PREFIX}")
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
#   - Show console window (affects the game exe):
#       cmake -S . -B build -DFRONTEND=win32 -DSHOW_CONSOLE=ON
#
#   - Build SDL frontend (requires SDL2 in path or via vcpkg):
#       cmake -S . -B build -DFRONTEND=sdl
#
#   - Pick a specific GAME ENTRY file (e.g., win32_main.cpp):
#       cmake -S . -B build -DLAUNCHER_FILE=src/platform/win32/win32_main.cpp
#
#   - Override the BOOTSTRAPPER source (WinLauncher.cpp) or disable it:
#       cmake -S . -B build -DBOOTSTRAP_FILE=WinLauncher.cpp
#       cmake -S . -B build -DBUILD_BOOTSTRAPPER=OFF
#
#   - Static MSVC runtime (/MT):
#       cmake -S . -B build -DMSVC_STATIC_RUNTIME=ON
#
#   - Treat warnings as errors:
#       cmake -S . -B build -DWARNINGS_AS_ERRORS=ON
#
#   - Stage into build/stage/<cfg> automatically (enabled by default via 'stage_win' target),
#     or explicitly:
#       cmake --install build --config Release
# --------------------------------------------------------------------------------------------------

