# =========================================================================================
# Colony-Game — HLSL offline compilation helper (Windows / Visual Studio generators)
#
# This script compiles .hlsl files to .cso blobs using:
#   * FXC (Windows SDK) for Shader Model 2 .. 5.1  -> DXBC (D3D11-friendly)
#   * DXC (DirectXShaderCompiler) for Shader Model 6+ -> DXIL (D3D12)
#
# Why this split? Microsoft’s guidance: FXC is the compiler for SM 2–5.1; DXC is for SM 6+.
# Visual Studio’s HLSL integration does the same under the hood. 
#
# Usage:
#   include(src/shaders.cmake)
#
#   # (Optional) Global search paths and defines for all shaders:
#   list(APPEND COLONY_HLSL_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders/includes")
#   list(APPEND COLONY_HLSL_DEFINES       "EXAMPLE_DEFINE=1")
#
#   # Register shaders (TYPE/ENTRY/MODEL optional; TYPE inferred from file suffix: _vs,_ps,_cs,_gs,_hs,_ds)
#   colony_add_shader(SOURCE "${CMAKE_SOURCE_DIR}/shaders/fullscreen_vs.hlsl")                 # -> vs_5_0, entry=main
#   colony_add_shader(SOURCE "${CMAKE_SOURCE_DIR}/shaders/lighting_ps.hlsl" ENTRY "main")      # -> ps_5_0
#   # Example SM6 (DXC): colony_add_shader(SOURCE "foo_cs.hlsl" MODEL "6_6" TYPE "CS")
#
#   # Materialize commands & target (creates a custom target named 'compile_shaders'):
#   colony_create_shader_target(compile_shaders)
#
#   # IMPORTANT: Wire your EXE to the shader target from the SAME directory that creates the EXE:
#   # add_dependencies(ColonyGame compile_shaders)
#
# Notes:
#   - Outputs are placed in:  <build>/shaders/<Configuration>/<name>.cso
#   - Debug:   embeds/produces debug info (PDB);   Release: strips debug info and optimizes.
#   - For D3D11 projects, prefer SM 5.x (FXC). D3D11 does not consume DXIL directly.
# =========================================================================================

include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR "src/shaders.cmake is Windows-only (expects VS generator + Windows SDK).")
endif()

# ---------------------------------------------
# Helpers to hold registered shaders (global)
# Each item is packed as:  PATH::TYPE::ENTRY::MODEL
# ---------------------------------------------
set(_COLONY_SHADER_ITEMS "" CACHE INTERNAL "Packed list of registered HLSL shaders")

# Defaults (can be overridden by the caller before registering shaders)
set(COLONY_HLSL_DEFAULT_MODEL "5_0" CACHE STRING "Default shader model if not specified (e.g., 5_0, 6_6)")
set(COLONY_HLSL_DEFAULT_ENTRY "main" CACHE STRING "Default entry point if not specified")

# Optional global flags users can set before creating the target
set(COLONY_HLSL_INCLUDE_DIRS "" CACHE STRING "List of include directories for HLSL")
set(COLONY_HLSL_DEFINES       "" CACHE STRING "List of preprocessor defines for HLSL (NAME or NAME=VALUE)")

