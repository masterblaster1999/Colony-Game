# cmake/CGRuntimeLayout.cmake
#
# Runtime layout helpers:
# - Set runtime output directories (bin/ or bin/<Config>/)
# - Post-build copy of assets/res/Content next to the built EXE
# - Install of assets/res/Content
#
# Design notes:
# - Multi-config generators (VS, Ninja Multi-Config) are handled via
#   RUNTIME_OUTPUT_DIRECTORY_<CONFIG> so CMake does not append its own
#   per-config subdirectory.
# - Post-build copy uses add_custom_command(TARGET ... POST_BUILD ...) with
#   generator expressions like $<TARGET_FILE_DIR:...> in COMMAND args.

include_guard(GLOBAL)

function(cg_configure_runtime_output)
  cmake_parse_arguments(ARG "" "BASE_DIR" "TARGETS" ${ARGN})

  if(NOT ARG_TARGETS)
    message(FATAL_ERROR "cg_configure_runtime_output: TARGETS is required.")
  endif()

  if(NOT ARG_BASE_DIR)
    set(ARG_BASE_DIR "${CMAKE_BINARY_DIR}/bin")
  endif()

  if(CMAKE_CONFIGURATION_TYPES)
    foreach(tgt IN LISTS ARG_TARGETS)
      if(TARGET "${tgt}")
        foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
          string(TOUPPER "${cfg}" CFGU)
          set_target_properties("${tgt}" PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${CFGU}" "${ARG_BASE_DIR}/${cfg}"
          )
        endforeach()
      endif()
    endforeach()
  else()
    foreach(tgt IN LISTS ARG_TARGETS)
      if(TARGET "${tgt}")
        set_target_properties("${tgt}" PROPERTIES
          RUNTIME_OUTPUT_DIRECTORY "${ARG_BASE_DIR}"
        )
      endif()
    endforeach()
  endif()
endfunction()

function(cg_add_post_build_content_copy)
  cmake_parse_arguments(ARG "" "TARGET;ROOT_DIR" "" ${ARGN})

  if(NOT ARG_TARGET)
    message(FATAL_ERROR "cg_add_post_build_content_copy: TARGET is required.")
  endif()

  if(NOT ARG_ROOT_DIR)
    get_filename_component(ARG_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  # Generator expression is allowed in add_custom_command COMMAND arguments.
  set(_RUNTIME_DIR "$<TARGET_FILE_DIR:${ARG_TARGET}>")

  # Always copy assets/ and res/ (matches your current behavior).
  add_custom_command(TARGET "${ARG_TARGET}" POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/res"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARG_ROOT_DIR}/assets" "${_RUNTIME_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ARG_ROOT_DIR}/res"    "${_RUNTIME_DIR}/res"
    VERBATIM
  )

  # Copy Content/ only if present.
  set(_CONTENT_DIR "${ARG_ROOT_DIR}/Content")
  if(EXISTS "${_CONTENT_DIR}")
    add_custom_command(TARGET "${ARG_TARGET}" POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_RUNTIME_DIR}/Content"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${_CONTENT_DIR}" "${_RUNTIME_DIR}/Content"
      COMMENT "Copy Content/ next to the EXE for streaming"
      VERBATIM
    )
  endif()

  unset(_RUNTIME_DIR)
  unset(_CONTENT_DIR)
endfunction()

function(cg_install_runtime_content)
  cmake_parse_arguments(ARG "" "ROOT_DIR" "" ${ARGN})

  if(NOT ARG_ROOT_DIR)
    get_filename_component(ARG_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
  endif()

  if(EXISTS "${ARG_ROOT_DIR}/assets")
    install(DIRECTORY "${ARG_ROOT_DIR}/assets/" DESTINATION "assets")
  endif()

  if(EXISTS "${ARG_ROOT_DIR}/res")
    install(DIRECTORY "${ARG_ROOT_DIR}/res/" DESTINATION "res")
  endif()

  if(EXISTS "${ARG_ROOT_DIR}/Content")
    install(DIRECTORY "${ARG_ROOT_DIR}/Content/" DESTINATION "Content")
  endif()
endfunction()
