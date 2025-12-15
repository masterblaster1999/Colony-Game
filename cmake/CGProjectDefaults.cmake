# cmake/CGProjectDefaults.cmake
#
# Project-level defaults:
# - Turn on solution folders (USE_FOLDERS)
# - Put CMake predefined targets into a nicer folder (PREDEFINED_TARGETS_FOLDER)
# - Set Visual Studio startup project (VS_STARTUP_PROJECT) on the solution directory
# - Provide helpers to assign per-target solution folders (FOLDER property)

include_guard(GLOBAL)

function(cg_project_defaults_init)
  cmake_parse_arguments(ARG "" "PREDEFINED_TARGETS_FOLDER" "" ${ARGN})

  # Enable folder/group organization in IDE generators (VS solution folders etc.)
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)

  # Rename the built-in "CMakePredefinedTargets" folder if desired
  if(NOT "${ARG_PREDEFINED_TARGETS_FOLDER}" STREQUAL "")
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "${ARG_PREDEFINED_TARGETS_FOLDER}")
  endif()
endfunction()

function(cg_project_set_vs_startup_project target)
  # VS_STARTUP_PROJECT is a DIRECTORY property affecting the .sln produced for that directory.
  if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT "${target}")
  endif()
endfunction()

function(cg_project_set_target_folder)
  cmake_parse_arguments(ARG "" "TARGET;FOLDER" "" ${ARGN})

  if("${ARG_TARGET}" STREQUAL "" OR "${ARG_FOLDER}" STREQUAL "")
    message(FATAL_ERROR "cg_project_set_target_folder: TARGET and FOLDER are required.")
  endif()

  if(TARGET "${ARG_TARGET}")
    set_target_properties("${ARG_TARGET}" PROPERTIES FOLDER "${ARG_FOLDER}")
  endif()
endfunction()

function(cg_project_apply_standard_folders)
  cmake_parse_arguments(ARG "" "LIB_FOLDER;APP_FOLDER" "LIB_TARGETS;APP_TARGETS" ${ARGN})

  if("${ARG_LIB_FOLDER}" STREQUAL "")
    set(ARG_LIB_FOLDER "Libraries")
  endif()
  if("${ARG_APP_FOLDER}" STREQUAL "")
    set(ARG_APP_FOLDER "Apps")
  endif()

  foreach(tgt IN LISTS ARG_LIB_TARGETS)
    if(TARGET "${tgt}")
      set_target_properties("${tgt}" PROPERTIES FOLDER "${ARG_LIB_FOLDER}")
    endif()
  endforeach()

  foreach(tgt IN LISTS ARG_APP_TARGETS)
    if(TARGET "${tgt}")
      set_target_properties("${tgt}" PROPERTIES FOLDER "${ARG_APP_FOLDER}")
    endif()
  endforeach()
endfunction()
