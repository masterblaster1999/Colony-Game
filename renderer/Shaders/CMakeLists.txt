# renderer/Shaders/CMakeLists.txt
include(${CMAKE_SOURCE_DIR}/cmake/Shaders.cmake)

set(SHADER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_vs.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_ps.hlsl
  # add more here as you create them
)

colony_add_hlsl(HLSL_CSOS
  OUTDIR   ${CMAKE_BINARY_DIR}/shaders
  FILES    ${SHADER_SRC}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}     # adjust include paths as needed
  DEFINES  HLSL_SM6=1
)

add_custom_target(ColonyGame_shaders ALL DEPENDS ${HLSL_CSOS})
