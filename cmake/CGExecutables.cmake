# cmake/CGExecutables.cmake
#
# Owns ColonyGame + ColonyLauncher executable target setup:
# - WIN32_EXECUTABLE toggling and include dirs
# - link to colony_core + colony_build_options
# - attach EXE-owned sources via CGSources.cmake helpers
# - toolchain defaults (warnings/sanitizers/link /WX) via CGToolchainWin.cmake
#
# (VS startup project and solution folders moved to CGProjectDefaults.cmake.)

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CGSources.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CGToolchainWin.cmake")

# ------------------------------------------------------------------------------
# Internal helpers
# ------------------------------------------------------------------------------

# Link whichever "Windows platform library" target exists in the current tree.
# Supports both the new canonical name (colony::platform_win) and older aliases.
function(_cg_link_platform_win_if_present tgt)
  if(TARGET colony::platform_win)
    target_link_libraries("${tgt}" PRIVATE colony::platform_win)
  elseif(TARGET colony_platform_win)
    target_link_libraries("${tgt}" PRIVATE colony_platform_win)
  endif()

  # Back-compat / transitional names (if they exist in your build graph)
  if(TARGET Colony::PlatformWin)
    target_link_libraries("${tgt}" PRIVATE Colony::PlatformWin)
  endif()
  if(TARGET Colony::WinPlatform)
    target_link_libraries("${tgt}" PRIVATE Colony::WinPlatform)
  endif()
endfunction()

# Apply per-source compile properties to CrashDumpWin.cpp *inside an EXE target*:
# - /EHa only for CrashDumpWin.cpp (needed if it uses SEH-aware catch paths)
# - Disable PCH for this file (prevents MSVC C4652 / PCH EH-mode mismatch noise)
# - Ensure it never participates in unity builds
function(_cg_apply_seh_and_pch_exclusions_for_crashdump tgt)
  if(NOT (MSVC AND TARGET "${tgt}"))
    return()
  endif()

  get_target_property(_srcs "${tgt}" SOURCES)
  if(NOT _srcs)
    return()
  endif()

  # For relative paths, resolve using the directory that defined the target.
  get_target_property(_tgt_src_dir "${tgt}" SOURCE_DIR)
  if(NOT _tgt_src_dir)
    set(_tgt_src_dir "${CMAKE_SOURCE_DIR}")
  endif()

  foreach(_s IN LISTS _srcs)
    # Resolve to an absolute path for robust set_source_files_properties behavior.
    if(IS_ABSOLUTE "${_s}")
      set(_abs "${_s}")
    else()
      set(_abs "${_tgt_src_dir}/${_s}")
    endif()

    file(TO_CMAKE_PATH "${_abs}" _abs_norm)

    if(_abs_norm MATCHES "CrashDumpWin\\.cpp$")
      set_source_files_properties("${_abs}" PROPERTIES
        COMPILE_OPTIONS            "/EHa"
        SKIP_PRECOMPILE_HEADERS    ON
        SKIP_UNITY_BUILD_INCLUSION ON
      )
    endif()

    unset(_abs)
    unset(_abs_norm)
  endforeach()

  unset(_srcs)
  unset(_tgt_src_dir)
endfunction()

