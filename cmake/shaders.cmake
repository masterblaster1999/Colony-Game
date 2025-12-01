function(colony_add_hlsl OUTVAR)
  set(options)
  set(oneValueArgs OUTDIR)
  set(multiValueArgs FILES INCLUDES DEFINES)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if (CMAKE_GENERATOR MATCHES "Visual Studio")
    foreach(f ${HLSL_FILES})
      set_source_files_properties(${f} PROPERTIES
        VS_SHADER_ENABLE_DEBUG $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:true>
        VS_SHADER_OBJECT_FILE_NAME "${HLSL_OUTDIR}/$<TARGET_FILE_BASE_NAME:${f}>.cso"
        VS_SHADER_INCLUDE_DIRECTORIES "$<JOIN:${HLSL_INCLUDES},;>"
      )
    endforeach()
    set(${OUTVAR} ${HLSL_FILES} PARENT_SCOPE)
  else()
    find_program(DXC_EXE NAMES dxc)
    if(NOT DXC_EXE)
      message(FATAL_ERROR "dxc.exe not found; install DirectX Shader Compiler or add to PATH.")
    endif()
    file(MAKE_DIRECTORY ${HLSL_OUTDIR})
    set(outputs)
    foreach(src ${HLSL_FILES})
      get_filename_component(name ${src} NAME_WE)
      # obtain type/profile/entry from properties if you prefer; here’s a basic map:
      set(profile "$<$<MATCHES:${src},.*compute.*>:cs_6_0>$<$<MATCHES:${src},.*_vs\\..*>:vs_6_0>$<$<MATCHES:${src},.*_ps\\..*>:ps_6_0>")
      set(entry   "$<$<MATCHES:${src},.*compute.*>:CSMain>$<$<MATCHES:${src},.*_vs\\..*>:VSMain>$<$<MATCHES:${src},.*_ps\\..*>:PSMain>")
      set(out "${HLSL_OUTDIR}/${name}.cso")
      add_custom_command(OUTPUT ${out}
        COMMAND ${DXC_EXE} -nologo -T ${profile} -E ${entry}
                -Fo ${out} $<$<CONFIG:Debug>:-Zi -Qembed_debug>
                $<$<BOOL:${HLSL_DEFINES}>:-D${HLSL_DEFINES}>
                $<$<BOOL:${HLSL_INCLUDES}>:-I${HLSL_INCLUDES}>
                ${src}
        DEPENDS ${src} VERBATIM)
      list(APPEND outputs ${out})
    endforeach()
    add_custom_target(ColonyGame_hlsl_build ALL DEPENDS ${outputs})
    set(${OUTVAR} ${outputs} PARENT_SCOPE)
  endif()
endfunction()
