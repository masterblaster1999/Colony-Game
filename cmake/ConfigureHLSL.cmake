# Auto-configure HLSL sources for Visual Studio generators.
# Heuristics: *_vs.hlsl => Vertex VSMain, *_ps.hlsl => Pixel PSMain, *_cs.hlsl => Compute CSMain
function(configure_hlsl_target target)
  if(NOT MSVC)
    return()
  endif()

  get_target_property(_srcs ${target} SOURCES)
  if(NOT _srcs)
    return()
  endif()

  foreach(_s IN LISTS _srcs)
    get_filename_component(_ext "${_s}" EXT)
    if(NOT _ext STREQUAL ".hlsl")
      continue()
    endif()

    get_filename_component(_name "${_s}" NAME_WE)

    if(_name MATCHES "_vs$")
      set_source_files_properties(${_s} PROPERTIES
        VS_SHADER_TYPE        "Vertex"
        VS_SHADER_MODEL       "5.0"
        VS_SHADER_ENTRYPOINT  "VSMain"
      )
    elseif(_name MATCHES "_ps$")
      set_source_files_properties(${_s} PROPERTIES
        VS_SHADER_TYPE        "Pixel"
        VS_SHADER_MODEL       "5.0"
        VS_SHADER_ENTRYPOINT  "PSMain"
      )
    elseif(_name MATCHES "_cs$")
      set_source_files_properties(${_s} PROPERTIES
        VS_SHADER_TYPE        "Compute"
        VS_SHADER_MODEL       "5.0"
        VS_SHADER_ENTRYPOINT  "CSMain"
      )
    else()
      # Treat as include / utility: don't try to compile it as a shader.
      set_source_files_properties(${_s} PROPERTIES HEADER_FILE_ONLY ON)
    endif()
  endforeach()
endfunction()