function(_cg_apply_exe_common_settings tgt)
  cmake_parse_arguments(ARG "" "ROOT_DIR;SHOW_CONSOLE;UNITY_BUILD;UNITY_UNIQUE_ID" "" ${ARGN})

  if(NOT ARG_ROOT_DIR)
    get_filename_component(ARG_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  if(WIN32)
    target_compile_definitions("${tgt}" PRIVATE
      WIN32_LEAN_AND_MEAN
      NOMINMAX
      UNICODE
      _UNICODE
    )

    if(NOT ARG_SHOW_CONSOLE)
      set_target_properties("${tgt}" PROPERTIES WIN32_EXECUTABLE YES)
    else()
      # Explicitly flip back to console subsystem when requested.
      set_target_properties("${tgt}" PROPERTIES WIN32_EXECUTABLE NO)
    endif()

    # Make VS "Start Debugging" run from the EXE output directory by default.
    # This helps asset/shader relative paths behave consistently.
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
      set_target_properties("${tgt}" PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${tgt}>"
      )
    endif()
  endif()

  if(ARG_UNITY_BUILD)
    set_target_properties("${tgt}" PROPERTIES UNITY_BUILD ON)
    if(ARG_UNITY_UNIQUE_ID)
      set_target_properties("${tgt}" PROPERTIES UNITY_BUILD_UNIQUE_ID "${ARG_UNITY_UNIQUE_ID}")
    endif()
  else()
    set_target_properties("${tgt}" PROPERTIES UNITY_BUILD OFF)
  endif()

  if(CMAKE_VERSION VERSION_LESS 3.21)
    target_compile_features("${tgt}" PRIVATE cxx_std_20)
  else()
    target_compile_features("${tgt}" PRIVATE cxx_std_23)
  endif()

  target_include_directories("${tgt}" PRIVATE
    "${ARG_ROOT_DIR}/src"
    "${ARG_ROOT_DIR}/include"
    "${ARG_ROOT_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
  )

  # Apply toolchain defaults (warnings/sanitizers + optional /WX link for MSVC).
  # IMPORTANT: Do NOT force LINK_WERROR here; let CGToolchainWin.cmake's option
  # (COLONY_MSVC_LINK_WERROR) control it project-wide.
  cg_toolchain_win_setup_target("${tgt}"
    IS_EXE ON
  )
endfunction()

function(cg_setup_executables)
  cmake_parse_arguments(ARG ""
    "ROOT_DIR;FRONTEND;SHOW_CONSOLE;UNITY_BUILD;BUILD_LAUNCHER;CORE_TARGET;BUILD_OPTIONS_TARGET;OUT_TARGETS"
    ""
    ${ARGN}
  )

  if(NOT ARG_ROOT_DIR)
    get_filename_component(ARG_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  if(NOT DEFINED ARG_FRONTEND)
    set(ARG_FRONTEND "")
  endif()

  if(NOT DEFINED ARG_BUILD_LAUNCHER)
    set(ARG_BUILD_LAUNCHER OFF)
  endif()

  if(NOT ARG_CORE_TARGET)
    set(ARG_CORE_TARGET colony_core)
  endif()

  if(NOT ARG_BUILD_OPTIONS_TARGET)
    set(ARG_BUILD_OPTIONS_TARGET colony_build_options)
  endif()

  # ------------------------------- ColonyGame -------------------------------
  if(NOT TARGET ColonyGame)
    add_executable(ColonyGame)
  endif()

  _cg_apply_exe_common_settings(ColonyGame
    ROOT_DIR "${ARG_ROOT_DIR}"
    SHOW_CONSOLE "${ARG_SHOW_CONSOLE}"
    UNITY_BUILD "${ARG_UNITY_BUILD}"
    UNITY_UNIQUE_ID "COLONY"
  )

  target_link_libraries(ColonyGame PRIVATE
    "${ARG_CORE_TARGET}"
    "${ARG_BUILD_OPTIONS_TARGET}"
  )

  _cg_link_platform_win_if_present(ColonyGame)

  cg_add_colonygame_sources(ColonyGame
    ROOT_DIR "${ARG_ROOT_DIR}"
    FRONTEND "${ARG_FRONTEND}"
  )

  # SDL frontend typically requires SDL2main on Windows.
  if(WIN32 AND (ARG_FRONTEND STREQUAL "sdl"))
    if(TARGET SDL2::SDL2main)
      target_link_libraries(ColonyGame PRIVATE SDL2::SDL2main)
    endif()
  endif()

  # Some crash/dump stacks still reference dbghelp symbols (or prefer an explicit dep).
  if(WIN32)
    target_link_libraries(ColonyGame PRIVATE dbghelp)
  endif()

  # Apply CrashDumpWin.cpp special handling if it was added to this EXE.
  _cg_apply_seh_and_pch_exclusions_for_crashdump(ColonyGame)

  set(_exe_targets ColonyGame)

  # ------------------------------ ColonyLauncher ------------------------------
  if(WIN32 AND ARG_BUILD_LAUNCHER AND NOT (ARG_FRONTEND STREQUAL "sdl"))
    if(NOT TARGET ColonyLauncher)
      add_executable(ColonyLauncher)
    endif()

    _cg_apply_exe_common_settings(ColonyLauncher
      ROOT_DIR "${ARG_ROOT_DIR}"
      SHOW_CONSOLE "${ARG_SHOW_CONSOLE}"
      UNITY_BUILD "${ARG_UNITY_BUILD}"
      UNITY_UNIQUE_ID "COLONY_LAUNCHER"
    )

    target_link_libraries(ColonyLauncher PRIVATE
      "${ARG_CORE_TARGET}"
      "${ARG_BUILD_OPTIONS_TARGET}"
      shell32
      ole32
    )

    _cg_link_platform_win_if_present(ColonyLauncher)

    cg_add_colonylauncher_sources(ColonyLauncher
      ROOT_DIR "${ARG_ROOT_DIR}"
      FRONTEND "${ARG_FRONTEND}"
    )

    # If the launcher compiles CrashDumpWin.cpp, keep link deps consistent.
    if(WIN32)
      target_link_libraries(ColonyLauncher PRIVATE dbghelp)
    endif()

    _cg_apply_seh_and_pch_exclusions_for_crashdump(ColonyLauncher)

    list(APPEND _exe_targets ColonyLauncher)
  endif()

  if(ARG_OUT_TARGETS)
    set(${ARG_OUT_TARGETS} "${_exe_targets}" PARENT_SCOPE)
  endif()

  unset(_exe_targets)
endfunction()
