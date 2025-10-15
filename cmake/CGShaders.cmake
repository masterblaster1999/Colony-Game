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

    if(_lower MATCHES "(_|\\.)vs$|^vs_")      ; set(_type "Vertex")  ; set(_entry "VSMain")
    elseif(_lower MATCHES "(_|\\.)ps$|^ps_")  ; set(_type "Pixel")   ; set(_entry "PSMain")
    elseif(_lower MATCHES "(_|\\.)cs$|^cs_")  ; set(_type "Compute") ; set(_entry "CSMain")
    elseif(_lower MATCHES "(_|\\.)gs$|^gs_")  ; set(_type "Geometry"); set(_entry "GSMain")
    elseif(_lower MATCHES "(_|\\.)hs$|^hs_")  ; set(_type "Hull")    ; set(_entry "HSMain")
    elseif(_lower MATCHES "(_|\\.)ds$|^ds_")  ; set(_type "Domain")  ; set(_entry "DSMain")
    else()
      # Unknown naming -> treat as include
      set_source_files_properties("${SRC}" PROPERTIES HEADER_FILE_ONLY ON)
      continue()
    endif()

    # Model comes from your cache var (defaulted in top-level to 5.0 for D3D11)
    # See: VS_SHADER_* docs
    set_source_files_properties("${SRC}" PROPERTIES
      VS_SHADER_TYPE              "${_type}"
      VS_SHADER_ENTRYPOINT        "${_entry}"
      VS_SHADER_MODEL             "${COLONY_HLSL_MODEL}"
      VS_SHADER_ENABLE_DEBUG      $<CONFIG:Debug>
      VS_SHADER_DISABLE_OPTIMIZATIONS $<CONFIG:Debug>)

    # Optional extra compiler flags (avoid list/semicolon pitfalls)
    set(_flags "")
    foreach(def IN LISTS COLONY_HLSL_DEFINES)
      if(def)  string(APPEND _flags " -D${def}")  endif()
    endforeach()
    foreach(inc IN LISTS COLONY_HLSL_INCLUDE_DIRS)
      if(inc)  string(APPEND _flags " -I\"${inc}\"")  endif()
    endforeach()
    if(NOT _flags STREQUAL "")
      set_source_files_properties("${SRC}" PROPERTIES VS_SHADER_FLAGS "${_flags}")
    endif()

    source_group(TREE "${CMAKE_SOURCE_DIR}" PREFIX "Shaders" FILES "${SRC}")
    target_sources(${TARGET} PRIVATE "${SRC}")
  endforeach()
endfunction()
