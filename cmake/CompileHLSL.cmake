function(compile_hlsl OUT_VAR)
  set(options)
  set(oneValueArgs TARGET ENTRY PROFILE)
  set(multiValueArgs SOURCES INCLUDES DEFINES)
  cmake_parse_arguments(H "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  foreach(SRC ${H_SOURCES})
    get_filename_component(NAME ${SRC} NAME_WE)
    set(OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders/${NAME}.cso)
    add_custom_command(
      OUTPUT ${OUT}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
      COMMAND dxc.exe -nologo -E ${H_ENTRY} -T ${H_PROFILE}
              $<$<CONFIG:Debug>:-Od -Zi>
              $<$<CONFIG:Release>:-O3>
              ${H_DEFINES} ${H_INCLUDES} -Fo ${OUT} ${SRC}
      DEPENDS ${SRC}
      VERBATIM)
    list(APPEND H_OUTS ${OUT})
  endforeach()
  set(${OUT_VAR} ${H_OUTS} PARENT_SCOPE)
endfunction()
