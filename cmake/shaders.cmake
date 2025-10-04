# cmake/shaders.cmake  (Windows + Visual Studio generators)
include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR "shaders.cmake is Windows-only.")
endif()

function(colony_add_hlsl TARGET)
  # Usage: colony_add_hlsl(MyExe shaders/quad_vs.hlsl shaders/quad_ps.hlsl ...)
  set(hlsl_files ${ARGN})
  if(NOT hlsl_files)
    return()
  endif()

  source_group("Shaders" FILES ${hlsl_files})
  target_sources(${TARGET} PRIVATE ${hlsl_files})

  foreach(src IN LISTS hlsl_files)
    get_filename_component(name "${src}" NAME_WE)

    # Infer type from filename suffix (convention: *_vs, *_ps, *_cs, *_gs, *_hs, *_ds)
    set(_type "")
    set(_entry "")
    if(name MATCHES "_vs$")
      set(_type "Vertex")
      set(_entry "VSMain")
    elseif(name MATCHES "_ps$")
      set(_type "Pixel")
      set(_entry "PSMain")
    elseif(name MATCHES "_cs$")
      set(_type "Compute")
      set(_entry "CSMain")
    elseif(name MATCHES "_gs$")
      set(_type "Geometry")
      set(_entry "GSMain")
    elseif(name MATCHES "_hs$")
      set(_type "Hull")
      set(_entry "HSMain")
    elseif(name MATCHES "_ds$")
      set(_type "Domain")
      set(_entry "DSMain")
    else()
      message(WARNING "Cannot infer shader type for ${src}; set VS_SHADER_TYPE/ENTRYPOINT manually.")
    endif()

    # Output .cso into build/shaders/<config>/
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>/${name}.cso")

    # Properties recognized by Visual Studio HLSL build
    set_source_files_properties("${src}" PROPERTIES
      VS_SHADER_TYPE               "${_type}"
      VS_SHADER_MODEL              "5.0"             # ps_5_0 / vs_5_0 etc.
      VS_SHADER_ENTRYPOINT         "${_entry}"
      VS_SHADER_OBJECT_FILE_NAME   "${_obj}"
      # Optional: debug flags in Debug; optimize in Release (MSBuild merges these)
      VS_SHADER_FLAGS              "$<$<CONFIG:Debug>:/Zi;/Od>;$<$<CONFIG:Release>:/O3>"
    )
  endforeach()

  # Make sure the output directory exists at build time
  add_custom_command(TARGET ${TARGET} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>")
endfunction()
