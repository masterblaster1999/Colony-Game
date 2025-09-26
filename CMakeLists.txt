cmake_minimum_required(VERSION 3.24)

# ---------------- Policies & developer UX ----------------
# Honor CMAKE_MSVC_RUNTIME_LIBRARY for MSVC (runtime selection via CMake).
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# Generate compile_commands.json for better tooling/IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- vcpkg auto-wire (before project()) ----------------
# If no toolchain is explicitly provided, pick up vcpkg from VCPKG_ROOT.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "vcpkg toolchain file")
    message(STATUS "Using vcpkg toolchain at: ${CMAKE_TOOLCHAIN_FILE}")
  endif()
endif()

project(ColonyGame LANGUAGES CXX)

# ------------- Options (Windows-first)
option(ENABLE_IMGUI         "Enable Dear ImGui overlay" ON)
option(ENABLE_TRACY         "Enable Tracy profiler"     ON)
option(TRACY_FETCH          "FetchContent Tracy"        ON)
option(SHOW_CONSOLE         "Show Console for WIN32 exe" OFF)
option(BUILD_BOOTSTRAPPER   "Build WinLauncher bootstrapper" ON)
set(FRONTEND "win32" CACHE STRING "Frontend to build: win32 or sdl")
set_property(CACHE FRONTEND PROPERTY STRINGS win32 sdl)

# Prefer static MSVC runtime with a single CMake knob
option(MSVC_STATIC_RUNTIME "Link /MT or /MTd" OFF)

# ---------------- Build info (Git hash / timestamp / version) ----------------
# Capture short Git hash; fall back to "unknown" if not a git workspace.
execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  OUTPUT_VARIABLE CG_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(CG_GIT_HASH STREQUAL "")
  set(CG_GIT_HASH "unknown")
endif()

string(TIMESTAMP CG_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# Semantic version (bump as needed)
set(CG_VERSION_MAJOR 0)
set(CG_VERSION_MINOR 1)
set(CG_VERSION_PATCH 0)
set(CG_VERSION_STR "${CG_VERSION_MAJOR}.${CG_VERSION_MINOR}.${CG_VERSION_PATCH}")

# Configure generated headers/resources used by targets (if templates exist)
configure_file("${CMAKE_SOURCE_DIR}/src/build_info.h.in"
               "${CMAKE_BINARY_DIR}/generated/build_info.h" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/platform/win/Version.rc.in"
               "${CMAKE_BINARY_DIR}/generated/Version.rc" @ONLY)

# ------------- Toolchain & compiler flags
include(cmake/msvc_flags.cmake)

# ------------- Dependencies (vcpkg-friendly)
include(cmake/deps.cmake)           # SDL2 (if FRONTEND=sdl), imgui wiring, Tracy, etc.

# ------------- Shaders (DXC/FXC fallback)
include(cmake/shaders.cmake)

# ------------- Sources & targets
add_subdirectory(src)

# ------------- Install/Stage
include(cmake/install_stage.cmake)

# ------------- Testing (robust when tests/ isn't present)
include(CTest)
if(BUILD_TESTING)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
  else()
    message(STATUS "BUILD_TESTING=ON but no 'tests/' directory. Skipping tests.")
  endif()
endif()

# ------------- Packaging (ZIP + MSVC runtime redist)
# Includes VC++ redistributable DLLs when needed and creates a ZIP package target.
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_NAME "ColonyGame")
set(CPACK_PACKAGE_VERSION "${CG_VERSION_STR}")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "ColonyGame-${CPACK_PACKAGE_VERSION}-win64")
include(CPack)

# ------------- Helpful tips (fixed bracket block)
include(cmake/tips.cmake)
