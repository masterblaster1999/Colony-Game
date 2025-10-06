function(add_hlsl_shaders TARGET)
  set(options)
  set(oneValueArgs SHADER_DIR MODEL ENTRYPOINT OUTPUT_DIR)
  set(multiValueArgs FILES)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  foreach(SHADER ${HLSL_FILES})
    target_sources(${TARGET} PRIVATE ${SHADER})
    get_filename_component(_name ${SHADER} NAME)
    if (_name MATCHES "_vs\\.hlsl$")  ; set(_type Vertex)
    elseif (_name MATCHES "_ps\\.hlsl$"); set(_type Pixel)
    elseif (_name MATCHES "_cs\\.hlsl$"); set(_type Compute)
    else()                              set(_type Pixel)
    endif()
    set_source_files_properties(${SHADER} PROPERTIES
      VS_SHADER_TYPE               ${_type}
      VS_SHADER_MODEL              ${HLSL_MODEL}
      VS_SHADER_ENTRYPOINT         ${HLSL_ENTRYPOINT}
      VS_SHADER_OBJECT_FILE_NAME   "${HLSL_OUTPUT_DIR}/%(Filename).cso"
      VS_SHADER_ENABLE_DEBUG       $<CONFIG:Debug>
    )
  endforeach()
endfunction()
