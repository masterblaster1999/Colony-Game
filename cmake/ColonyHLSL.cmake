# cmake/ColonyHLSL.cmake
# Windows-only HLSL helper that works with Visual Studio generator (native HLSL)
# and with other Windows generators (Ninja/NMake) via offline compilation.
#
# Usage:
#   colony_add_hlsl(<target>
#     [DIR <dir>]              # directory to scan for *.hlsl (recursive)
#     [SOURCES ...]            # explicit list of *.hlsl files (mutually exclusive with DIR)
#     [ENTRY <main>]           # shader entry point (one for this invocation; default: main)
#     [MODEL <5.0|5.1|6.0|...>]# shader model; default: 5.0 (D3D11-friendly DXBC)
#     [OUTDIR <path>]          # where to place compiled outputs; default: <binary dir>/shaders
#     [DEFINES ...]            # macro definitions (FOO or FOO=1)
#     [INCLUDES ...]           # include paths used by compiler
#     [COMPILER <AUTO|FXC|DXC>]# default AUTO: VS uses native; Ninja uses FXC; choose DXC explicitly
#     [EMIT <object|header|both>]   # outputs .cso/.dxil and/or C header; default: object
#     [VARIABLE_PREFIX <g_>]   # header variable name prefix when EMIT includes 'header'
#   )
#
# Notes:
# - Call this *after* add_executable/add_library for <target>.
# - We do NOT use 'add_custom_command(TARGET …)' anywhere; this avoids CMake's
#   same-directory constraint that causes "TARGET was not created in this directory" errors.
# - Visual Studio generator path uses CMake's VS shader properties:
#     VS_SHADER_TYPE / VS_SHADER_MODEL / VS_SHADER_ENTRYPOINT /
#     VS_SHADER_OBJECT_FILE_NAME / VS_SHADER_OUTPUT_HEADER_FILE /
#     VS_SHADER_ENABLE_DEBUG / VS_SHADER_DISABLE_OPTIMIZATIONS / VS_SHADER_FLAGS
# - Non-VS generators on Windows:
#   * FXC (default): produces DXBC .cso for D3D11.
#   * DXC (opt-in): produces DXIL; for SM6.x/D3D12 workflows.
# - Stage inference: supports *VS/*PS/*CS/*GS/*HS/*DS suffixes with or without '_' before them.
#
include_guard(GLOBAL)
include(CMakeParseArguments)

if(NOT WIN32)
  message(FATAL_ERROR "colony_add_hlsl: Windows-only helper.")
endif()

