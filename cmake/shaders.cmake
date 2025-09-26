# cmake/shaders.cmake — HLSL compile helpers for DXC (preferred) and FXC (fallback)
# Notes:
# * We prefer DXC for SM6+; FXC remains for legacy SM5.x pipelines. 
# * On Windows runners, %WindowsSdkDir% and %VCToolsInstallDir% are set by 
#   Developer Command Prompt (or actions that emulate it). :contentReference[oaicite:2]{index=2}

# Prefer the vcpkg-provided DXC if available, else search VS/SDK locations.
if(DEFINED DIRECTX_DXC_TOOL AND EXISTS "${DIRECTX_DXC_TOOL}")
  set(DXC_EXE "${DIRECTX_DXC_TOOL}")
else()
  find_program(DXC_EXE dxc
    HINTS
      "$ENV{VCToolsInstallDir}/bin/Hostx64/x64"
      "$ENV{WindowsSdkDir}/bin/x64"
      "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
  )
endif()

# FXC (legacy) — often found under Windows SDK bin\<version>\x64
find_program(FXC_EXE fxc
  HINTS
    "$ENV{WindowsSdkDir}/bin/x64"
    "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
)

# cg_add_hlsl(<OUT_DIR> SRC <file.hlsl> ENTRY <name> TARGET_PROFILE <vs_6_7|ps_5_0|...>
#             [INCLUDES <dir1;dir2;...>] [DEFINES <MACRO;MACRO=VALUE;...>])
function(cg_add_hlsl OUT_DIR)
  set(options)
  set(oneValueArgs SRC ENTRY TARGET_PROFILE)
  set(multiValueArgs INCLUDES DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SRC OR NOT CG_ENTRY OR NOT CG_TARGET_PROFILE)
    message(FATAL_ERROR "cg_add_hlsl: require SRC, ENTRY, TARGET_PROFILE")
  endif()

  # Build -I / -D args for DXC and /I /D for FXC carefully (one per item)
  set(_dxc_inc_args)
  foreach(_inc IN LISTS CG_INCLUDES)
    if(_inc)
      list(APPEND _dxc_inc_args -I "${_inc}")
    endif()
  endforeach()

  set(_dxc_def_args)
  foreach(_def IN LISTS CG_DEFINES)
    if(_def)
      list(APPEND _dxc_def_args -D "${_def}")
    endif()
  endforeach()

  set(_fxc_inc_args)
  foreach(_inc IN LISTS CG_INCLUDES)
    if(_inc)
      list(APPEND _fxc_inc_args /I "${_inc}")
    endif()
  endforeach()

  set(_fxc_def_args)
  foreach(_def IN LISTS CG_DEFINES)
    if(_def)
      list(APPEND _fxc_def_args /D "${_def}")
    endif()
  endforeach()

  set(_out "${OUT_DIR}/${CG_ENTRY}.cso")

  if(DXC_EXE)
    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
      COMMAND "${DXC_EXE}" -nologo
              -T "${CG_TARGET_PROFILE}"
              -E "${CG_ENTRY}"
              ${_dxc_inc_args} ${_dxc_def_args}
              -Fo "${_out}"
              "${CG_SRC}"
      DEPENDS "${CG_SRC}"
      COMMENT "DXC ${CG_SRC} -> ${_out}"
      VERBATIM
      COMMAND_EXPAND_LISTS)
  elseif(FXC_EXE)
    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
      COMMAND "${FXC_EXE}" /nologo
              /T "${CG_TARGET_PROFILE}"
              /E "${CG_ENTRY}"
              ${_fxc_inc_args} ${_fxc_def_args}
              /Fo "${_out}"
              "${CG_SRC}"
      DEPENDS "${CG_SRC}"
      COMMENT "FXC ${CG_SRC} -> ${_out}"
      VERBATIM
      COMMAND_EXPAND_LISTS)
  else()
    message(FATAL_ERROR "No HLSL compiler (DXC/FXC) found on this Windows machine.")
  endif()
endfunction()
