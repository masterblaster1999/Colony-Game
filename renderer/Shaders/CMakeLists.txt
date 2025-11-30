# renderer/Shaders/CMakeLists.txt
include(${CMAKE_SOURCE_DIR}/cmake/Shaders.cmake)

# List of HLSL shader sources in this directory
set(SHADER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_vs.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_ps.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/AtmosphereSky.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/Batch2D_ps.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/TerrainGen.compute.hlsl
  # add more here as you create them
)

# Make HLSL shader type, entry point, and shader model explicit (VS / DXC)
# Atmosphere full-screen sky shader (pixel)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/AtmosphereSky.hlsl
  PROPERTIES
    VS_SHADER_TYPE        Pixel
    VS_SHADER_MODEL       "6.0"
    VS_SHADER_ENTRYPOINT  PSMain
)

# 2D batching shader (pixel)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/Batch2D_ps.hlsl
  PROPERTIES
    VS_SHADER_TYPE        Pixel
    VS_SHADER_MODEL       "6.0"
    VS_SHADER_ENTRYPOINT  PSMain
)

# Terrain generation (compute shader)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/TerrainGen.compute.hlsl
  PROPERTIES
    VS_SHADER_TYPE        Compute
    VS_SHADER_MODEL       "6.0"
    VS_SHADER_ENTRYPOINT  CSMain
)

# If you later change mesh_vs.hlsl / mesh_ps.hlsl to use VSMain / PSMain
# you can uncomment and adjust these as well:
#
# set_source_files_properties(
#   ${CMAKE_CURRENT_SOURCE_DIR}/mesh_vs.hlsl
#   PROPERTIES
#     VS_SHADER_TYPE        Vertex
#     VS_SHADER_MODEL       "6.0"
#     VS_SHADER_ENTRYPOINT  VSMain
# )
#
# set_source_files_properties(
#   ${CMAKE_CURRENT_SOURCE_DIR}/mesh_ps.hlsl
#   PROPERTIES
#     VS_SHADER_TYPE        Pixel
#     VS_SHADER_MODEL       "6.0"
#     VS_SHADER_ENTRYPOINT  PSMain
# )

# Hook into the DXC-based shader build helper
colony_add_hlsl(HLSL_CSOS
  OUTDIR   ${CMAKE_BINARY_DIR}/shaders
  FILES    ${SHADER_SRC}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}     # adjust include paths as needed
  DEFINES  HLSL_SM6=1                      # SM6 define for your shaders
)

add_custom_target(ColonyGame_shaders ALL DEPENDS ${HLSL_CSOS})
