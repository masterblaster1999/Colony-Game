# cmake/CGInstall.cmake
#
# Centralizes install rules for ColonyGame and optional ColonyLauncher.
#
# Uses install(TARGETS ... RUNTIME DESTINATION ...) :contentReference[oaicite:5]{index=5}
# and delegates content directory install to cg_install_runtime_content().

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/CGRuntimeLayout.cmake")

function(cg_install_colony_artifacts)
  cmake_parse_arguments(ARG "" "ROOT_DIR;GAME_TARGET;LAUNCHER_TARGET;RUNTIME_DESTINATION" "" ${ARGN})

  if(NOT ARG_ROOT_DIR)
    get_filename_component(ARG_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  if(NOT ARG_GAME_TARGET)
    set(ARG_GAME_TARGET "ColonyGame")
  endif()

  if(NOT ARG_LAUNCHER_TARGET)
    set(ARG_LAUNCHER_TARGET "ColonyLauncher")
  endif()

  if(NOT ARG_RUNTIME_DESTINATION)
    set(ARG_RUNTIME_DESTINATION ".")
  endif()

  if(NOT TARGET "${ARG_GAME_TARGET}")
    message(FATAL_ERROR "cg_install_colony_artifacts: GAME_TARGET '${ARG_GAME_TARGET}' is not a target.")
  endif()

  # Install main game executable
  install(TARGETS "${ARG_GAME_TARGET}"
    RUNTIME DESTINATION "${ARG_RUNTIME_DESTINATION}"
  )

  # Install launcher executable only if it exists / was built
  if(TARGET "${ARG_LAUNCHER_TARGET}")
    install(TARGETS "${ARG_LAUNCHER_TARGET}"
      RUNTIME DESTINATION "${ARG_RUNTIME_DESTINATION}"
    )
  endif()

  # Install runtime data directories (assets/, res/, Content/ if present)
  cg_install_runtime_content(ROOT_DIR "${ARG_ROOT_DIR}")
endfunction()
