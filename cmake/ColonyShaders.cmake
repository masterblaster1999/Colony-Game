# cmake/ColonyShaders.cmake
include(CMakeParseArguments)

function(_colony_find_hlsl_compilers out_dxc out_fxc)
  # Prefer DXC (via vcpkg or PATH), then FXC (Windows SDK)
  # vcpkg manifest mode often defines these variables:
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    find_program(_DXC_EXE NAMES dxc HINTS
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc"
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  else()
    find_program(_DXC_EXE NAMES dxc)
  endif()

  find_program(_FXC_EXE NAMES fxc
    HINTS "$ENV{WindowsSdkDir}/bin" "C:/Program Files (x86)/Windows Kits/10/bin" "C:/Program Files/Windows Kits/10/bin"
    PATH_SUFFIXES x64 x86)

  set(${out_dxc} "${_DXC_EXE}" PARENT_SCOPE)
  set(${out_fxc} "${_FXC_EXE}" PARENT_SCOPE)
endfunction()

# colony_register_shaders(
#   TARGET       <runtime target that should depend on compiled shaders>
#   MANIFEST     <path to renderer/Shaders/shaders.json>
#   [OUTPUT_DIR  <dir for compiled blobs>]
#   [DXC_ARGS    <extra args for dxc>]
# )
function(colony_register_shaders)
  set(_opts)
  set(_one TARGET MANIFEST OUTPUT_DIR)
  set(_many DXC_ARGS)
  cmake_parse_arguments(CRS "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT WIN32)
    message(FATAL_ERROR "colony_register_shaders: Windows-only")
  endif()
  if(NOT CRS_TARGET OR NOT CRS_MANIFEST OR NOT EXISTS "${CRS_MANIFEST}")
    message(FATAL_ERROR "colony_register_shaders: TARGET and existing MANIFEST are required")
  endif()
  if(NOT CRS_OUTPUT_DIR)
    set(CRS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()

  file(READ "${CRS_MANIFEST}" _json_text)

  # Parse the top-level array length; bail out early if malformed/empty.
  string(JSON _count ERROR_VARIABLE _json_err LENGTH "${_json_text}")
  if(_json_err)
    message(FATAL_ERROR "Invalid JSON in ${CRS_MANIFEST}: ${_json_err}")
  endif()
  if(_count EQUAL 0)
    message(WARNING "No shader entries found in ${CRS_MANIFEST}")
    return()
  endif()

  _colony_find_hlsl_compilers(_DXC _FXC)
  if(NOT _DXC AND NOT _FXC)
    message(FATAL_ERROR "No HLSL compiler found (tried dxc.exe and fxc.exe). Install 'directx-dxc' and/or the Windows 10/11 SDK.")
  endif()

  file(MAKE_DIRECTORY "${CRS_OUTPUT_DIR}")
  set(_outputs)

  # Helper to accumulate -I /I and defines per entry
  function(_make_inc_and_def_args _i _incArgsOut _defArgsOut)
    # includes (array)
    set(_incs "")
    string(JSON _incsType TYPE "${_json_text}" ${_i} includes)
    if(_incsType STREQUAL "ARRAY")
      string(JSON _incsLen LENGTH "${_json_text}" ${_i} includes)
      math(EXPR _incLast "${_incsLen}-1")
      foreach(_j RANGE 0 ${_incLast})
        string(JSON _oneInc GET "${_json_text}" ${_i} includes ${_j})
        # Search relative to renderer/Shaders/<include>
        list(APPEND _incs "${CMAKE_SOURCE_DIR}/renderer/Shaders/${_oneInc}")
      endforeach()
    endif()

    set(_incArgs "")
    foreach(_in ${_incs})
      if(_DXC)
        list(APPEND _incArgs -I "${_in}")
      else()
        list(APPEND _incArgs /I "${_in}")
      endif()
    endforeach()

    # defines (array)
    set(_defArgs "")
    string(JSON _defsType TYPE "${_json_text}" ${_i} defines)
    if(_defsType STREQUAL "ARRAY")
      string(JSON _defsLen LENGTH "${_json_text}" ${_i} defines)
      math(EXPR _defLast "${_defsLen}-1")
      foreach(_k RANGE 0 ${_defLast})
        string(JSON _oneDef GET "${_json_text}" ${_i} defines ${_k})
        if(_DXC)
          list(APPEND _defArgs -D "${_oneDef}")
        else()
          list(APPEND _defArgs /D "${_oneDef}")
        endif()
      endforeach()
    endif()

    set(${_incArgsOut} "${_incArgs}" PARENT_SCOPE)
    set(${_defArgsOut} "${_defArgs}" PARENT_SCOPE)
  endfunction()

  math(EXPR _last "${_count}-1")
  foreach(_i RANGE 0 ${_last})
    string(JSON _file   GET "${_json_text}" ${_i} file)
    string(JSON _entry  GET "${_json_text}" ${_i} entry)
    string(JSON _profile GET "${_json_text}" ${_i} profile)
    if(NOT _entry OR _entry STREQUAL "")
      set(_entry "main")
    endif()
    if(NOT _profile OR _profile STREQUAL "")
      set(_profile "ps_5_0")
    endif()

    # Resolve paths; source shaders live under renderer/Shaders
    get_filename_component(_srcAbs "${_file}"
      ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}/renderer/Shaders")
    get_filename_component(_base "${_srcAbs}" NAME_WE)
    set(_out "${CRS_OUTPUT_DIR}/${_base}.cso")

    # Per-entry include/define args
    _make_inc_and_def_args(${_i} _incArgs _defArgs)

    if(_DXC)
      # DXC flags
      set(_cmd "${_DXC}" -nologo -T "${_profile}" -E "${_entry}"
               -Fo "${_out}" "${_srcAbs}" ${_incArgs} ${_defArgs} ${CRS_DXC_ARGS})
    else()
      # FXC flags
      set(_cmd "${_FXC}" /nologo /T "${_profile}" /E "${_entry}"
               /Fo "${_out}" "${_srcAbs}" ${_incArgs} ${_defArgs})
    endif()

    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CRS_OUTPUT_DIR}"
      COMMAND ${_cmd}
      MAIN_DEPENDENCY "${_srcAbs}"
      COMMENT "HLSL: ${_file} (${_profile}:${_entry}) → ${_base}.cso"
      VERBATIM)

    list(APPEND _outputs "${_out}")
  endforeach()

  add_custom_target(${CRS_TARGET}_shaders DEPENDS ${_outputs})
  add_dependencies(${CRS_TARGET} ${CRS_TARGET}_shaders)
endfunction()

function(colony_install_shaders)
  set(_opts)
  set(_one TARGET DESTINATION)
  cmake_parse_arguments(CIS "${_opts}" "${_one}" "" ${ARGN})
  if(NOT CIS_TARGET OR NOT CIS_DESTINATION)
    message(FATAL_ERROR "colony_install_shaders: TARGET and DESTINATION are required")
  endif()
  install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/" DESTINATION "${CIS_DESTINATION}")
endfunction()