# ---------------------------------------------
# Detect shader TYPE from filename (_vs,_ps,_cs,_gs,_hs,_ds)
# Compose profile string TYPE_MODEL (e.g., vs_5_0).
# ---------------------------------------------
function(_colony_hlsl_guess_type_and_profile in_path in_default_model out_type out_profile)
  get_filename_component(_name "${in_path}" NAME_WE)
  set(_type "")
  if(_name MATCHES ".*_vs$")
    set(_type "vs")
  elseif(_name MATCHES ".*_ps$")
    set(_type "ps")
  elseif(_name MATCHES ".*_cs$")
    set(_type "cs")
  elseif(_name MATCHES ".*_gs$")
    set(_type "gs")
  elseif(_name MATCHES ".*_hs$")
    set(_type "hs")
  elseif(_name MATCHES ".*_ds$")
    set(_type "ds")
  endif()

  if(_type STREQUAL "")
    # Default to pixel shader if suffix isn't present
    set(_type "ps")
  endif()

  if(NOT in_default_model)
    set(in_default_model "5_0")
  endif()

  set(${out_type}    "${_type}"                PARENT_SCOPE)
  set(${out_profile} "${_type}_${in_default_model}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------
# Register a single shader with optional metadata
# colony_add_shader(SOURCE <path> [TYPE VS|PS|CS|GS|HS|DS] [ENTRY <name>] [MODEL <m.m>])
# ---------------------------------------------
function(colony_add_shader)
  set(options "")
  set(oneValueArgs SOURCE TYPE ENTRY MODEL)
  set(multiValueArgs "")
  cmake_parse_arguments(CSH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CSH_SOURCE)
    message(FATAL_ERROR "colony_add_shader: SOURCE is required")
  endif()

  # Normalize TYPE to lowercase short form if provided
  set(_type "")
  if(CSH_TYPE)
    string(TOLOWER "${CSH_TYPE}" _type)
    if(_type STREQUAL "vertex")  ; set(_type "vs") ; endif()
    if(_type STREQUAL "pixel")   ; set(_type "ps") ; endif()
    if(_type STREQUAL "compute") ; set(_type "cs") ; endif()
    if(_type STREQUAL "geometry"); set(_type "gs") ; endif()
    if(_type STREQUAL "hull")    ; set(_type "hs") ; endif()
    if(_type STREQUAL "domain")  ; set(_type "ds") ; endif()
    if(NOT _type MATCHES "^(vs|ps|cs|gs|hs|ds)$")
      message(FATAL_ERROR "colony_add_shader: TYPE must be one of VS|PS|CS|GS|HS|DS (or long names).")
    endif()
  endif()

  set(_entry "${CSH_ENTRY}")
  if(NOT _entry)
    set(_entry "${COLONY_HLSL_DEFAULT_ENTRY}")
  endif()

  set(_model "${CSH_MODEL}")
  if(NOT _model)
    set(_model "${COLONY_HLSL_DEFAULT_MODEL}")
  endif()

  set(_profile "")
  if(_type)
    set(_profile "${_type}_${_model}")
  else()
    _colony_hlsl_guess_type_and_profile("${CSH_SOURCE}" "${_model}" _type _profile)
  endif()

  # Pack and store
  set(_packed "${CSH_SOURCE}::${_type}::${_entry}::${_model}")
  set(_COLONY_SHADER_ITEMS "${_COLONY_SHADER_ITEMS};${_packed}" CACHE INTERNAL "" FORCE)
endfunction()

# ---------------------------------------------
# Try to locate FXC and DXC
#   - FXC comes with the Windows SDK   (fxc.exe)
#   - DXC comes from vcpkg (directx-dxc) or Windows SDK (dxc.exe)
# ---------------------------------------------
function(_colony_locate_tools out_fxc out_dxc)
  # DXC
  set(_DXC_EXE "")
  if(TARGET Microsoft::DirectXShaderCompiler)
    # If vcpkg made an imported tool target available
    get_target_property(_dxc_loc Microsoft::DirectXShaderCompiler IMPORTED_LOCATION)
    if(_dxc_loc)
      set(_DXC_EXE "${_dxc_loc}")
    endif()
  endif()
  if(NOT _DXC_EXE AND DEFINED DIRECTX_DXC_TOOL)
    set(_DXC_EXE "${DIRECTX_DXC_TOOL}")
  endif()
  if(NOT _DXC_EXE)
    # Try common locations (vcpkg / Windows SDK)
    find_program(_DXC_EXE NAMES dxc dxc.exe
      PATHS
        "$ENV{VCPKG_ROOT}/installed/x64-windows/tools/directx-dxc"
        "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/x64"
      NO_DEFAULT_PATH
    )
    if(NOT _DXC_EXE)
      find_program(_DXC_EXE NAMES dxc.exe dxc)
    endif()
  endif()

  # FXC
  set(_FXC_EXE "")
  # Build a hint list from SDK env and known versions
  set(_fxc_hint_roots "$ENV{WindowsSdkDir}" "C:/Program Files (x86)/Windows Kits/10")
  set(_fxc_hint_vers  "$ENV{WindowsSDKVersion}" "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
                      "10.0.26100.0" "10.0.22621.0" "10.0.22000.0" "10.0.19041.0" "10.0.18362.0")
  set(_fxc_paths "")
  foreach(_r IN LISTS _fxc_hint_roots)
    foreach(_v IN LISTS _fxc_hint_vers)
      if(_r AND _v)
        list(APPEND _fxc_paths "${_r}/bin/${_v}/x64")
        list(APPEND _fxc_paths "${_r}/bin/${_v}/x86")
      endif()
    endforeach()
  endforeach()
  find_program(_FXC_EXE NAMES fxc fxc.exe PATHS ${_fxc_paths})
  if(NOT _FXC_EXE)
    find_program(_FXC_EXE NAMES fxc.exe fxc)
  endif()

  set(${out_fxc} "${_FXC_EXE}" PARENT_SCOPE)
  set(${out_dxc} "${_DXC_EXE}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------
# Create the custom commands and a utility target that depends on them
# colony_create_shader_target(<name>)
#   - Emits outputs to: <build>/shaders/<Configuration>/<name>.cso
#   - Exposes COLONY_SHADER_OUTPUTS (list) for optional use by caller.
# ---------------------------------------------
function(colony_create_shader_target target_name)
  if("${target_name}" STREQUAL "")
    set(target_name "compile_shaders")
  endif()

  if(_COLONY_SHADER_ITEMS STREQUAL "")
    message(WARNING "colony_create_shader_target: No shaders registered via colony_add_shader().")
    add_custom_target(${target_name})
    return()
  endif()

  # Locate tools
  _colony_locate_tools(_FXC_EXE _DXC_EXE)

  # Output directory pattern (multi-config aware via CMAKE_CFG_INTDIR: '.', or '$(Configuration)')
  set(_OUT_ROOT "${CMAKE_BINARY_DIR}/shaders/${CMAKE_CFG_INTDIR}")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders")

  # Build include/define flags for both compilers
  set(_FXC_INC_FLAGS "")
  set(_DXC_INC_FLAGS "")
  foreach(_inc IN LISTS COLONY_HLSL_INCLUDE_DIRS)
    list(APPEND _FXC_INC_FLAGS "/I" "${_inc}")
    list(APPEND _DXC_INC_FLAGS "-I" "${_inc}")
  endforeach()

  set(_FXC_DEF_FLAGS "")
  set(_DXC_DEF_FLAGS "")
  foreach(_def IN LISTS COLONY_HLSL_DEFINES)
    # accept NAME or NAME=VALUE
    if(_def MATCHES "^[A-Za-z_][A-Za-z0-9_]*=")
      set(_pair "${_def}")
    else()
      set(_pair "${_def}=1")
    endif()
    list(APPEND _FXC_DEF_FLAGS "/D" "${_pair}")
    list(APPEND _DXC_DEF_FLAGS "-D" "${_pair}")
  endforeach()

  set(_ALL_OUTPUTS "")
  foreach(_packed IN LISTS _COLONY_SHADER_ITEMS)
    # Unpack: PATH::TYPE::ENTRY::MODEL
    string(REPLACE "::" ";" _parts "${_packed}")
    list(GET _parts 0 _SRC)
    list(GET _parts 1 _TYPE)
    list(GET _parts 2 _ENTRY)
    list(GET _parts 3 _MODEL)

    if(NOT EXISTS "${_SRC}")
      message(FATAL_ERROR "HLSL source not found: ${_SRC}")
    endif()

    get_filename_component(_NAME "${_SRC}" NAME_WE)
    set(_OUT_DIR "${_OUT_ROOT}")
    set(_OUT_CSO "${_OUT_DIR}/${_NAME}.cso")
    set(_OUT_PDB "${_OUT_DIR}/${_NAME}.pdb")

    # Determine whether this is SM6+ (DXC) or SM5.x and below (FXC)
    # _MODEL is like '5_0' or '6_6'
    string(REPLACE "_" ";" _model_parts "${_MODEL}")
    list(GET _model_parts 0 _MAJOR)

    if(_MAJOR GREATER_EQUAL 6)
      if(NOT _DXC_EXE)
        message(FATAL_ERROR
          "DXC (dxc.exe) not found but a SM${_MODEL} shader was registered:\n"
          "  ${_SRC}\n"
          "Install the DXC toolchain (e.g., vcpkg 'directx-dxc') or ensure dxc.exe is on PATH.")
      endif()

      # DXC command
      add_custom_command(
        OUTPUT  "${_OUT_CSO}"
        BYPRODUCTS "${_OUT_PDB}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_OUT_DIR}"
        COMMAND "${_DXC_EXE}"
                -nologo
                -T "${_TYPE}_${_MODEL}"
                -E "${_ENTRY}"
                $<$<CONFIG:Debug>:-Zi -Qembed_debug -Fd "${_OUT_PDB}">
                $<$<CONFIG:Release>:-O3 -Qstrip_debug>
                ${_DXC_INC_FLAGS}
                ${_DXC_DEF_FLAGS}
                -Fo "${_OUT_CSO}" "${_SRC}"
        DEPENDS "${_SRC}"
        COMMENT "DXC  ${_TYPE}_${_MODEL}  ${_NAME}.hlsl  ->  ${_NAME}.cso"
        VERBATIM
      )
    else()
      if(NOT _FXC_EXE)
        message(FATAL_ERROR
          "FXC (fxc.exe) not found but an SM${_MODEL} shader was registered:\n"
          "  ${_SRC}\n"
          "Install the Windows 10/11 SDK (fxc.exe) or switch this shader to SM6+ for DXC.")
      endif()

      # FXC command (O1/O2/O3 behave the same per docs; we use /O3 and strip debug in Release)
      add_custom_command(
        OUTPUT  "${_OUT_CSO}"
        BYPRODUCTS "${_OUT_PDB}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_OUT_DIR}"
        COMMAND "${_FXC_EXE}"
                /nologo
                /T "${_TYPE}_${_MODEL}"
                /E "${_ENTRY}"
                $<$<CONFIG:Debug>:/Zi /Od /Fd "${_OUT_PDB}">
                $<$<CONFIG:Release>:/O3 /Qstrip_debug>
                ${_FXC_INC_FLAGS}
                ${_FXC_DEF_FLAGS}
                /Fo "${_OUT_CSO}" "${_SRC}"
        DEPENDS "${_SRC}"
        COMMENT "FXC  ${_TYPE}_${_MODEL}  ${_NAME}.hlsl  ->  ${_NAME}.cso"
        VERBATIM
      )
    endif()

    list(APPEND _ALL_OUTPUTS "${_OUT_CSO}")
  endforeach()

  add_custom_target(${target_name} ALL DEPENDS ${_ALL_OUTPUTS})
  set(COLONY_SHADER_OUTPUTS "${_ALL_OUTPUTS}" PARENT_SCOPE)
endfunction()
