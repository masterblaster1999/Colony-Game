# src/CMakeLists.txt
# Target: ColonyGame (Windows-only game executable)
# IMPORTANT: This file should ONLY define the EXE (no monolithic core build here).

if(NOT WIN32)
  return()
endif()

# -----------------------------------------
# Collect game/app sources from src/
# -----------------------------------------
set(COLONY_GAME_SOURCES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/src")
  file(GLOB_RECURSE COLONY_GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.cc"
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.cxx"
  )
endif()

# Exclude module-owned code if it sits under src/
# (these should be built as their own libraries and linked)
if(COLONY_GAME_SOURCES)
  list(FILTER COLONY_GAME_SOURCES EXCLUDE REGEX "/src/(core|engine|renderer|procgen|platform)/")
endif()

# Exclude other standalone apps/tools (do NOT compile into the game exe)
# This prevents accidental inclusion of map viewer + tool entrypoints.
if(COLONY_GAME_SOURCES)
  list(FILTER COLONY_GAME_SOURCES EXCLUDE REGEX "/src/(tools|tool|map_viewer|launcher)/")
endif()

# -----------------------------------------
# Entry TU selection (WinMain)
# Prefer repo-root WinLauncher.cpp if present.
# -----------------------------------------
set(_cg_entry "")
if(EXISTS "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
  set(_cg_entry "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/win/WinLauncher.cpp")
  set(_cg_entry "${CMAKE_SOURCE_DIR}/src/win/WinLauncher.cpp")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/WinLauncher.cpp")
  set(_cg_entry "${CMAKE_SOURCE_DIR}/src/WinLauncher.cpp")
endif()

# Remove any WinLauncher.cpp variants found under src/ so we compile exactly ONE.
if(COLONY_GAME_SOURCES)
  list(FILTER COLONY_GAME_SOURCES EXCLUDE REGEX "/WinLauncher\\.(c|cc|cpp|cxx)$")
endif()
if(_cg_entry)
  list(APPEND COLONY_GAME_SOURCES "${_cg_entry}")
endif()

list(REMOVE_DUPLICATES COLONY_GAME_SOURCES)

if(NOT COLONY_GAME_SOURCES)
  message(FATAL_ERROR
    "ColonyGame: no sources found.\n"
    "Ensure src/ contains your game-level code and an entry TU (WinLauncher.cpp)."
  )
endif()

# -----------------------------------------
# Define executable
# -----------------------------------------
add_executable(ColonyGame WIN32 ${COLONY_GAME_SOURCES})
set_target_properties(ColonyGame PROPERTIES FOLDER "apps")
target_compile_features(ColonyGame PRIVATE cxx_std_23)

if(MSVC)
  target_compile_definitions(ColonyGame PRIVATE UNICODE _UNICODE)
endif()

target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/src"
)

# -----------------------------------------
# Fix MSB8027 object overwrites in Visual Studio
# MSBuild's recommended resolution is:
# Object File Name = $(IntDir)%(RelativeDir)
# -----------------------------------------
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
  # Best solution: per-file MSBuild metadata via VS_SETTINGS (CMake >= 3.22 for built sources).
  if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.22")
    foreach(_cg_src IN LISTS COLONY_GAME_SOURCES)
      if(_cg_src MATCHES "\\.(c|cc|cpp|cxx)$")
        # VS_SETTINGS adds MSBuild item metadata. (CMake doc: VS_SETTINGS) :contentReference[oaicite:2]{index=2}
        # MSBuild doc recommends $(IntDir)%(RelativeDir) for MSB8027. :contentReference[oaicite:3]{index=3}
        set_property(SOURCE "${_cg_src}" PROPERTY VS_SETTINGS
          "ObjectFileName=$(IntDir)%(RelativeDir)"
        )
      endif()
    endforeach()
  else()
    # Fallback for older CMake: push the same /Fo value as a compile option (MSBuild expands macros).
    target_compile_options(ColonyGame PRIVATE "/Fo$(IntDir)%(RelativeDir)")
  endif()
endif()

# -----------------------------------------
# Link required libraries (resilient to alias naming)
# -----------------------------------------
# Engine
if(TARGET colony::engine)
  target_link_libraries(ColonyGame PRIVATE colony::engine)
elseif(TARGET colony_engine)
  target_link_libraries(ColonyGame PRIVATE colony_engine)
endif()

# Renderer
if(TARGET colony::renderer)
  target_link_libraries(ColonyGame PRIVATE colony::renderer)
elseif(TARGET colony_renderer)
  target_link_libraries(ColonyGame PRIVATE colony_renderer)
endif()

# Nav
if(TARGET colony::nav)
  target_link_libraries(ColonyGame PRIVATE colony::nav)
elseif(TARGET colony::path)
  target_link_libraries(ColonyGame PRIVATE colony::path)
elseif(TARGET colony_nav)
  target_link_libraries(ColonyGame PRIVATE colony_nav)
endif()

# Platform (support multiple historical names)
if(TARGET colony::platform_win)
  target_link_libraries(ColonyGame PRIVATE colony::platform_win)
elseif(TARGET Colony::PlatformWin)
  target_link_libraries(ColonyGame PRIVATE Colony::PlatformWin)
elseif(TARGET colony_platform_win)
  target_link_libraries(ColonyGame PRIVATE colony_platform_win)
elseif(TARGET win_platform)
  target_link_libraries(ColonyGame PRIVATE win_platform)
endif()

# Core
if(TARGET colony::core)
  target_link_libraries(ColonyGame PRIVATE colony::core)
elseif(TARGET colony_core)
  target_link_libraries(ColonyGame PRIVATE colony_core)
endif()

# -----------------------------------------
# PCH (make header path absolute to avoid src/src/pch.hpp)
# -----------------------------------------
if(COLONY_USE_PCH)
  set(_cg_pch "")

  # Prefer an absolute COLONY_PCH_HEADER if provided.
  if(DEFINED COLONY_PCH_HEADER AND NOT "${COLONY_PCH_HEADER}" STREQUAL "")
    if(IS_ABSOLUTE "${COLONY_PCH_HEADER}")
      set(_cg_pch "${COLONY_PCH_HEADER}")
    else()
      set(_cg_pch "${CMAKE_SOURCE_DIR}/${COLONY_PCH_HEADER}")
    endif()
  endif()

  # Fallback: src/pch.hpp (common layout)
  if(NOT _cg_pch OR NOT EXISTS "${_cg_pch}")
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/pch.hpp")
      set(_cg_pch "${CMAKE_SOURCE_DIR}/src/pch.hpp")
    endif()
  endif()

  if(_cg_pch AND EXISTS "${_cg_pch}")
    target_precompile_headers(ColonyGame PRIVATE "${_cg_pch}")
  else()
    message(STATUS
      "ColonyGame: COLONY_USE_PCH is ON, but no valid PCH header was found. "
      "Skipping PCH for this target."
    )
  endif()
endif()

# -----------------------------------------
# Optional repo helper hooks
# -----------------------------------------
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(ColonyGame)
endif()

if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(ColonyGame)
endif()

unset(_cg_entry)
unset(_cg_pch)
