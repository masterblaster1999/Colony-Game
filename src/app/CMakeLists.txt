# ==========================
# File: src/app/CMakeLists.txt
# ==========================
cmake_minimum_required(VERSION 3.27...3.31 FATAL_ERROR)

include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR "src/app is Windows-only in this repository.")
endif()

# Standalone convenience (rare).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ColonyGame LANGUAGES CXX)
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(_APP_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ---- Target -------------------------------------------------------------------
if(NOT TARGET ColonyGame)
  add_executable(ColonyGame)
endif()

target_compile_features(ColonyGame PRIVATE cxx_std_23)

target_include_directories(ColonyGame
  PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
    "${_APP_DIR}"
)

# Visual Studio: put it under a nice folder.
set_target_properties(ColonyGame PROPERTIES FOLDER "colony/apps")

# ---- App sources (non-entrypoint) ---------------------------------------------
file(GLOB_RECURSE _APP_CPP CONFIGURE_DEPENDS
  "${_APP_DIR}/*.cpp"
  "${_APP_DIR}/*.c"
)
file(GLOB_RECURSE _APP_HDR CONFIGURE_DEPENDS
  "${_APP_DIR}/*.hpp"
  "${_APP_DIR}/*.h"
)

# Normalize and filter out entrypoints from the general glob.
set(_APP_SOURCES "")
foreach(_f IN LISTS _APP_CPP _APP_HDR)
  file(TO_CMAKE_PATH "${_f}" _n)
  list(APPEND _APP_SOURCES "${_n}")
endforeach()
list(REMOVE_DUPLICATES _APP_SOURCES)

unset(_APP_CPP)
unset(_APP_HDR)

# Do NOT auto-compile entrypoints from the glob; we add exactly ONE explicitly below.
set(_ENTRYPOINT_EXCLUDE "(^|/)(EntryWinMain|WinMain|EntryPoint|AppMain|main)\\.(c|cpp)$")
list(FILTER _APP_SOURCES EXCLUDE REGEX "${_ENTRYPOINT_EXCLUDE}")

target_sources(ColonyGame PRIVATE ${_APP_SOURCES})

# ---- Entrypoint (exactly one) -------------------------------------------------
# Prefer src/app/EntryWinMain.cpp. Add more fallbacks if your tree uses different names.
set(_ENTRY_CANDIDATES
  "${_APP_DIR}/EntryWinMain.cpp"
  "${_APP_DIR}/WinMain.cpp"
  "${CMAKE_SOURCE_DIR}/src/app/EntryWinMain.cpp"
  "${CMAKE_SOURCE_DIR}/src/EntryWinMain.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/win/EntryWinMain.cpp"
)
set(_ENTRY_ADDED OFF)
foreach(_cand IN LISTS _ENTRY_CANDIDATES)
  if(EXISTS "${_cand}")
    target_sources(ColonyGame PRIVATE "${_cand}")
    # Entry points should never be merged into unity buckets.
    set_source_files_properties("${_cand}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    set(_ENTRY_ADDED ON)
    break()
  endif()
endforeach()
if(NOT _ENTRY_ADDED)
  message(FATAL_ERROR
    "No Windows entrypoint found. Provide src/app/EntryWinMain.cpp (preferred) "
    "or one of the fallback entrypoint filenames."
  )
endif()

# ---- App bootstrap (compile into EXE, not into libraries) ---------------------
# We try a few common locations so you can migrate files without breaking builds.
set(_BOOTSTRAP_CANDIDATES
  "${_APP_DIR}/Bootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/src/Bootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/win/Bootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/platform/win/Bootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/src/platform/win/WinBootstrap.cpp"
  "${CMAKE_SOURCE_DIR}/platform/win/WinBootstrap.cpp"
)
foreach(_bs IN LISTS _BOOTSTRAP_CANDIDATES)
  if(EXISTS "${_bs}")
    target_sources(ColonyGame PRIVATE "${_bs}")
    set_source_files_properties("${_bs}" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    break()
  endif()
endforeach()

# ---- Windows EXE / console toggle ---------------------------------------------
# Many repos define SHOW_CONSOLE at top-level; if not, default to OFF.
if(NOT DEFINED SHOW_CONSOLE)
  set(SHOW_CONSOLE OFF)
endif()
if(WIN32 AND NOT SHOW_CONSOLE)
  set_target_properties(ColonyGame PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ---- Unity build (opt-in) -----------------------------------------------------
if(DEFINED COLONY_UNITY_BUILD AND COLONY_UNITY_BUILD)
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)
else()
  set_target_properties(ColonyGame PROPERTIES UNITY_BUILD OFF)
endif()

# ---- Link internal libs (best-effort; some are linked later by CGGameTarget) ---
if(TARGET colony_core)
  target_link_libraries(ColonyGame PRIVATE colony_core)
endif()
if(TARGET colony_platform_win)
  target_link_libraries(ColonyGame PRIVATE colony_platform_win)
endif()

# ---- Resources / generated files ----------------------------------------------
if(EXISTS "${CMAKE_BINARY_DIR}/generated/Version.rc")
  target_sources(ColonyGame PRIVATE "${CMAKE_BINARY_DIR}/generated/Version.rc")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
  target_sources(ColonyGame PRIVATE "${CMAKE_SOURCE_DIR}/platform/win/Manifest.rc")
endif()

# ---- Make running from Visual Studio work (assets/res staged next to EXE) -----
# This mirrors the behavior in many single-exe workflows, but is guarded.
set(_RUNTIME_DIR "$<TARGET_FILE_DIR:ColonyGame>")

if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "${_RUNTIME_DIR}/assets"
    VERBATIM
  )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/res")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/res" "${_RUNTIME_DIR}/res"
    VERBATIM
  )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/Content")
  add_custom_command(TARGET ColonyGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/Content"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/Content" "${_RUNTIME_DIR}/Content"
    COMMENT "Copy Content/ next to the EXE for streaming"
    VERBATIM
  )
endif()

# ---- Repo helper functions (safe if missing) ----------------------------------
if(COMMAND colony_apply_msvc_runtime)
  colony_apply_msvc_runtime(ColonyGame)
endif()
if(COMMAND colony_apply_debug_runtime_checks)
  colony_apply_debug_runtime_checks(ColonyGame)
endif()

# VS debugger working directory (nice default)
if(MSVC)
  set_target_properties(ColonyGame PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# IDE grouping
if(_APP_SOURCES)
  source_group(TREE "${CMAKE_SOURCE_DIR}/src" PREFIX "App" FILES ${_APP_SOURCES})
endif()

unset(_APP_DIR)
unset(_APP_SOURCES)
unset(_ENTRYPOINT_EXCLUDE)
unset(_ENTRY_CANDIDATES)
unset(_ENTRY_ADDED)
unset(_BOOTSTRAP_CANDIDATES)
unset(_RUNTIME_DIR)
