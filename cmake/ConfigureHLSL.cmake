# cmake/ConfigureHLSL.cmake
# Auto-configure HLSL sources for Visual Studio generators.
# Supports BOTH suffix (_vs/_ps/_cs) AND prefix (VS_/PS_/CS_) naming, and
# entrypoints of either VS/PS/CS or VSMain/PSMain/CSMain.

function(configure_hlsl_target target)
  if(NOT MSVC)
    return()
  endif()

  get_target_property(_srcs ${target} SOURCES)
  if(NOT _srcs)
    return()
  endif()

  foreach(_s IN LISTS _srcs)
    get_filename_component(_ext  "${_s}" EXT)
    get_filename_component(_name "${_s}" NAME_WE)

    # Treat includes as header-only
    if(_ext STREQUAL ".hlsli")
      set_source_files_properties(${_s} PROPERTIES HEADER_FILE_ONLY ON)
      continue()
    endif()

    if(NOT _ext STREQUAL ".hlsl")
      continue()
    endif()

    set(_shader_type "")
    set(_entry "")

    # ---- Recognize suffix patterns ----
    if(_name MATCHES "_vs$")
      set(_shader_type "Vertex")
      set(_entry "VSMain")
    elseif(_name MATCHES "_ps$")
      set(_shader_type "Pixel")
      set(_entry "PSMain")
    elseif(_name MATCHES "_cs$")
      set(_shader_type "Compute")
      set(_entry "CSMain")

    # ---- Recognize prefix patterns ----
    elseif(_name MATCHES "^VS_")
      set(_shader_type "Vertex")
      # Prefer VS if present, else VSMain
      set(_entry "VS")
    elseif(_name MATCHES "^PS_")
      set(_shader_type "Pixel")
      set(_entry "PS")
    elseif(_name MATCHES "^CS_")
      set(_shader_type "Compute")
      set(_entry "CS")
    endif()

    if(_shader_type STREQUAL "")
      # Unknown pattern -> don't try to compile
      set_source_files_properties(${_s} PROPERTIES HEADER_FILE_ONLY ON)
      continue()
    endif()

    # Apply Visual Studio HLSL properties
    set_source_files_properties(${_s} PROPERTIES
      VS_SHADER_TYPE       "${_shader_type}"
      VS_SHADER_MODEL      "5.0"      # D3D11 target; swap to 6.x if using DXC + D3D12
      VS_SHADER_ENTRYPOINT "${_entry}"
    )
  endforeach()
endfunction()
