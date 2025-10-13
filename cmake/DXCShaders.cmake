# cmake/DxcShaders.cmake
# Windows-only DXC helpers to compile HLSL -> DXIL at build time.

if(NOT WIN32)
  return()
endif()

include(CMakeParseArguments)

# --- Locate dxc.exe (prefers vcpkg's directx-dxc tool) -----------------------
function(dxc_locate OUT_VAR)
  if(DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
    set(_dxc "${DIRECTX_DXC_TOOL}")
  else()
    # Fall back to PATH / common install locations:
    find_program(_dxc NAMES dxc.exe dxc PATHS
      # VS and Windows SDK typical tool folders are searched via PATH as well
      )
  endif()

  if(NOT _dxc)
    message(FATAL_ERROR "DXC not found. Install vcpkg 'directx-dxc' as a host tool or add dxc to PATH.")
  endif()

  set(${OUT_VAR} "${_dxc}" PARENT_SCOPE)
endfunction()

# --- Map stage to a default Shader Model profile (can be overridden) ----------
function(dxc_default_profile STAGE OUT_VAR)
  string(TOLOWER "${STAGE}" _s)
  if(_s STREQUAL "vs")
    set(_p "vs_6_7")
  elseif(_s STREQUAL "ps")
    set(_p "ps_6_7")
  elseif(_s STREQUAL "cs")
    set(_p "cs_6_7")
  elseif(_s STREQUAL "gs")
    set(_p "gs_6_7")
  elseif(_s STREQUAL "hs")
    set(_p "hs_6_7")
  elseif(_s STREQUAL "ds")
    set(_p "ds_6_7")
  elseif(_s STREQUAL "ms")
    set(_p "ms_6_7") # Mesh
  elseif(_s STREQUAL "as")
    set(_p "as_6_7") # Amplification
  elseif(_s STREQUAL "lib")
    set(_p "lib_6_7") # DXIL library (raytracing, work graphs, etc.)
  else()
    message(FATAL_ERROR "Unknown shader stage '${STAGE}'.")
  endif()
  set(${OUT_VAR} "${_p}" PARENT_SCOPE)
endfunction()

# --- Compile a single shader --------------------------------------------------
# Usage:
#   dxc_compile_shader(
#     OUT out_list_var
#     FILE <path/to/shader.hlsl>
#     ENTRY <VSMain|PSMain|...>
#     STAGE <vs|ps|cs|gs|hs|ds|ms|as|lib>
#     [PROFILE <vs_6_7|...>]
#     [DEFINES FOO=1 BAR=2 ...]
#     [INCLUDES dir1 dir2 ...]
#     [ARGS -enable-16bit-types ...]   # extra raw dxc args if needed
#   )
function(dxc_compile_shader)
  set(options)
  set(oneValueArgs OUT FILE ENTRY STAGE PROFILE)
  set(multiValueArgs DEFINES INCLUDES ARGS)
  cmake_parse_arguments(DCX "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT DCX_OUT OR NOT DCX_FILE OR NOT DCX_ENTRY OR NOT DCX_STAGE)
    message(FATAL_ERROR "dxc_compile_shader: OUT, FILE, ENTRY, and STAGE are required.")
  endif()

  get_filename_component(_abs "${DCX_FILE}" ABSOLUTE)
  if(NOT EXISTS "${_abs}")
    message(FATAL_ERROR "Shader source not found: ${_abs}")
  endif()
  get_filename_component(_namewe "${_abs}" NAME_WE)
  string(TOLOWER "${DCX_STAGE}" _stage_lc)

  # Resolve dxc and profile
  dxc_locate(DXC_EXE)
  if(DCX_PROFILE)
    set(_profile "${DCX_PROFILE}")
  else()
    dxc_default_profile("${DCX_STAGE}" _profile)
  endif()

  # Output directory and files
  set(_outdir "${CMAKE_BINARY_DIR}/shaders/${_stage_lc}")
  set(_dxil   "${_outdir}/${_namewe}.${_stage_lc}.dxil")
  file(MAKE_DIRECTORY "${_outdir}")

  # Compose include/define args
  set(_inc_args "")
  foreach(_i IN LISTS DCX_INCLUDES)
    list(APPEND _inc_args -I "${_i}")
  endforeach()

  set(_def_args "")
  foreach(_d IN LISTS DCX_DEFINES)
    list(APPEND _def_args -D "${_d}")
  endforeach()

  # Config-conditional flags:
  #  Debug / RelWithDebInfo: embed debug info to aid PIX/Nsight
  #  Release: strip debug + reflection to shrink blobs
  set(_cfg_args
    $<$<CONFIG:Debug>:-Zi;-Qembed_debug;-O0>
    $<$<CONFIG:RelWithDebInfo>:-Zi;-Qembed_debug;-O3>
    $<$<CONFIG:Release>:-O3;-Qstrip_debug;-Qstrip_reflect>
  )

  add_custom_command(
    OUTPUT "${_dxil}"
    COMMAND "${DXC_EXE}"
            -nologo
            -T "${_profile}"
            -E "${DCX_ENTRY}"
            ${_inc_args}
            ${_def_args}
            ${_cfg_args}
            ${DCX_ARGS}
            -Fo "${_dxil}"
            "${_abs}"
    DEPENDS "${_abs}"
    COMMENT "DXC ${_profile} ${_namewe}.${_stage_lc} -> ${_dxil}"
    VERBATIM
  )

  # Return generated file
  set(${DCX_OUT} "${${DCX_OUT}};${_dxil}" PARENT_SCOPE)
endfunction()

# --- Compile a standalone root signature from HLSL text ----------------------
#   dxc_compile_rootsig(OUT out_var FILE RootSig.hlsl ENTRY MyRootSig VERSION 1_1)
function(dxc_compile_rootsig)
  set(options)
  set(oneValueArgs OUT FILE ENTRY VERSION)
  set(multiValueArgs)
  cmake_parse_arguments(RS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT RS_OUT OR NOT RS_FILE OR NOT RS_ENTRY)
    message(FATAL_ERROR "dxc_compile_rootsig: OUT, FILE, and ENTRY are required.")
  endif()

  set(_ver "${RS_VERSION}")
  if(NOT _ver)
    set(_ver "1_1")
  endif()

  get_filename_component(_abs "${RS_FILE}" ABSOLUTE)
  get_filename_component(_namewe "${_abs}" NAME_WE)
  set(_outdir "${CMAKE_BINARY_DIR}/shaders/rootsig")
  file(MAKE_DIRECTORY "${_outdir}")
  set(_bin "${_outdir}/${_namewe}.rootsig${_ver}.bin")

  dxc_locate(DXC_EXE)
  add_custom_command(
    OUTPUT "${_bin}"
    COMMAND "${DXC_EXE}" -nologo
            -T "rootsig_${_ver}"
            -E "${RS_ENTRY}"
            -Fo "${_bin}"
            "${_abs}"
    DEPENDS "${_abs}"
    COMMENT "DXC rootsig_${_ver} ${_namewe} -> ${_bin}"
    VERBATIM
  )
  set(${RS_OUT} "${${RS_OUT}};${_bin}" PARENT_SCOPE)
endfunction()
