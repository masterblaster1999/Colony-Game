# cmake/shaders.cmake — HLSL compile helpers for DXC (preferred for SM6+) and FXC (fallback for SM5.x)
# Notes:
# * We prefer DXC for SM6+; FXC remains for legacy SM5.x pipelines.
# * On Windows runners, %WindowsSdkDir% and %VCToolsInstallDir% are set by
#   Developer Command Prompt (or actions that emulate it).

# ---------------------------
# DXC discovery (preferred)
# ---------------------------
# Prefer a caller-provided DXC path (e.g., from vcpkg/CI), else search VS/SDK locations.
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
if(DXC_EXE)
  message(STATUS "Using DXC: ${DXC_EXE}")
endif()

# ---------------------------
# FXC discovery (robust)
# ---------------------------
# Allow explicit override from CI or local machines.
set(FXC_PATH "" CACHE FILEPATH "Path to fxc.exe (optional)")

set(_FXC_CANDIDATE "")
if(FXC_PATH AND EXISTS "${FXC_PATH}")
  set(_FXC_CANDIDATE "${FXC_PATH}")
else()
  # Probe typical Windows SDK locations (10/11) for x64 fxc.exe
  file(GLOB _fxc_glob_candidates
       "$ENV{ProgramFiles(x86)}/Windows Kits/10/bin/*/x64/fxc.exe"
       "$ENV{ProgramFiles}/Windows Kits/10/bin/*/x64/fxc.exe"
       "$ENV{ProgramFiles(x86)}/Windows Kits/11/bin/*/x64/fxc.exe"
       "$ENV{ProgramFiles}/Windows Kits/11/bin/*/x64/fxc.exe")
  if(_fxc_glob_candidates)
    list(SORT _fxc_glob_candidates DESCENDING)
    list(GET _fxc_glob_candidates 0 _FXC_CANDIDATE)
  else()
    # Fallback to environment-driven search
    find_program(FXC_EXE fxc
      HINTS
        "$ENV{WindowsSdkDir}/bin/x64"
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64")
    if(FXC_EXE)
      set(_FXC_CANDIDATE "${FXC_EXE}")
    endif()
  endif()
endif()

set(FXC_EXE "${_FXC_CANDIDATE}")
if(FXC_EXE)
  message(STATUS "Using FXC: ${FXC_EXE}")
endif()

# --------------------------------------------
# cg_add_hlsl
# --------------------------------------------
# cg_add_hlsl(<OUT_DIR>
#   SRC <file.hlsl> ENTRY <name> TARGET_PROFILE <vs_6_7|ps_5_0|...>
#   [INCLUDES <dir1;dir2;...>] [DEFINES <MACRO;MACRO=VALUE;...>])
#
# Behavior:
#   * Shader Model 6.x profiles are compiled with DXC (required).
#   * Shader Model 5.x profiles are compiled with FXC (required).
#   * FXC invocations are logged to <OUT_DIR>/<ENTRY>.log with stdout+stderr,
#     and the log is printed to the build output so CI shows the actual error text.
function(cg_add_hlsl OUT_DIR)
  set(options)
  set(oneValueArgs SRC ENTRY TARGET_PROFILE)
  set(multiValueArgs INCLUDES DEFINES)
  cmake_parse_arguments(CG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CG_SRC OR NOT CG_ENTRY OR NOT CG_TARGET_PROFILE)
    message(FATAL_ERROR "cg_add_hlsl: require SRC, ENTRY, TARGET_PROFILE")
  endif()

  # Build -I / -D args for DXC (list form)
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

  # Build /I /D arguments for FXC (string form so we can place them inside 'cmd /c "..."')
  set(_fxc_inc_str "")
  foreach(_inc IN LISTS CG_INCLUDES)
    if(_inc)
      string(APPEND _fxc_inc_str " /I \"${_inc}\"")
    endif()
  endforeach()
  set(_fxc_def_str "")
  foreach(_def IN LISTS CG_DEFINES)
    if(_def)
      # fxc expects /DNAME[=VALUE] without quotes (unless VALUE has spaces)
      string(APPEND _fxc_def_str " /D ${_def}")
    endif()
  endforeach()

  # Outputs
  set(_out "${OUT_DIR}/${CG_ENTRY}.cso")
  set(_log "${OUT_DIR}/${CG_ENTRY}.log")

  # Decide compiler based on target profile: SM6.x -> DXC, else FXC
  if(CG_TARGET_PROFILE MATCHES "_6(_|$)")
    if(NOT DXC_EXE)
      message(FATAL_ERROR "cg_add_hlsl: DXC is required for Shader Model 6 targets (profile='${CG_TARGET_PROFILE}') but was not found.")
    endif()

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
      COMMENT "DXC ${CG_TARGET_PROFILE} ${CG_SRC} -> ${_out}"
      VERBATIM
      COMMAND_EXPAND_LISTS)

  else() # Assume SM5.x or earlier -> FXC
    if(NOT FXC_EXE)
      message(FATAL_ERROR "cg_add_hlsl: FXC is required for Shader Model 5 targets (profile='${CG_TARGET_PROFILE}') but was not found. Set FXC_PATH or install the Windows SDK.")
    endif()

    # Use cmd.exe for I/O redirection to a per-shader log, then print the log.
    # Note: All arguments for fxc are inside the quoted string to keep redirection working.
    add_custom_command(
      OUTPUT "${_out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${OUT_DIR}"
      COMMAND cmd /c "\"${FXC_EXE}\" /nologo
                      /T \"${CG_TARGET_PROFILE}\"
                      /E \"${CG_ENTRY}\"
                      ${_fxc_inc_str} ${_fxc_def_str}
                      /Fo \"${_out}\"
                      \"${CG_SRC}\" > \"${_log}\" 2>&1"
      COMMAND cmd /c "type \"${_log}\""
      DEPENDS "${CG_SRC}"
      COMMENT "FXC ${CG_TARGET_PROFILE} ${CG_SRC} -> ${_out} (see ${_log})"
      VERBATIM
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()
