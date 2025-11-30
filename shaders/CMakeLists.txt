# Build HLSL shaders with DXC on Windows (Visual Studio & Ninja)
if(NOT WIN32)
  message(STATUS "Skipping shaders on non-Windows.")
  return()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/Shaders.cmake)

# List your HLSL files here (expand as needed)
set(SHADER_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_vs.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/mesh_ps.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/AtmosphereSky.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/Batch2D_ps.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/TerrainGen.compute.hlsl
)

# Nice VS Solution view
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SHADER_SRC})

# Global per-config QoL (seen by VS generators and/or helper)
set_source_files_properties(
  ${SHADER_SRC}
  PROPERTIES
    VS_SHADER_ENABLE_DEBUG          $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:true>
    VS_SHADER_DISABLE_OPTIMIZATIONS $<$<CONFIG:Debug>:true>
)

# Per-file metadata (type, entrypoint, SM)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/AtmosphereSky.hlsl
  PROPERTIES VS_SHADER_TYPE Pixel VS_SHADER_MODEL "6.0" VS_SHADER_ENTRYPOINT PSMain)

set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/Batch2D_ps.hlsl
  PROPERTIES VS_SHADER_TYPE Pixel VS_SHADER_MODEL "6.0" VS_SHADER_ENTRYPOINT PSMain)

set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/TerrainGen.compute.hlsl
  PROPERTIES VS_SHADER_TYPE Compute VS_SHADER_MODEL "6.0" VS_SHADER_ENTRYPOINT CSMain)

# Example (uncomment when your mesh shaders expose VSMain/PSMain)
# set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/mesh_vs.hlsl
#   PROPERTIES VS_SHADER_TYPE Vertex VS_SHADER_MODEL "6.0" VS_SHADER_ENTRYPOINT VSMain)
# set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/mesh_ps.hlsl
#   PROPERTIES VS_SHADER_TYPE Pixel VS_SHADER_MODEL "6.0" VS_SHADER_ENTRYPOINT PSMain)

# Configurable output dir
set(COLONY_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE PATH "Where compiled DXIL/CSO land")

# Invoke helper (DXC)
colony_add_hlsl(HLSL_CSOS
  OUTDIR   ${COLONY_SHADER_OUTPUT_DIR}
  FILES    ${SHADER_SRC}
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
  DEFINES  HLSL_SM6=1 $<$<CONFIG:Debug>:HLSL_DEBUG=1>
)

add_custom_target(ColonyGame_shaders ALL DEPENDS ${HLSL_CSOS})
