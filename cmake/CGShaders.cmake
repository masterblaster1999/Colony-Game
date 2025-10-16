# cmake/CGShaders.cmake
include_guard(GLOBAL)

# Discover shader roots commonly used in this repo
function(cg_collect_shader_dirs OUT_VAR)
  set(_dirs)
  foreach(_cand
    "${CMAKE_SOURCE_DIR}/shaders"
    "${CMAKE_SOURCE_DIR}/renderer/Shaders"
    "${CMAKE_SOURCE_DIR}/src/pcg/shaders")
    if(EXISTS "${_cand}")
      list(APPEND _dirs "${_cand}")
    endif()
  endforeach()
  if(_dirs)
    list(REMOVE_DUPLICATES _dirs)
  endif()
  set(${OUT_VAR} "${_dirs}" PARENT_SCOPE)
endfunction()

# Configure VS per-file HLSL properties and attach sources to the given target
function(cg_configure_vs_hlsl TARGET)
  if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    return()
  endif()
  if(NOT TARGET "${TARGET}")
    message(FATAL_ERROR "cg_configure_vs_hlsl: target '${TARGET}' not found")
  endif()

  # Default shader model if not provided by cache/toolchain
  if(NOT COLONY_HLSL_MODEL)
    set(COLONY_HLSL_MODEL "5.0")
  endif()

  # Mark .hlsli as header-only
  file(GLOB_RECURSE _HLSLI
       "${CMAKE_SOURCE_DIR}/shaders/*.hlsli"
       "${CMAKE_SOURCE_DIR}/renderer/Shaders/*.hlsli"
       "${CMAKE_SOURCE_DIR}/src/pcg/shaders/*.hlsli")
  if(_HLSLI)
    set_source_files_properties(${_HLSLI} PROPERTIES HEADER_FILE_ONLY ON)
  endif()

  # Collect .hlsl files
  file(GLOB_RECURSE _HLSL
       "${CMAKE_SOURCE_DIR}/shaders/*.hlsl"
       "${CMAKE_SOURCE_DIR}/renderer/Shaders/*.hlsl"
       "${CMAKE_SOURCE_DIR}/src/pcg/shaders/*.hlsl")

  foreach(SRC IN LISTS _HLSL)
    get_filename_component(_stem "${SRC}" NAME_WE)
    string(TOLOWER "${_stem}" _lower)
    set(_type "")  # VS expects "Vertex|Pixel|Compute|Geometry|Hull|Domain"
    set(_entry "") # Defaulted below per stage

    if(_lower MATCHES "(_|\\.)vs$|^vs_")
      set(_type "Vertex")
      set(_entry "VSMain")
    elseif(_lower MATCHES "(_|\\.)ps$|^ps_")
      set(_type "Pixel")
      set(_entry "PSMain")
    elseif(_lower MATCHES "(_|\\.)cs$|^cs_")
      set(_type "Compute")
      set(_entry "CSMain")
    elseif(_lower MATCHES "(_|\\.)gs$|^gs_")
      set(_type "Geometry")
      set(_entry "GSMain")
    elseif(_lower MATCHES "(_|\\.)hs$|^hs_")
      set(_type "Hull")
      set(_entry "HSMain")
    elseif(_lower MATCHES "(_|\\.)ds$|^ds_")
      set(_type "Domain")
      set(_entry "DSMain")
    else()
      # Unknown naming -> treat as include
      set_source_files_properties("${SRC}" PROPERTIES HEADER_FILE_ONLY ON)
      continue()
    endif()

    # Per-file VS HLSL properties
    set_source_files_properties("${SRC}" PROPERTIES
      VS_SHADER_TYPE                   "${_type}"
      VS_SHADER_ENTRYPOINT             "${_entry}"
      VS_SHADER_MODEL                  "${COLONY_HLSL_MODEL}"
      VS_SHADER_ENABLE_DEBUG           "$<$<CONFIG:Debug>:true>"
      VS_SHADER_DISABLE_OPTIMIZATIONS  "$<$<CONFIG:Debug>:true>"
      VS_SHADER_OBJECT_FILE_NAME       "$(OutDir)res\\shaders\\%(Filename).cso"
    )

    # Optional extra compiler flags (use MSBuild/FXC-friendly switches)
    set(_flags "")
    foreach(def IN LISTS COLONY_HLSL_DEFINES)
      if(def)
        string(APPEND _flags " /D${def}")
      endif()
    endforeach()
    foreach(inc IN LISTS COLONY_HLSL_INCLUDE_DIRS)
      if(inc)
        string(APPEND _flags " /I\"${inc}\"")
      endif()
    endforeach()
    if(NOT _flags STREQUAL "")
      set_source_files_properties("${SRC}" PROPERTIES VS_SHADER_FLAGS "${_flags}")
    endif()

    source_group(TREE "${CMAKE_SOURCE_DIR}" PREFIX "Shaders" FILES "${SRC}")
    target_sources(${TARGET} PRIVATE "${SRC}")
  endforeach()
