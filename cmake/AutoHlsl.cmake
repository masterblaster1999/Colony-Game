# cmake/AutoHlsl.cmake
# Auto-configure Visual Studio HLSL per-file properties for .hlsl files
# and mark all .hlsli as header-only to avoid FXC/DXC errors.

function(colony_autoconfigure_hlsl TARGET)
  if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    return()
  endif()

  # Mark all .hlsli as header-only so MSBuild won't compile them
  file(GLOB_RECURSE _HLSLI
       "${CMAKE_SOURCE_DIR}/shaders/*.hlsli"
       "${CMAKE_SOURCE_DIR}/renderer/Shaders/*.hlsli")
  if(_HLSLI)
    set_source_files_properties(${_HLSLI} PROPERTIES HEADER_FILE_ONLY TRUE)
  endif()

  # Collect .hlsl files
  file(GLOB_RECURSE _HLSL
       "${CMAKE_SOURCE_DIR}/shaders/*.hlsl"
       "${CMAKE_SOURCE_DIR}/renderer/Shaders/*.hlsl")

  foreach(SRC IN LISTS _HLSL)
    get_filename_component(_name "${SRC}" NAME_WE)
    string(TOLOWER "${_name}" _lower)
    set(_type "")
    set(_entry "")
    set(_model "5.0") # default SM—good for D3D11. Override per target if needed.

    if(_lower MATCHES "(_|\\.)vs$|^vs_")
      set(_type "Vertex")
      set(_entry "VSMain")
      set(_model "${COLONY_HLSL_MODEL}")
    elseif(_lower MATCHES "(_|\\.)ps$|^ps_")
      set(_type "Pixel")
      set(_entry "PSMain")
      set(_model "${COLONY_HLSL_MODEL}")
    elseif(_lower MATCHES "(_|\\.)cs$|^cs_")
      set(_type "Compute")
      set(_entry "CSMain")
      set(_model "${COLONY_HLSL_MODEL}")
    elseif(_lower MATCHES "(_|\\.)gs$|^gs_")
      set(_type "Geometry")
      set(_entry "GSMain")
      set(_model "${COLONY_HLSL_MODEL}")
    elseif(_lower MATCHES "(_|\\.)hs$|^hs_")
      set(_type "Hull")
      set(_entry "HSMain")
      set(_model "${COLONY_HLSL_MODEL}")
    elseif(_lower MATCHES "(_|\\.)ds$|^ds_")
      set(_type "Domain")
      set(_entry "DSMain")
      set(_model "${COLONY_HLSL_MODEL}")
    else()
      # Unknown stage naming—skip compilation and let it be header-only
      set_source_files_properties("${SRC}" PROPERTIES HEADER_FILE_ONLY TRUE)
      continue()
    endif()

    # Special case: known compute shader using 'main' (keep your log’s precedent)
    if("${_name}" STREQUAL "noise_fbm_cs")
      set(_entry "main")
    endif()

    # Apply per-file VS HLSL properties
    set_source_files_properties("${SRC}" PROPERTIES
      VS_SHADER_TYPE           "${_type}"
      VS_SHADER_ENTRYPOINT     "${_entry}"
      VS_SHADER_MODEL          "${_type STREQUAL \"Compute\" AND COLONY_HLSL_MODEL MATCHES \"^6\" ? \"cs_6_7\" : \"${_type STREQUAL \"Compute\" ? \"cs_${_model}\" : \"${_type MATCHES \"Vertex|Pixel\" ? ( _type STREQUAL \"Vertex\" ? \"vs_${_model}\" : \"ps_${_model}\" ) : \"${_model}\" }\"}" # safe fallback
    )
    target_sources(${TARGET} PRIVATE "${SRC}")
  endforeach()
endfunction()
