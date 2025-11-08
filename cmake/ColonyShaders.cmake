# cmake/ColonyShaders.cmake

include(CMakeParseArguments)

# --------------------------
# DXC / FXC discovery
# --------------------------
function(_colony_find_hlsl_compilers out_dxc out_fxc)
  # Prefer DXC from vcpkg (both legacy and current paths), then PATH.
  find_program(_DXC_EXE NAMES dxc
    HINTS
      "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
      "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/dxc"
  )
  # FXC comes from Windows SDK
  find_program(_FXC_EXE NAMES fxc
    HINTS
      "$ENV{WindowsSdkDir}/bin"
      "C:/Program Files (x86)/Windows Kits/10/bin"
      "C:/Program Files/Windows Kits/10/bin"
    PATH_SUFFIXES x64 x86
  )
  set(${out_dxc} "${_DXC_EXE}" PARENT_SCOPE)
  set(${out_fxc} "${_FXC_EXE}" PARENT_SCOPE)
endfunction()

# Decide whether to use DXC or FXC for a given shader profile.
# - Optional override via -DCOLONY_HLSL_COMPILER=AUTO|DXC|FXC (AUTO = per-profile)
function(_colony_pick_compiler out_use_dxc profile)
  if(NOT DEFINED COLONY_HLSL_COMPILER)
    set(COLONY_HLSL_COMPILER "AUTO")
  endif()
  string(TOUPPER "${COLONY_HLSL_COMPILER}" _ovr)

  set(_use_dxc FALSE)
  if(_ovr STREQUAL "DXC")
    set(_use_dxc TRUE)
  elseif(_ovr STREQUAL "FXC")
    set(_use_dxc FALSE)
  else()
    # AUTO: Shader Model 6.x (and lib_6_x) -> DXC; 5.x -> FXC
    if("${profile}" MATCHES "^(ps|vs|gs|ds|hs|cs|ms|as|lib)_6_[0-9]+$")
      set(_use_dxc TRUE)
    else()
      set(_use_dxc FALSE)
    endif()
  endif()

  set(${out_use_dxc} ${_use_dxc} PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
# Robust per-file JSON parsing + correct compiler selection (Windows only)
# Manifest format (array of objects):
# [
#   {
#     "file": "mesh_ps.hlsl",
#     "entry": "main",                    // optional, default "main"
#     "profile": "ps_5_0",                // optional, default "ps_5_0"
#     "defines": ["USE_FOG=1","X=2"],     // optional
#     "includes": ["include","common"]    // optional, relative to renderer/Shaders unless absolute
#   },
#   ...
# ]
# ---------------------------------------------------------------------
function(colony_register_shaders)
  if(NOT WIN32)
    message(FATAL_ERROR "colony_register_shaders: Windows-only")
  endif()
  if(CMAKE_VERSION VERSION_LESS "3.19")
    message(FATAL_ERROR "colony_register_shaders requires CMake >= 3.19 (string(JSON) support)")
  endif()

  set(_opts)
  set(_one TARGET MANIFEST OUTPUT_DIR)
  set(_many INCLUDE_DIRS DXC_ARGS)
  cmake_parse_arguments(CRS "${_opts}" "${_one}" "${_many}" ${ARGN})

  if(NOT CRS_TARGET OR NOT CRS_MANIFEST OR NOT EXISTS "${CRS_MANIFEST}")
    message(FATAL_ERROR "colony_register_shaders: TARGET and an existing MANIFEST are required")
  endif()

  if(NOT CRS_OUTPUT_DIR)
    set(CRS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
  endif()

  # Locate compilers once
  _colony_find_hlsl_compilers(_DXC _FXC)
  if(NOT _DXC AND NOT _FXC)
    message(FATAL_ERROR "No HLSL compiler found (tried dxc, fxc)")
  endif()

  # Read manifest JSON and validate top-level type
  file(READ "${CRS_MANIFEST}" _json_text)

  string(JSON _root_type TYPE "${_json_text}" "")
  if(NOT _root_type STREQUAL "ARRAY")
    message(FATAL_ERROR "Shader manifest must be a JSON array: ${CRS_MANIFEST}")
  endif()

  file(MAKE_DIRECTORY "${CRS_OUTPUT_DIR}")
  set(_outputs)

  # Base folder for relative shader paths & includes
  get_filename_component(_shaders_base "${CMAKE_CURRENT_LIST_DIR}/../renderer/Shaders" ABSOLUTE)

  # Iterate manifest array
  string(JSON _count LENGTH "${_json_text}" "")
  if(_count GREATER 0)
    math(EXPR _last "${_count} - 1")
    foreach(i RANGE 0 ${_last})

      # Required: file
      set(_err "")
      string(JSON _file ERROR_VARIABLE _err GET "${_json_text}" ${i} "file")
      if(_err OR NOT _file)
        message(FATAL_ERROR "Manifest entry ${i}: missing required key \"file\"")
      endif()

      # Optional: entry (default main)
      set(_entry "")
      set(_err "")
      string(JSON _entry ERROR_VARIABLE _err GET "${_json_text}" ${i} "entry")
      if(_err OR NOT _entry)
        set(_entry "main")
      endif()

      # Optional: profile (default ps_5_0)
      set(_profile "")
      set(_err "")
      string(JSON _profile ERROR_VARIABLE _err GET "${_json_text}" ${i} "profile")
      if(_err OR NOT _profile)
        set(_profile "ps_5_0")
      endif()

      # Optional arrays: defines, includes
      set(_defines_list "")
      set(_includes_list "")

      set(_err "")
      string(JSON _def_type ERROR_VARIABLE _err TYPE "${_json_text}" ${i} "defines")
      if(NOT _err AND _def_type STREQUAL "ARRAY")
        string(JSON _def_len LENGTH "${_json_text}" ${i} "defines")
        if(_def_len GREATER 0)
          math(EXPR _dlast "${_def_len} - 1")
          foreach(di RANGE 0 ${_dlast})
            string(JSON _dval GET "${_json_text}" ${i} "defines" ${di})
            list(APPEND _defines_list "${_dval}")
          endforeach()
        endif()
      endif()

      set(_err "")
      string(JSON _inc_type ERROR_VARIABLE _err TYPE "${_json_text}" ${i} "includes")
      if(NOT _err AND _inc_type STREQUAL "ARRAY")
        string(JSON _inc_len LENGTH "${_json_text}" ${i} "includes")
        if(_inc_len GREATER 0)
          math(EXPR _ilast "${_inc_len} - 1")
          foreach(ii RANGE 0 ${_ilast})
            string(JSON _ival GET "${_json_text}" ${i} "includes" ${ii})
            list(APPEND _includes_list "${_ival}")
          endforeach()
        endif()
      endif()

      # Resolve absolute paths
      get_filename_component(_abs "${_file}" ABSOLUTE BASE_DIR "${_shaders_base}")
      if(NOT EXISTS "${_abs}")
        message(FATAL_ERROR "Shader not found: ${_abs}")
      endif()
      get_filename_component(_name "${_abs}" NAME_WE)
      set(_out "${CRS_OUTPUT_DIR}/${_name}.cso")

      # Merge include dirs: manifest-specified (relative to renderer/Shaders unless absolute) + user-provided INCLUDE_DIRS
      set(_all_includes "")
      foreach(_inc IN LISTS _includes_list)
        if(IS_ABSOLUTE "${_inc}")
          list(APPEND _all_includes "${_inc}")
        else()
          list(APPEND _all_includes "${_shaders_base}/${_inc}")
        endif()
      endforeach()
      foreach(_user_inc IN LISTS CRS_INCLUDE_DIRS)
        list(APPEND _all_includes "${_user_inc}")
      endforeach()

      # Which compiler for this profile?
      _colony_pick_compiler(_USE_DXC "${_profile}")

      # Build define/include args according to compiler
      set(_def_args "")
      foreach(_d IN LISTS _defines_list)
        if(_USE_DXC)
          list(APPEND _def_args "-D" "${_d}")
        else()
          list(APPEND _def_args "/D${_d}")
        endif()
      endforeach()
      set(_inc_args "")
      foreach(_incdir IN LISTS _all_includes)
        if(_USE_DXC)
          list(APPEND _inc_args "-I" "${_incdir}")
        else()
          list(APPEND _inc_args "/I${_incdir}")
        endif()
      endforeach()

      if(_USE_DXC)
        if(NOT _DXC)
          message(FATAL_ERROR "DXC requested but not found for profile ${_profile}")
        endif()
        add_custom_command(
          OUTPUT  "${_out}"
          COMMAND "${_DXC}"
                  -nologo
                  -T "${_profile}"
                  -E "${_entry}"
                  $<$<CONFIG:Debug>:-Zi -Qembed_debug -Od>
                  $<$<CONFIG:RelWithDebInfo>:-Zi -Qembed_debug -O3>
                  $<$<CONFIG:MinSizeRel>:-O3 -Qstrip_debug -Qstrip_reflect>
                  $<$<CONFIG:Release>:-O3 -Qstrip_debug -Qstrip_reflect>
                  ${_def_args}
                  ${_inc_args}
                  ${CRS_DXC_ARGS}
                  -Fo "${_out}"
                  "${_abs}"
          MAIN_DEPENDENCY "${_abs}"
          DEPENDS "${CRS_MANIFEST}"
          VERBATIM
          COMMENT "DXC ${_profile} ${_name}.hlsl → ${_out}"
        )
      else()
        if(NOT _FXC)
          message(FATAL_ERROR "FXC requested but not found for profile ${_profile}")
        endif()
        add_custom_command(
          OUTPUT  "${_out}"
          COMMAND "${_FXC}"
                  /nologo
                  /T "${_profile}"
                  /E "${_entry}"
                  $<$<CONFIG:Debug>:/Zi /Od>
                  $<$<CONFIG:RelWithDebInfo>:/Zi /O3>
                  $<$<CONFIG:MinSizeRel>:/O1>
                  $<$<CONFIG:Release>:/O3>
                  ${_def_args}
                  ${_inc_args}
                  /Fo "${_out}"
                  "${_abs}"
          MAIN_DEPENDENCY "${_abs}"
          DEPENDS "${CRS_MANIFEST}"
          VERBATIM
          COMMENT "FXC ${_profile} ${_name}.hlsl → ${_out}"
        )
      endif()

      list(APPEND _outputs "${_out}")
    endforeach()
  endif()

  add_custom_target(${CRS_TARGET}_shaders DEPENDS ${_outputs})
  add_dependencies(${CRS_TARGET} ${CRS_TARGET}_shaders)
endfunction()

# Install helper (unchanged)
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
