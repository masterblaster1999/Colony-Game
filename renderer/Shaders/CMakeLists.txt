# renderer/Shaders/CMakeLists.txt

# This shader build is Windows-first. If your project is opened on other OSes,
# skip configuring this directory gracefully.
if(NOT WIN32)
  message(STATUS "Colony-Game: Skipping renderer/Shaders on non-Windows host.")
  return()
endif()

# Your DXC-based shader helper must define colony_add_hlsl()
include(${CMAKE_SOURCE_DIR}/cmake/Shaders.cmake)

if(NOT COMMAND colony_add_hlsl)
  message(FATAL_ERROR
    "colony_add_hlsl() not found. Ensure cmake/Shaders.cmake defines it and is included before this file.")
endif()

# Allow redirecting where compiled shaders are written (DXIL/CSO, etc.)
set(COLONY_SHADER_OUTPUT_DIR
    "${CMAKE_BINARY_DIR}/shaders"
    CACHE PATH "Output directory for compiled HLSL artifacts (DXIL/CSO)")

# Optional: parent CMakeLists can append extra shader include directories here.
# Example:
#   set(COLONY_SHADER_EXTRA_INCLUDES "${CMAKE_SOURCE_DIR}/renderer/Shaders/includes" PARENT_SCOPE)
set(_COLONY_SHADER_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${COLONY_SHADER_EXTRA_INCLUDES})

list(REMOVE_DUPLICATES _COLONY_SHADER_INCLUDES)

# -----------------------------------------------------------------------------#
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

# -----------------------------------------------------------------------------#
# Hook into your existing DXC-based shader build helper.
# Notes:
#  - HLSL_SM6=1 informs your helper to use SM 6.x with DXC.
#  - In Debug, we also define HLSL_DEBUG=1 (generator expression below).
#  - INCLUDES carries this folder + any parent-provided extras.
colony_add_hlsl(HLSL_CSOS
  OUTDIR   ${COLONY_SHADER_OUTPUT_DIR}
  FILES    ${SHADER_SRC}
  INCLUDES ${_COLONY_SHADER_INCLUDES}
  DEFINES  HLSL_SM6=1 $<$<CONFIG:Debug>:HLSL_DEBUG=1>
)

# Build shaders as part of the default ALL target
add_custom_target(ColonyGame_shaders ALL DEPENDS ${HLSL_CSOS})

# Optional: show shader sources in Visual Studio's Solution Explorer
# even though they are built via custom commands.
add_custom_target(ColonyGame_shader_sources SOURCES ${SHADER_SRC})
