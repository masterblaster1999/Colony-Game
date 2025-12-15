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

function(_cg_apply_exe_common_settings tgt)
  cmake_parse_arguments(ARG "" "ROOT_DIR;SHOW_CONSOLE;UNITY_BUILD;UNITY_UNIQUE_ID" "" ${ARGN})

  if(NOT ARG_ROOT_DIR)
    get_filename_component(ARG_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  if(WIN32)
    target_compile_definitions("${tgt}" PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(NOT ARG_SHOW_CONSOLE)
      set_target_properties("${tgt}" PROPERTIES WIN32_EXECUTABLE YES)
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

  # Apply toolchain defaults (warnings/sanitizers + /WX link for MSVC)
  cg_toolchain_win_setup_target("${tgt}"
    IS_EXE       ON
    LINK_WERROR  ON
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

  target_link_libraries(ColonyGame PRIVATE "${ARG_CORE_TARGET}" "${ARG_BUILD_OPTIONS_TARGET}")

  if(TARGET Colony::PlatformWin)
    target_link_libraries(ColonyGame PRIVATE Colony::PlatformWin)
  endif()
  if(TARGET Colony::WinPlatform)
    target_link_libraries(ColonyGame PRIVATE Colony::WinPlatform)
  endif()

  cg_add_colonygame_sources(ColonyGame
    ROOT_DIR "${ARG_ROOT_DIR}"
    FRONTEND "${ARG_FRONTEND}"
  )

  if(WIN32)
    target_link_libraries(ColonyGame PRIVATE dbghelp)
  endif()

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
      shell32 ole32
    )

    if(TARGET Colony::PlatformWin)
      target_link_libraries(ColonyLauncher PRIVATE Colony::PlatformWin)
    endif()
    if(TARGET Colony::WinPlatform)
      target_link_libraries(ColonyLauncher PRIVATE Colony::WinPlatform)
    endif()

    cg_add_colonylauncher_sources(ColonyLauncher
      ROOT_DIR "${ARG_ROOT_DIR}"
      FRONTEND "${ARG_FRONTEND}"
    )

    list(APPEND _exe_targets ColonyLauncher)
  endif()

  if(ARG_OUT_TARGETS)
    set(${ARG_OUT_TARGETS} "${_exe_targets}" PARENT_SCOPE)
  endif()

  unset(_exe_targets)
endfunction()