function(colony_add_hlsl target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "colony_add_hlsl: Target '${target}' does not exist.")
  endif()

  # ---- Parse args
  set(options)
  set(oneValueArgs DIR MODEL ENTRY OUTDIR COMPILER EMIT VARIABLE_PREFIX)
  set(multiValueArgs SOURCES DEFINES INCLUDES)
  cmake_parse_arguments(CAH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT CAH_SOURCES AND NOT CAH_DIR)
    message(FATAL_ERROR "colony_add_hlsl: Provide SOURCES or DIR.")
  endif()

  if(NOT CAH_ENTRY)
    set(CAH_ENTRY "main")
  endif()

  # D3D11-friendly default
  if(NOT CAH_MODEL)
    set(CAH_MODEL "5.0")
  endif()

  if(NOT CAH_OUTDIR)
    set(CAH_OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  endif()

  if(NOT CAH_COMPILER)
    set(CAH_COMPILER "AUTO")
  endif()
  string(TOUPPER "${CAH_COMPILER}" CAH_COMPILER)

  if(NOT CAH_EMIT)
    set(CAH_EMIT "object")
  endif()
  string(TOLOWER "${CAH_EMIT}" CAH_EMIT)

  if(NOT CAH_VARIABLE_PREFIX)
    set(CAH_VARIABLE_PREFIX "g_")
  endif()

  # Gather files
  set(HLSL_FILES)
  if(CAH_DIR)
    # Only compile .hlsl; .hlsli are includes and should not be separate compile units.
    file(GLOB_RECURSE HLSL_FILES CONFIGURE_DEPENDS "${CAH_DIR}/*.hlsl")
  else()
    set(HLSL_FILES ${CAH_SOURCES})
  endif()

  if(HLSL_FILES STREQUAL "")
    message(WARNING "colony_add_hlsl: No .hlsl sources found.")
    return()
  endif()

  # Utility: stage inference from file name (case-insensitive).
  # Matches both FooVS.hlsl and Foo_VS.hlsl (and similar for PS/CS/GS/HS/DS).
  function(_colony_guess_stage in out_var)
    get_filename_component(_base "${in}" NAME)
    string(TOUPPER "${_base}" _UP)
    set(stage "Pixel")
    if("${_UP}" MATCHES "(_|)VS\\.HLSL$")
      set(stage "Vertex")
    elseif("${_UP}" MATCHES "(_|)PS\\.HLSL$")
      set(stage "Pixel")
    elseif("${_UP}" MATCHES "(_|)CS\\.HLSL$")
      set(stage "Compute")
    elseif("${_UP}" MATCHES "(_|)GS\\.HLSL$")
      set(stage "Geometry")
    elseif("${_UP}" MATCHES "(_|)HS\\.HLSL$")
      set(stage "Hull")
    elseif("${_UP}" MATCHES "(_|)DS\\.HLSL$")
      set(stage "Domain")
    endif()
    set(${out_var} "${stage}" PARENT_SCOPE)
  endfunction()

  # Utility: FXC profile from stage+model (e.g., vs_5_0)
  function(_colony_fxc_profile stage model out_var)
    if(stage STREQUAL "Vertex")
      set(pfx "vs")
    elseif(stage STREQUAL "Pixel")
      set(pfx "ps")
    elseif(stage STREQUAL "Compute")
      set(pfx "cs")
    elseif(stage STREQUAL "Geometry")
      set(pfx "gs")
    elseif(stage STREQUAL "Hull")
      set(pfx "hs")
    elseif(stage STREQUAL "Domain")
      set(pfx "ds")
    else()
      set(pfx "ps")
    endif()
    string(REPLACE "." "_" _m "${model}")   # fxc expects underscores
    set(${out_var} "${pfx}_${_m}" PARENT_SCOPE)
  endfunction()

  # Utility: DXC profile from stage+model (e.g., vs_6_7)
  function(_colony_dxc_profile stage model out_var)
    string(REPLACE "." "_" _m "${model}")
    if(stage STREQUAL "Vertex")
      set(pfx "vs")
    elseif(stage STREQUAL "Pixel")
      set(pfx "ps")
    elseif(stage STREQUAL "Compute")
      set(pfx "cs")
    elseif(stage STREQUAL "Geometry")
      set(pfx "gs")
    elseif(stage STREQUAL "Hull")
      set(pfx "hs")
    elseif(stage STREQUAL "Domain")
      set(pfx "ds")
    else()
      set(pfx "ps")
    endif()
    set(${out_var} "${pfx}_${_m}" PARENT_SCOPE)
  endfunction()

  # Utility: find fxc.exe from Windows 10/11 SDK
  function(_colony_find_fxc OUT_VAR)
    # First, check PATH
    find_program(_fxc NAMES fxc)
    if(_fxc)
      set(${OUT_VAR} "${_fxc}" PARENT_SCOPE)
      return()
    endif()

    # Search common Windows SDK install roots
    set(_roots
      "$ENV{WindowsSdkDir}/bin"
      "C:/Program Files (x86)/Windows Kits/10/bin"
      "C:/Program Files/Windows Kits/10/bin"
    )

    set(_cands "")
    foreach(_r IN LISTS _roots)
      if(EXISTS "${_r}")
        file(GLOB _verdirs LIST_DIRECTORIES TRUE "${_r}/*")
        if(_verdirs)
          list(SORT _verdirs COMPARE STRING)
          list(REVERSE _verdirs)
          foreach(_vd IN LISTS _verdirs)
            if(EXISTS "${_vd}/x64/fxc.exe")
              list(APPEND _cands "${_vd}/x64/fxc.exe")
            endif()
            if(EXISTS "${_vd}/x86/fxc.exe")
              list(APPEND _cands "${_vd}/x86/fxc.exe")
            endif()
          endforeach()
        endif()
      endif()
    endforeach()

    if(_cands)
      list(GET _cands 0 _fxc_best)
      set(${OUT_VAR} "${_fxc_best}" PARENT_SCOPE)
      return()
    endif()

    set(${OUT_VAR} "" PARENT_SCOPE)
  endfunction()

  # Prepare output dirs
  file(MAKE_DIRECTORY "${CAH_OUTDIR}")
  file(MAKE_DIRECTORY "${CAH_OUTDIR}/objects")
  file(MAKE_DIRECTORY "${CAH_OUTDIR}/headers")

  # ---- Path A: Visual Studio generator → native HLSL (FXC for SM<=5.1; DXC for SM6.x)
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    # CMake source-file properties that drive the VS/MSBuild HLSL step:
    # VS_SHADER_TYPE / VS_SHADER_MODEL / VS_SHADER_ENTRYPOINT /
    # VS_SHADER_OBJECT_FILE_NAME / VS_SHADER_OUTPUT_HEADER_FILE /
    # VS_SHADER_ENABLE_DEBUG / VS_SHADER_DISABLE_OPTIMIZATIONS / VS_SHADER_FLAGS
    foreach(f IN LISTS HLSL_FILES)
      get_filename_component(_abs "${f}" ABSOLUTE)
      get_filename_component(_namewe "${f}" NAME_WE)

      # Sanitize for header variable names
      set(_var "${CAH_VARIABLE_PREFIX}${_namewe}")
      string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _var "${_var}")

      _colony_guess_stage("${f}" _stage)

      # Base properties
      set(_props
        VS_SHADER_TYPE "${_stage}"
        VS_SHADER_MODEL "${CAH_MODEL}"
        VS_SHADER_ENTRYPOINT "${CAH_ENTRY}"
        VS_SHADER_ENABLE_DEBUG "$<IF:$<CONFIG:Debug>,true,false>"
        VS_SHADER_DISABLE_OPTIMIZATIONS "$<IF:$<CONFIG:Debug>,true,false>"
      )

      # Emit object/header as requested
      if(CAH_EMIT STREQUAL "object" OR CAH_EMIT STREQUAL "both")
        list(APPEND _props
          VS_SHADER_OBJECT_FILE_NAME "${CAH_OUTDIR}/objects/${_namewe}.cso")
      endif()

      if(CAH_EMIT STREQUAL "header" OR CAH_EMIT STREQUAL "both")
        list(APPEND _props
          VS_SHADER_OUTPUT_HEADER_FILE "${CAH_OUTDIR}/headers/${_namewe}.h"
          VS_SHADER_VARIABLE_NAME       "${_var}")
      endif()

      # Defines and include paths via VS_SHADER_FLAGS
      set(_fxcflags "")
      if(CAH_DEFINES)
        foreach(_d IN LISTS CAH_DEFINES)
          list(APPEND _fxcflags "/D${_d}")
        endforeach()
      endif()
      if(CAH_INCLUDES)
        foreach(_i IN LISTS CAH_INCLUDES)
          list(APPEND _fxcflags "/I\"${_i}\"")
        endforeach()
      endif()
      if(_fxcflags)
        string(REPLACE ";" " " _fxcflags "${_fxcflags}")
        set_source_files_properties("${_abs}" PROPERTIES VS_SHADER_FLAGS "${_fxcflags}")
      endif()

      set_source_files_properties("${_abs}" PROPERTIES ${_props})
      target_sources(${target} PRIVATE "${_abs}")
    endforeach()

    if(CAH_EMIT STREQUAL "header" OR CAH_EMIT STREQUAL "both")
      target_include_directories(${target} PRIVATE "${CAH_OUTDIR}/headers")
    endif()

    # Optional: group in the IDE
    if(CAH_DIR)
      source_group(TREE "${CAH_DIR}" FILES ${HLSL_FILES})
    else()
      source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HLSL_FILES})
    endif()

    return()
  endif()

  # ---- Path B: Non-VS Windows generators → offline compiler
  # Decide compiler
  set(_use_dxc FALSE)
  if(CAH_COMPILER STREQUAL "DXC")
    set(_use_dxc TRUE)
  elseif(CAH_COMPILER STREQUAL "AUTO")
    # D3D11-friendly default: FXC for SM <= 5.1
    if(CAH_MODEL MATCHES "^6\\.")
      message(WARNING "colony_add_hlsl: MODEL=${CAH_MODEL} implies DXC/DXIL. "
                      "D3D11 cannot consume DXIL. Use DX12 or switch MODEL to 5.0/5.1 for D3D11.")
      set(_use_dxc TRUE)
    else()
      set(_use_dxc FALSE)
    endif()
  else()
    set(_use_dxc FALSE) # FXC
  endif()

  set(_outputs)

  if(NOT _use_dxc)
    # ---------- FXC (DXBC) ----------
    _colony_find_fxc(_FXC_EXE)
    if(NOT _FXC_EXE)
      message(FATAL_ERROR
        "colony_add_hlsl: Could not find fxc.exe in Windows SDK. "
        "Install the Windows 10/11 SDK or use the Visual Studio generator.")
    endif()

    foreach(f IN LISTS HLSL_FILES)
      get_filename_component(_abs "${f}" ABSOLUTE)
      get_filename_component(_namewe "${f}" NAME_WE)

      # Sanitize variable name for /Vn (header mode)
      set(_var "${CAH_VARIABLE_PREFIX}${_namewe}")
      string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _var "${_var}")

      _colony_guess_stage("${f}" _stage)
      _colony_fxc_profile("${_stage}" "${CAH_MODEL}" _profile)

      set(_obj "${CAH_OUTDIR}/objects/${_namewe}.cso")
      set(_hdr "${CAH_OUTDIR}/headers/${_namewe}.h")
      set(_cmd "${_FXC_EXE}" /nologo /T "${_profile}" /E "${CAH_ENTRY}")

      # Includes/defines
      foreach(_inc IN LISTS CAH_INCLUDES)
        list(APPEND _cmd /I "${_inc}")
      endforeach()
      foreach(_def IN LISTS CAH_DEFINES)
        list(APPEND _cmd /D "${_def}")
      endforeach()

      # Outputs & bookkeeping
      set(_outputs_this_rule "")
      if(CAH_EMIT STREQUAL "object" OR CAH_EMIT STREQUAL "both")
        list(APPEND _cmd /Fo "${_obj}")
        list(APPEND _outputs_this_rule "${_obj}")
        list(APPEND _outputs "${_obj}")
      endif()
      if(CAH_EMIT STREQUAL "header" OR CAH_EMIT STREQUAL "both")
        list(APPEND _cmd /Fh "${_hdr}" /Vn "${_var}")
        list(APPEND _outputs_this_rule "${_hdr}")
        list(APPEND _outputs "${_hdr}")
      endif()

      list(APPEND _cmd "${_abs}")

      add_custom_command(
        OUTPUT  ${_outputs_this_rule}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CAH_OUTDIR}/objects"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CAH_OUTDIR}/headers"
        COMMAND ${_cmd}
        DEPENDS "${_abs}"
        COMMENT "FXC ${_profile} ${f}"
        VERBATIM
      )
    endforeach()

  else()
    # ---------- DXC (DXIL) ----------
    # Prefer vcpkg's directx-dxc tool if available.
    if(NOT DEFINED DIRECTX_DXC_TOOL)
      find_program(DIRECTX_DXC_TOOL NAMES dxc)
    endif()
    if(NOT DIRECTX_DXC_TOOL)
      message(FATAL_ERROR
        "colony_add_hlsl: dxc.exe not found. Install 'directx-dxc' via vcpkg or add to PATH.")
    endif()

    foreach(f IN LISTS HLSL_FILES)
      get_filename_component(_abs "${f}" ABSOLUTE)
      get_filename_component(_namewe "${f}" NAME_WE)

      # Sanitize variable name for -Vn (header mode)
      set(_var "${CAH_VARIABLE_PREFIX}${_namewe}")
      string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _var "${_var}")

      _colony_guess_stage("${f}" _stage)
      _colony_dxc_profile("${_stage}" "${CAH_MODEL}" _profile)

      set(_obj "${CAH_OUTDIR}/objects/${_namewe}.dxil")
      set(_hdr "${CAH_OUTDIR}/headers/${_namewe}.h")
      set(_cmd "${DIRECTX_DXC_TOOL}" -nologo -T "${_profile}" -E "${CAH_ENTRY}")

      # Includes/defines
      foreach(_inc IN LISTS CAH_INCLUDES)
        list(APPEND _cmd -I "${_inc}")
      endforeach()
      foreach(_def IN LISTS CAH_DEFINES)
        list(APPEND _cmd -D "${_def}")
      endforeach()

      # Outputs & bookkeeping
      set(_outputs_this_rule "")
      if(CAH_EMIT STREQUAL "object" OR CAH_EMIT STREQUAL "both")
        list(APPEND _cmd -Fo "${_obj}")
        list(APPEND _outputs_this_rule "${_obj}")
        list(APPEND _outputs "${_obj}")
      endif()
      if(CAH_EMIT STREQUAL "header" OR CAH_EMIT STREQUAL "both")
        list(APPEND _cmd -Fh "${_hdr}" -Vn "${_var}")
        list(APPEND _outputs_this_rule "${_hdr}")
        list(APPEND _outputs "${_hdr}")
      endif()

      list(APPEND _cmd "${_abs}")

      add_custom_command(
        OUTPUT  ${_outputs_this_rule}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CAH_OUTDIR}/objects"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CAH_OUTDIR}/headers"
        COMMAND ${_cmd}
        DEPENDS "${_abs}"
        COMMENT "DXC ${_profile} ${f}"
        VERBATIM
      )
    endforeach()
  endif()

  # Aggregate outputs under a custom target and hook to the consumer target.
  add_custom_target(${target}_hlsl DEPENDS ${_outputs})
  add_dependencies(${target} ${target}_hlsl)

  if(CAH_EMIT STREQUAL "header" OR CAH_EMIT STREQUAL "both")
    target_include_directories(${target} PRIVATE "${CAH_OUTDIR}/headers")
  endif()

  # IDE grouping
  if(CAH_DIR)
    source_group(TREE "${CAH_DIR}" FILES ${HLSL_FILES})
  else()
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${HLSL_FILES})
  endif()
endfunction()
