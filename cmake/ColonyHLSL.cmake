# -------------------------------------------------------------------------------------------------
# ColonyHLSL.cmake
#
# Defines: colony_add_hlsl(<target> [FILES ...] [OUTPUT_DIR <dir>]
#                          [DEFAULT_MODEL <5_0|5_1|6_6|6_7>] [INCLUDE_DIRS <...>]
#                          [DEFINES <NAME[=VALUE];...>])
#
# Purpose:
#   Attach .hlsl sources to a CMake target and configure Visual Studio’s native HLSL
#   compilation (FxCompile/MSBuild) using official source-file properties:
#     VS_SHADER_TYPE, VS_SHADER_MODEL, VS_SHADER_ENTRYPOINT,
#     VS_SHADER_OBJECT_FILE_NAME, VS_SHADER_ENABLE_DEBUG, VS_SHADER_DISABLE_OPTIMIZATIONS,
#     VS_SHADER_FLAGS.
#
#   By default we infer shader stage and entrypoint from the filename suffix:
#     *_vs.hlsl -> "Vertex Shader"     / entry "VSMain"
#     *_ps.hlsl -> "Pixel Shader"      / entry "PSMain"
#     *_cs.hlsl -> "Compute Shader"    / entry "CSMain"
#     *_gs.hlsl -> "Geometry Shader"   / entry "GSMain"
#     *_hs.hlsl -> "Hull Shader"       / entry "HSMain"
#     *_ds.hlsl -> "Domain Shader"     / entry "DSMain"
#
#   You can override the default Shader Model for all files with DEFAULT_MODEL.
#   We also inject Debug-friendly defaults:
#     VS_SHADER_ENABLE_DEBUG          = $<CONFIG:Debug>  (adds /Zi)    [CMake 3.11+]
#     VS_SHADER_DISABLE_OPTIMIZATIONS = $<CONFIG:Debug>  (adds /Od)    [CMake 3.11+]
#
#   Compiled CSO location:
#     <OUTPUT_DIR>/<Config>/<basename>.cso    (OUTPUT_DIR defaults to ${CMAKE_BINARY_DIR}/shaders)
#
# Usage examples:
#   # simplest (stage from suffix, model inferred from COLONY_RENDERER)
#   colony_add_hlsl(ColonyGame
#     FILES ${CMAKE_SOURCE_DIR}/shaders/quad_vs.hlsl
#           ${CMAKE_SOURCE_DIR}/shaders/quad_ps.hlsl)
#
#   # override model & add include dirs/defines for shader preprocessing
#   colony_add_hlsl(ColonyGame
#     FILES ${CMAKE_SOURCE_DIR}/shaders/compute_cs.hlsl
#     DEFAULT_MODEL 6_7
#     INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/shaders;${CMAKE_SOURCE_DIR}/src/pcg/shaders"
#     DEFINES "SHADOWS=1;QUALITY=2")
#
# Requirements/Notes:
#   * Works best with Visual Studio generators on Windows. Non-VS generators will
#     still see the files in the project, but the VS_* properties are only honored
#     by Visual Studio’s MSBuild integration. :contentReference[oaicite:2]{index=2}
#   * Shader property docs:
#       - VS_SHADER_TYPE / VS_SHADER_MODEL / VS_SHADER_ENTRYPOINT  (CMake ≥3.1)  :contentReference[oaicite:3]{index=3}
#       - VS_SHADER_OBJECT_FILE_NAME                                (CMake ≥3.12) :contentReference[oaicite:4]{index=4}
#       - VS_SHADER_ENABLE_DEBUG, VS_SHADER_DISABLE_OPTIMIZATIONS    (CMake ≥3.11) :contentReference[oaicite:5]{index=5}
#       - VS_SHADER_FLAGS                                            (CMake ≥3.2)  :contentReference[oaicite:6]{index=6}
#
# Mesh/Amplification shaders:
#   If you pass files named *_ms.hlsl or *_as.hlsl we emit a warning because not
#   all Visual Studio + CMake combinations expose explicit “Mesh/Amplification Shader”
#   types through VS_SHADER_TYPE. Prefer your existing DXC path in CMake (BUILD_SHADERS_DXC)
#   for SM6+/DXIL cases, which this project already supports. :contentReference[oaicite:7]{index=7}
#
# License: MIT (match project)
# -------------------------------------------------------------------------------------------------

if(DEFINED _COLONY_HLSL_CM_INCLUDED)
  return()
endif()
set(_COLONY_HLSL_CM_INCLUDED 1)

