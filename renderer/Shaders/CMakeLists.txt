include(${CMAKE_SOURCE_DIR}/cmake/Shaders.cmake)

set(SHADER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_vs.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_ps.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/AtmosphereSky.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/Batch2D_ps.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/TerrainGen.compute.hlsl
)

# Make shader type/entry/model explicit (consumed by the VS generators)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/AtmosphereSky.hlsl PROPERTIES
  VS_SHADER_TYPE Pixel
  VS_SHADER_MODEL "6.0"
  VS_SHADER_ENTRYPOINT PSMain
  # Optionally control output file name:
  # VS_SHADER_OBJECT_FILE_NAME "${CMAKE_BINARY_DIR}/shaders/%(Filename).cso"
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Batch2D_ps.hlsl PROPERTIES
  VS_SHADER_TYPE Pixel
  VS_SHADER_MODEL "6.0"
  VS_SHADER_ENTRYPOINT PSMain
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/TerrainGen.compute.hlsl PROPERTIES
  VS_SHADER_TYPE Compute
  VS_SHADER_MODEL "6.0"
  VS_SHADER_ENTRYPOINT CSMain
)

# Optional: if your mesh_* shaders follow VSMain/PSMain naming:
# set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/mesh_vs.hlsl PROPERTIES
#   VS_SHADER_TYPE Vertex
#   VS_SHADER_MODEL "6.0"
#   VS_SHADER_ENTRYPOINT VSMain
# )
# set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/mesh_ps.hlsl PROPERTIES
#   VS_SHADER_TYPE Pixel
#   VS_SHADER_MODEL "6.0"
#   VS_SHADER_ENTRYPOINT PSMain
# )

# Keep your existing helper if you have cross-generator DXC steps:
colony_add_hlsl(HLSL_CSOS
  OUTDIR   ${CMAKE_BINARY_DIR}/shaders
  FILES    ${SHADER_SRC}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  DEFINES  HLSL_SM6=1
)
add_custom_target(ColonyGame_shaders ALL DEPENDS ${HLSL_CSOS})
