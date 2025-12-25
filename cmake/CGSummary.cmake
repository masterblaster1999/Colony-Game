function(cg_print_config_summary)
  message(STATUS "Colony Config Summary")

  # High-level toggles
  message(STATUS "  FRONTEND:                    ${FRONTEND}")
  message(STATUS "  ENABLE_IMGUI:                ${ENABLE_IMGUI}")
  message(STATUS "  ENABLE_TRACY:                ${ENABLE_TRACY}")
  message(STATUS "  SHOW_CONSOLE:                ${SHOW_CONSOLE}")
  message(STATUS "  COLONY_BUILD_LAUNCHER:        ${COLONY_BUILD_LAUNCHER}")
  message(STATUS "  BUILD_TESTING:               ${BUILD_TESTING}")

  # Build performance knobs
  message(STATUS "  COLONY_USE_PCH:               ${COLONY_USE_PCH}")
  message(STATUS "  COLONY_PCH_HEADER:            ${COLONY_PCH_HEADER}")
  message(STATUS "  COLONY_UNITY_BUILD:           ${COLONY_UNITY_BUILD}")

  # Compiler behavior
  message(STATUS "  COLONY_WERROR:                ${COLONY_WERROR}")
  message(STATUS "  COLONY_WARNINGS_AS_ERRORS:    ${COLONY_WARNINGS_AS_ERRORS}")
  message(STATUS "  COLONY_ENABLE_COMPUTE_SHADERS: ${COLONY_ENABLE_COMPUTE_SHADERS}")

  # Toolchain info
  message(STATUS "  CMAKE_GENERATOR:              ${CMAKE_GENERATOR}")
  message(STATUS "  CMAKE_CXX_STANDARD:           ${CMAKE_CXX_STANDARD}")
endfunction()
