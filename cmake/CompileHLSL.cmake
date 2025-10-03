# cmake/CompileHLSL.cmake
function(compile_hlsl OUTVAR)
  set(options)
  set(oneValueArgs TARGET SHADER ENTRY STAGE CONFIG)
  cmake_parse_arguments(HLSL "${options}" "${oneValueArgs}" "" ${ARGN})

  set(out "${CMAKE_BINARY_DIR}/shaders/${HLSL_SHADER}.${HLSL_STAGE}.bin")
  add_custom_command(
    OUTPUT "${out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND dxc -T ${HLSL_STAGE}_6_6 -E ${HLSL_ENTRY} "$<IF:$<CONFIG:Debug>,-Zi,-O3>" -Fo "${out}" "${HLSL_SHADER}"
    DEPENDS "${HLSL_SHADER}"
    VERBATIM)
  set(${OUTVAR} "${out}" PARENT_SCOPE)
endfunction()
