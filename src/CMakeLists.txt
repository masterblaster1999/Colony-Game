# src/CMakeLists.txt
# Target: ColonyGame (game executable)
# IMPORTANT: this file should now ONLY define the EXE (no more monolithic "colony_core" build here).

if(NOT WIN32)
  return()
endif()

set(COLONY_GAME_SOURCES "")
if(EXISTS "${CMAKE_SOURCE_DIR}/src")
  file(GLOB_RECURSE COLONY_GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
  )
endif()

# Exclude module-owned code if it sits under src/
# (core, engine, renderer, procgen, platform)
if(COLONY_GAME_SOURCES)
  list(FILTER COLONY_GAME_SOURCES EXCLUDE REGEX "/src/(core|engine|renderer|procgen|platform)/")
endif()

# Prefer WinLauncher.cpp if you have it at repo root (or adjust to your actual entry TU).
if(EXISTS "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
  list(APPEND COLONY_GAME_SOURCES "${CMAKE_SOURCE_DIR}/WinLauncher.cpp")
endif()

list(REMOVE_DUPLICATES COLONY_GAME_SOURCES)

if(NOT COLONY_GAME_SOURCES)
  message(FATAL_ERROR "ColonyGame: no sources found. Ensure src/ contains your game entry and game-level code.")
endif()

add_executable(ColonyGame WIN32 ${COLONY_GAME_SOURCES})
set_target_properties(ColonyGame PROPERTIES FOLDER "apps")

if(MSVC)
  target_compile_definitions(ColonyGame PRIVATE UNICODE _UNICODE)
endif()

target_include_directories(ColonyGame PRIVATE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(ColonyGame PRIVATE
  colony::engine
  colony::renderer
  colony::nav
  colony::platform_win
  colony::core
)

if(COLONY_USE_PCH)
  target_precompile_headers(ColonyGame PRIVATE "${COLONY_PCH_HEADER}")
endif()

colony_apply_msvc_runtime(ColonyGame)
colony_apply_debug_runtime_checks(ColonyGame)