function(_cg_hlsl_infer_from_name SRC OUT_TYPE OUT_ENTRY OUT_WARN)
  # Determine stage from filename (no extension), choose a default entry point.
  get_filename_component(_name_we "${SRC}" NAME_WE)
  string(TOLOWER "${_name_we}" _lower)
  set(_type "")
  set(_entry "")
  set(_warn "")

  if(_lower MATCHES "(_|\\.)vs$|^vs_")
    set(_type  "Vertex Shader")
    set(_entry "VSMain")
  elseif(_lower MATCHES "(_|\\.)ps$|^ps_")
    set(_type  "Pixel Shader")
    set(_entry "PSMain")
  elseif(_lower MATCHES "(_|\\.)cs$|^cs_")
    set(_type  "Compute Shader")
    set(_entry "CSMain")
  elseif(_lower MATCHES "(_|\\.)gs$|^gs_")
    set(_type  "Geometry Shader")
    set(_entry "GSMain")
  elseif(_lower MATCHES "(_|\\.)hs$|^hs_")
    set(_type  "Hull Shader")
    set(_entry "HSMain")
  elseif(_lower MATCHES "(_|\\.)ds$|^ds_")
    set(_type  "Domain Shader")
    set(_entry "DSMain")
  elseif(_lower MATCHES "(_|\\.)ms$|^ms_" OR _lower MATCHES "(_|\\.)as$|^as_")
    # Mesh / Amplification shader: Not universally surfaced as a VS_SHADER_TYPE choice
    # across all VS+CMake combos — keep source but warn so callers use the DXC path.
    set(_warn "Mesh/Amplification shader detected: ${SRC}. Visual Studio VS_SHADER_TYPE may not expose these stages in your toolchain; prefer the DXC-based path for SM6+. File will be attached but not auto-compiled here.")
  endif()

  # Per-file convention override used elsewhere in the repo (pcg noise compute)
  if("${_name_we}" STREQUAL "noise_fbm_cs")
    set(_entry "main")
  endif()

  set(${OUT_TYPE}  "${_type}"  PARENT_SCOPE)
  set(${OUT_ENTRY} "${_entry}" PARENT_SCOPE)
  set(${OUT_WARN}  "${_warn}"  PARENT_SCOPE)
endfunction()

function(_cg_join_with_quotes OUT_STR)
  # Joins ARGN into a single string, each item quoted, space separated.
  # Useful for composing /I "dir" and /D NAME=VAL sequences.
  set(_acc "")
  foreach(_tok IN LISTS ARGN)
    if(NOT "${_tok}" STREQUAL "")
      if(_acc STREQUAL "")
        set(_acc "\"${_tok}\"")
      else()
        set(_acc "${_acc} \"${_tok}\"")
      endif()
    endif()
  endforeach()
  set(${OUT_STR} "${_acc}" PARENT_SCOPE)
endfunction()

