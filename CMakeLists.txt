cmake_minimum_required(VERSION 3.24)
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

# ------------- Testing
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

# ------------- Helpful tips (fixed bracket block)
include(cmake/tips.cmake)
