# cmake/ColonyShaders.cmake

include(CMakeParseArguments)

function(_colony_find_hlsl_compilers out_dxc out_fxc)
  # Prefer dxc (via vcpkg or PATH), then fxc (Windows SDK)
  find_program(_DXC_EXE NAMES dxc HINTS "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/dxc")
  find_program(_FXC_EXE NAMES fxc
    HINTS "$ENV{WindowsSdkDir}/bin" "C:/Program Files (x86)/Windows Kits/10/bin" "C:/Program Files/Windows Kits/10/bin"
    PATH_SUFFIXES x64 x86)
  set(${out_dxc} "${_DXC_EXE}" PARENT_SCOPE)
  set(${out_fxc} "${_FXC_EXE}" PARENT_SCOPE)
endfunction()

# Parse a very simple shaders.json: [{"file":"mesh_vs.hlsl","entry":"main","profile":"vs_5_0","defines":["X=1"],"includes":["include"]}, ...]
function(colony_register_shaders)
  set(_opts)
  set(_one TARGET MANIFEST OUTPUT_DIR)
  set(_many INCLUDE_DIRS DXC_ARGS)
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
  # Quick-n-dirty parse: we expect a flat JSON array with per-shader dicts.
  # (Keeps dependencies low; OK for your simple manifest.)
  string(REPLACE "\n" "" _json_text "${_json_text}")
  string(REPLACE "\r" "" _json_text "${_json_text}")

  _colony_find_hlsl_compilers(_DXC _FXC)
  if(NOT _DXC AND NOT _FXC)
    message(FATAL_ERROR "No HLSL compiler found (tried dxc, fxc)")
  endif()

  file(MAKE_DIRECTORY "${CRS_OUTPUT_DIR}")
  set(_outputs)

  # Naive extraction: split on "file": "....hlsl" occurrences
  string(REGEX MATCHALL "\"file\"[ \t]*:[ \t]*\"([^\"]+)\"" _files "${_json_text}")
  foreach(_m ${_files})
    string(REGEX REPLACE ".*\"file\"[ \t]*:[ \t]*\"([^\"]+)\".*" "\\1" _file "${_m}")

    # Pull profile (default ps_5_0) and entry (default main) if present
    string(REGEX MATCH "\"profile\"[ \t]*:[ \t]*\"([^\"]+)\"" _pm "${_json_text}")
    if(_pm)
      string(REGEX REPLACE ".*\"profile\"[ \t]*:[ \t]*\"([^\"]+)\".*" "\\1" _profile "${_pm}")
    else()
      set(_profile "ps_5_0")
    endif()
    string(REGEX MATCH "\"entry\"[ \t]*:[ \t]*\"([^\"]+)\"" _em "${_json_text}")
    if(_em)
      string(REGEX REPLACE ".*\"entry\"[ \t]*:[ \t]*\"([^\"]+)\".*" "\\1" _entry "${_em}")
    else()
      set(_entry "main")
    endif()

    get_filename_component(_abs "${_file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/../renderer/Shaders")
    get_filename_component(_name "${_abs}" NAME_WE)
    set(_out "${CRS_OUTPUT_DIR}/${_name}.cso")

    if(_DXC)
      set(_cmd "${_DXC}" -nologo -T "${_profile}" -E "${_entry}" -Fo "${_out}" "${_abs}" ${CRS_DXC_ARGS})
    else()
      # FXC syntax for SM 5.x
      set(_cmd "${_FXC}" /nologo /T "${_profile}" /E "${_entry}" /Fo "${_out}" "${_abs}")
    endif()

    add_custom_command(OUTPUT "${_out}"
      COMMAND ${_cmd}
      DEPENDS "${_abs}" VERBATIM
      COMMENT "HLSL: ${_file} -> ${_name}.cso")
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
  install(DIRECTORY "${CMAKE_BINARY_DIR}/shaders/"
          DESTINATION "${CIS_DESTINATION}")
endfunction()