endfunction()

#
# High-level wrapper used by CGGameTarget: configure HLSL for VS generators
# or fall back to a manifest/offline pipeline when not using VS (Windows only).
#
function(cg_setup_hlsl_pipeline)
  if(NOT WIN32)
    return()
  endif()

  include(CMakeParseArguments)
  set(oneValueArgs TARGET RENDERER)
  cmake_parse_arguments(CG "" "${oneValueArgs}" "" ${ARGN})
  if(NOT CG_TARGET)
    message(FATAL_ERROR "cg_setup_hlsl_pipeline: TARGET is required")
  endif()

  # Discover include dirs once; reuse across flows
  cg_collect_shader_dirs(_cg_hlsl_inc)
  set(COLONY_HLSL_INCLUDE_DIRS "${_cg_hlsl_inc}" CACHE INTERNAL "HLSL includes" FORCE)

  # Prefer MSBuild HLSL on Visual Studio generators
  if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    cg_configure_vs_hlsl(${CG_TARGET})
    set(_CG_HLSL_TOOLCHAIN "MSBuild (VS_SHADER_*)" PARENT_SCOPE)
    return()
  endif()

  # Non‑VS generators: use simple manifest+DXC/FXC if available
  include("${CMAKE_SOURCE_DIR}/cmake/ColonyShaders.cmake" OPTIONAL)
  set(_manifest "")
  if(EXISTS "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
    set(_manifest "${CMAKE_SOURCE_DIR}/renderer/Shaders/shaders.json")
  elseif(EXISTS "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
    set(_manifest "${CMAKE_SOURCE_DIR}/shaders/shaders.json")
  endif()

  if(_manifest AND COMMAND colony_register_shaders)
    set(_out "${CMAKE_BINARY_DIR}/res/shaders")
    colony_register_shaders(
      TARGET     "${CG_TARGET}"
      MANIFEST   "${_manifest}"
      OUTPUT_DIR "${_out}"
      INCLUDE_DIRS ${COLONY_HLSL_INCLUDE_DIRS}
      DXC_ARGS   -nologo)

    if(COMMAND colony_install_shaders)
      colony_install_shaders(TARGET "${CG_TARGET}" DESTINATION "res/shaders")
    endif()

    set(_CG_HLSL_TOOLCHAIN "DXC/FXC (manifest)" PARENT_SCOPE)
    return()
  endif()

  # Fallback: directory scanner flow if available
  if(COMMAND colony_add_hlsl)
    foreach(_d IN LISTS COLONY_HLSL_INCLUDE_DIRS)
      if(EXISTS "${_d}")
        set(_emit_model "${COLONY_HLSL_MODEL}")
        set(_compiler "AUTO")
        if(CG_RENDERER STREQUAL "d3d11")
          set(_compiler "FXC")
        elseif(CG_RENDERER STREQUAL "d3d12")
          set(_compiler "DXC")
        endif()
        colony_add_hlsl(
          TARGET   "${CG_TARGET}"
          DIR      "${_d}"
          MODEL    "${_emit_model}"
          OUTDIR   "${CMAKE_BINARY_DIR}/res/shaders"
          COMPILER "${_compiler}"
          DEFINES  ${COLONY_HLSL_DEFINES}
          INCLUDES ${COLONY_HLSL_INCLUDE_DIRS}
          EMIT     object)
      endif()
    endforeach()
    set(_CG_HLSL_TOOLCHAIN "DXC/FXC (scan)" PARENT_SCOPE)
  else()
    message(STATUS "cg_setup_hlsl_pipeline: no offline shader toolchain; HLSL compilation is VS-only.")
    set(_CG_HLSL_TOOLCHAIN "none" PARENT_SCOPE)
  endif()
endfunction()
