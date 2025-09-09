cmake_minimum_required(VERSION 3.20)
project(ColonyGame LANGUAGES CXX)

# --- Language standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Sources
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Exclude launcher stubs if present at repo root
list(FILTER GAME_SOURCES EXCLUDE REGEX "/SingleClick\\.cpp$")
list(FILTER GAME_SOURCES EXCLUDE REGEX "/WinLauncher\\.cpp$")

add_executable(ColonyGame ${GAME_SOURCES})

# --- Output dirs (so the exe is easy to find)
if (MSVC)
  set_target_properties(ColonyGame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Debug"
  )
else()
  set_target_properties(ColonyGame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
endif()

# --- Windows: defuse min/max macros *globally*
if (WIN32)
  target_compile_definitions(ColonyGame PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  # Force-include our compatibility header first in every TU
  if (MSVC)
    target_compile_options(ColonyGame PRIVATE /FI"${CMAKE_SOURCE_DIR}/src/Compat.h")
  else()
    target_compile_options(ColonyGame PRIVATE -include "${CMAKE_SOURCE_DIR}/src/Compat.h")
  endif()
endif()

# --- SDL2 (optional)
option(BUILD_WITH_SDL2 "Build SDL-based sources if SDL2 is available" ON)

set(SDL2_WIRED FALSE)
if (BUILD_WITH_SDL2)
  # vcpkg/Config mode (MSVC or vcpkg-enabled MinGW)
  find_package(SDL2 CONFIG QUIET)
  if (SDL2_FOUND)
    target_link_libraries(ColonyGame PRIVATE SDL2::SDL2 SDL2::SDL2main)
    target_compile_definitions(ColonyGame PRIVATE HAS_SDL2=1)
    set(SDL2_WIRED TRUE)
  else()
    # pkg-config (Unix/MinGW)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
      pkg_check_modules(SDL2_PKG sdl2)
      if (SDL2_PKG_FOUND)
        target_include_directories(ColonyGame PRIVATE ${SDL2_PKG_INCLUDE_DIRS})
        target_link_directories(ColonyGame PRIVATE ${SDL2_PKG_LIBRARY_DIRS})
        target_link_libraries(ColonyGame PRIVATE ${SDL2_PKG_LIBRARIES})
        target_compile_definitions(ColonyGame PRIVATE HAS_SDL2=1)
        set(SDL2_WIRED TRUE)
      endif()
    endif()
  endif()

  if (NOT SDL2_WIRED)
    message(WARNING "SDL2 not found -> excluding SDL-dependent sources.")
    # Known SDL-dependent files in your tree:
    list(FILTER GAME_SOURCES EXCLUDE REGEX "/Launcher_SDL\\.cpp$")
    list(FILTER GAME_SOURCES EXCLUDE REGEX "/game/Game\\.cpp$")
    set_property(TARGET ColonyGame PROPERTY SOURCES "${GAME_SOURCES}")
  endif()
endif()

# --- Warnings & conformance
if (MSVC)
  target_compile_options(ColonyGame PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
else()
  target_compile_options(ColonyGame PRIVATE -Wall -Wextra -Wpedantic)
endif()
