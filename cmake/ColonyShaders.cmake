# cmake/ColonyShaders.cmake
# JSON-driven HLSL compilation with DXC (Option B: OUTPUT-based rules)
# Windows-only. Requires: CMake >= 3.20 (genex in OUTPUT), 3.19 (string(JSON)).
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/ColonyShaders.cmake)
#   colony_register_shaders(
#     TARGET       ColonyGame
#     MANIFEST     ${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json
#     OUTPUT_DIR   ${CMAKE_BINARY_DIR}/shaders
#     INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/renderer/Shaders
#     DEFINES      USE_FOG=1
#     DXC_ARGS     -nologo
#   )
#   colony_install_shaders(TARGET ColonyGame DESTINATION bin/shaders)

include_guard(GLOBAL)

if(CMAKE_VERSION VERSION_LESS "3.20")
  message(FATAL_ERROR "ColonyShaders.cmake requires CMake 3.20+")
endif()

function(colony_register_shaders)
  set(options)
  set(oneValueArgs TARGET MANIFEST OUTPUT_DIR)
  set(multiValueArgs INCLUDE_DIRS DEFINES DXC_ARGS)
  cmake_parse_arguments(CSH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CSH_TARGET)
    message(FATAL_ERROR "colony_register_shaders: specify TARGET <name>")
  endif()
  if(NOT TARGET ${CSH_TARGET})
    message(FATAL_ERROR "colony_register_shaders: target ${CSH_TARGET} does not exist yet. Call after add_executable/add_library.")
  endif()
  if(NOT CSH_MANIFEST)
    message(FATAL_ERROR "colony_register_shaders: specify MANIFEST <file.json>")
  endif()
  if(NOT EXISTS "${CSH_MANIFEST}")
    message(FATAL_ERROR "colony_register_shaders: manifest not found: ${CSH_MANIFEST}")
  endif()

  # Resolve DXC
  if(NOT DEFINED DIRECTX_DXC_TOOL OR NOT EXISTS "${DIRECTX_DXC_TOOL}")
    # Fallback search (works even without vcpkg variable)
    find_program(DIRECTX_DXC_TOOL NAMES dxc)
  endif()
  if(NOT DIRECTX_DXC_TOOL)
    message(FATAL_ERROR "DXC not found. Install 'directx-dxc' (vcpkg) or set DIRECTX_DXC_TOOL.")
  endif()

  # Default output directory
  if(NOT CSH_OUTPUT_DIR)
    set(CSH_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  endif()

  # Read & check manifest
  file(READ "${CSH_MANIFEST}" _csh_json)
  string(JSON _csh_type TYPE "${_csh_json}")
  if(NOT _csh_type STREQUAL "ARRAY")
    message(FATAL_ERROR "shader manifest must be a JSON array of objects")
  endif()
  string(JSON _csh_count LENGTH "${_csh_json}")
  math(EXPR _last "${_csh_count} - 1")

  set(_csh_outputs)

  foreach(_i RANGE 0 ${_last})
    # Required keys
    string(JSON _file    GET "${_csh_json}" ${_i} file)
    string(JSON _entry   GET "${_csh_json}" ${_i} entry)
    string(JSON _profile GET "${_csh_json}" ${_i} profile)
    if(_file STREQUAL "" OR _entry STREQUAL "" OR _profile STREQUAL "")
      message(FATAL_ERROR "manifest[${_i}]: keys 'file', 'entry', 'profile' required")
    endif()

    # Resolve source path
    if(NOT IS_ABSOLUTE "${_file}")
      get_filename_component(_manifest_dir "${CSH_MANIFEST}" DIRECTORY)
      set(_src "${_manifest_dir}/${_file}")
    else()
      set(_src "${_file}")
    endif()
    if(NOT EXISTS "${_src}")
      message(FATAL_ERROR "manifest[${_i}]: source not found: ${_src}")
    endif()

    # Per-shader defines
    set(_shader_defines)
    string(JSON _defs_type TYPE "${_csh_json}" ${_i} defines ERROR_VARIABLE _defs_err)
    if(NOT _defs_err AND _defs_type STREQUAL "ARRAY")
      string(JSON _defs_len LENGTH "${_csh_json}" ${_i} defines)
      math(EXPR _defs_last "${_defs_len} - 1")
      foreach(_j RANGE 0 ${_defs_last})
        string(JSON _def GET "${_csh_json}" ${_i} defines ${_j})
        list(APPEND _shader_defines "-D${_def}")
      endforeach()
    endif()

    # Per-shader include dirs (relative to the shader file dir)
    set(_shader_includes)
    string(JSON _incs_type TYPE "${_csh_json}" ${_i} includes ERROR_VARIABLE _incs_err)
    if(NOT _incs_err AND _incs_type STREQUAL "ARRAY")
      string(JSON _incs_len LENGTH "${_csh_json}" ${_i} includes)
      math(EXPR _incs_last "${_incs_len} - 1")
      foreach(_k RANGE 0 ${_incs_last})
        string(JSON _inc GET "${_csh_json}" ${_i} includes ${_k})
        if(NOT IS_ABSOLUTE "${_inc}")
          get_filename_component(_file_dir "${_src}" DIRECTORY)
          set(_inc_resolved "${_file_dir}/${_inc}")
        else()
          set(_inc_resolved "${_inc}")
        endif()
        list(APPEND _shader_includes "-I${_inc_resolved}")
      endforeach()
    endif()

    # Global include dirs / defines
    foreach(_idir IN LISTS CSH_INCLUDE_DIRS)
      list(APPEND _shader_includes "-I${_idir}")
    endforeach()
    foreach(_d IN LISTS CSH_DEFINES)
      list(APPEND _shader_defines "-D${_d}")
    endforeach()

    # Per-config flags
    set(_cfg_flags
      "$<$<CONFIG:Debug>:-Od;-Zi;-Qembed_debug>"
      "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:-O3>"
    )

    # Output path (per-config to avoid collisions on multi-config generators like VS)
    get_filename_component(_name "${_src}" NAME_WE)
    string(REPLACE "." "_" _profile_sanitized "${_profile}")
    set(_out "${CSH_OUTPUT_DIR}/$<CONFIG>/${_name}.${_profile_sanitized}.cso")

    # Build rule
    add_custom_command(
      OUTPUT "${_out}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CSH_OUTPUT_DIR}/$<CONFIG>"
      COMMAND "${DIRECTX_DXC_TOOL}" -nologo
              -T "${_profile}"
              -E "${_entry}"
              ${_cfg_flags}
              ${_shader_includes}
              ${_shader_defines}
              ${CSH_DXC_ARGS}
              -Fo "${_out}"
              "${_src}"
      DEPENDS "${_src}"
      COMMENT "DXC ${_profile}: ${_file} -> ${_out}"
      VERBATIM
      COMMAND_EXPAND_LISTS
    )

    list(APPEND _csh_outputs "${_out}")
  endforeach()

  # Aggregate & wire to the consumer target
  set(_shader_target "${CSH_TARGET}_shaders")
  add_custom_target(${_shader_target} ALL DEPENDS ${_csh_outputs})
  add_dependencies(${CSH_TARGET} ${_shader_target})

  # Expose for install helper
  set_property(GLOBAL APPEND PROPERTY COLONY_${CSH_TARGET}_SHADER_OUTPUTS "${_csh_outputs}")
endfunction()

function(colony_install_shaders)
  set(options)
  set(oneValueArgs TARGET DESTINATION)
  cmake_parse_arguments(CIS "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT CIS_TARGET OR NOT CIS_DESTINATION)
    message(FATAL_ERROR "colony_install_shaders: usage: colony_install_shaders(TARGET <tgt> DESTINATION <dir>)")
  endif()

  get_property(_outs GLOBAL PROPERTY COLONY_${CIS_TARGET}_SHADER_OUTPUTS)
  if(_outs)
    install(FILES ${_outs} DESTINATION "${CIS_DESTINATION}")
  else()
    message(WARNING "colony_install_shaders: no registered shader outputs for target ${CIS_TARGET}")
  endif()
endfunction()