function(colony_add_hlsl TARGET)
  if(NOT TARGET ${TARGET})
    message(FATAL_ERROR "colony_add_hlsl: target '${TARGET}' does not exist.")
  endif()

  # Parse simple signature:
  #   colony_add_hlsl(target FILES a.hlsl b.hlsl [OUTPUT_DIR dir] [DEFAULT_MODEL m]
  #                   [INCLUDE_DIRS list] [DEFINES list])
  set(_opts)
  set(_one  OUTPUT_DIR DEFAULT_MODEL)
  set(_multi FILES INCLUDE_DIRS DEFINES)
  cmake_parse_arguments(CAH "${_opts}" "${_one}" "${_multi}" ${ARGN})

  # Collect file list (accept unparsed extras as files too)
  set(_files "${CAH_FILES}")
  if(CAH_UNPARSED_ARGUMENTS)
    list(APPEND _files ${CAH_UNPARSED_ARGUMENTS})
  endif()
  if(NOT _files)
    message(FATAL_ERROR "colony_add_hlsl: no .hlsl sources provided.")
  endif()

  # Default shader model:
  #  - If caller passed DEFAULT_MODEL use it;
  #  - Else infer from global COLONY_RENDERER (d3d11 -> 5_0, d3d12 -> 6_7)
  set(_model "")
  if(DEFINED CAH_DEFAULT_MODEL AND NOT CAH_DEFAULT_MODEL STREQUAL "")
    set(_model "${CAH_DEFAULT_MODEL}")
  elseif(DEFINED COLONY_RENDERER AND COLONY_RENDERER STREQUAL "d3d12")
    set(_model "6_7")
  else()
    set(_model "5_0")
  endif()

  # Output directory (per-config subfolder is added via CMAKE_CFG_INTDIR)
  if(DEFINED CAH_OUTPUT_DIR AND NOT CAH_OUTPUT_DIR STREQUAL "")
    set(_out_dir "${CAH_OUTPUT_DIR}")
  else()
    set(_out_dir "${CMAKE_BINARY_DIR}/shaders")
  endif()

  # Compose extra flags for includes/defines (passed via VS_SHADER_FLAGS).
  # This maps to HLSL "Additional Options" and is supported by CMake. :contentReference[oaicite:8]{index=8}
  set(_extra_flags "")
  if(CAH_INCLUDE_DIRS)
    # Convert ';' -> distinct /I "dir"
    foreach(_idir IN LISTS CAH_INCLUDE_DIRS)
      if(NOT "${_idir}" STREQUAL "")
        set(_extra_flags "${_extra_flags} /I \"${_idir}\"")
      endif()
    endforeach()
  endif()
  if(CAH_DEFINES)
    foreach(_def IN LISTS CAH_DEFINES)
      if(NOT "${_def}" STREQUAL "")
        set(_extra_flags "${_extra_flags} /D${_def}")
      endif()
    endforeach()
  endif()
  string(STRIP "${_extra_flags}" _extra_flags)

  # Ensure the output root exists at build-time (avoid up-to-date confusion)
  file(MAKE_DIRECTORY "${_out_dir}")

  # Warn if we’re not on Visual Studio generator — properties are VS-specific. :contentReference[oaicite:9]{index=9}
  if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(WARNING "colony_add_hlsl: Visual Studio generator recommended. VS_SHADER_* properties are only honored by VS/MSBuild.")
  endif()

  set(_attached "")
  foreach(_src IN LISTS _files)
    # Make the path absolute so set_source_files_properties is unambiguous.
    if(IS_ABSOLUTE "${_src}")
      set(_abs "${_src}")
    else()
      get_filename_component(_abs "${_src}" ABSOLUTE "${CMAKE_CURRENT_LIST_DIR}/..")
      if(NOT EXISTS "${_abs}")
        # Try relative to source dir if the heuristic above fails
        get_filename_component(_abs "${_src}" ABSOLUTE "${CMAKE_SOURCE_DIR}")
      endif()
    endif()
    if(NOT EXISTS "${_abs}")
      message(FATAL_ERROR "colony_add_hlsl: file not found: ${_src}")
    endif()

    # Stage + default entry
    _cg_hlsl_infer_from_name("${_abs}" _stype _sentry _swarn)
    if(_swarn)
      message(WARNING "${_swarn}")
      # Do not try to force-compile unknown stages; attach for editing only
      # and exclude from build to prevent FxCompile errors. Use DXC path instead. :contentReference[oaicite:10]{index=10}
      set_source_files_properties("${_abs}" PROPERTIES
        VS_TOOL_OVERRIDE "None"         # exclude from build (still visible in VS) :contentReference[oaicite:11]{index=11}
      )
      list(APPEND _attached "${_abs}")
      continue()
    endif()

    # Compute final CSO path: <out>/<cfg>/<basename>.cso
    get_filename_component(_name_we "${_abs}" NAME_WE)
    set(_cso "${_out_dir}/${CMAKE_CFG_INTDIR}/${_name_we}.cso")

    # Apply VS shader properties. See CMake docs for each property. :contentReference[oaicite:12]{index=12}
    if(NOT "${_stype}" STREQUAL "")
      set_source_files_properties("${_abs}" PROPERTIES VS_SHADER_TYPE "${_stype}")
    endif()
    if(NOT "${_sentry}" STREQUAL "")
      set_source_files_properties("${_abs}" PROPERTIES VS_SHADER_ENTRYPOINT "${_sentry}")
    endif()
    set_source_files_properties("${_abs}" PROPERTIES
      VS_SHADER_MODEL                "${_model}"
      VS_SHADER_OBJECT_FILE_NAME     "${_cso}"
      VS_SHADER_ENABLE_DEBUG         "$<CONFIG:Debug>"
      VS_SHADER_DISABLE_OPTIMIZATIONS "$<CONFIG:Debug>"
    )
    if(NOT "${_extra_flags}" STREQUAL "")
      # Append any /I and /D switches for this file.
      set_source_files_properties("${_abs}" PROPERTIES VS_SHADER_FLAGS "${_extra_flags}")
    endif()

    list(APPEND _attached "${_abs}")
  endforeach()

  # Add to target so they appear in the solution and compile with the target build.
  if(_attached)
    target_sources(${TARGET} PRIVATE ${_attached})
    # Nice tree grouping under the repository's shaders/ folder if present.
    if(EXISTS "${CMAKE_SOURCE_DIR}/shaders")
      source_group(TREE "${CMAKE_SOURCE_DIR}/shaders" FILES ${_attached})
    else()
      source_group("shaders" FILES ${_attached})
    endif()
  endif()
endfunction()
