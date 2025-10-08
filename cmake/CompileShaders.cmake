# CMake ≥3.1: Visual Studio understands HLSL per-file properties.
# See: VS_SHADER_ENTRYPOINT / MODEL / TYPE docs.
# Usage: add_shader(<target> <file> <Type> <Entry> <Model>)
# Type: Vertex|Pixel|Geometry|Hull|Domain|Compute
function(add_shader TARGET FILE TYPE ENTRY MODEL)
  get_filename_component(_name "${FILE}" NAME_WE)

  # Mark includes as header-only up front; we won't compile them.
  if(FILE MATCHES "\\.hlsli$")
    set_source_files_properties("${FILE}" PROPERTIES HEADER_FILE_ONLY TRUE)
    return()
  endif()

  # Attach compile properties to HLSL sources
  set_source_files_properties("${FILE}" PROPERTIES
    VS_SHADER_TYPE           "${TYPE}"
    VS_SHADER_ENTRYPOINT     "${ENTRY}"
    VS_SHADER_MODEL          "${MODEL}"
    VS_SHADER_ENABLE_DEBUG   $<CONFIG:Debug>
    VS_SHADER_DISABLE_OPTIMIZATIONS $<CONFIG:Debug>
    VS_SHADER_OBJECT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_name}.cso"
  )
  target_sources(${TARGET} PRIVATE "${FILE}")
endfunction()
