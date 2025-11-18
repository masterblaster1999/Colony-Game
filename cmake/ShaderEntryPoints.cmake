# cmake/ShaderEntryPoints.cmake
#
# Central configuration for HLSL shaders when using the Visual Studio generator.
# This tells VS/CMake which function is the entrypoint for each shader file,
# and which shader type/model to use.
#
# Docs:
#   - VS_SHADER_TYPE       : https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_TYPE.html
#   - VS_SHADER_MODEL      : https://cmake.org/cmake/help/latest/prop_sf/VS_SHADER_MODEL.html
#   - VS_SHADER_ENTRYPOINT : documented under cmake-properties(7)
#
# These paths are absolute, rooted at CMAKE_SOURCE_DIR so this file can be
# included from anywhere.

# Full-screen pass VS: shaders/raster/FullScreen.vs.hlsl
# Make sure this file contains a function:
#   VSOutput VSMain(VSInput input) { ... }
set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/shaders/raster/FullScreen.vs.hlsl"
    PROPERTIES
        VS_SHADER_TYPE       "Vertex"
        VS_SHADER_MODEL      "6.0"
        VS_SHADER_ENTRYPOINT "VSMain"
)

# Common full-screen triangle VS: renderer/Shaders/Common/FullScreenTriangleVS.hlsl
# Again, ensure this file has a function:
#   VSOutput VSMain(uint vertexId : SV_VertexID) { ... }
set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Common/FullScreenTriangleVS.hlsl"
    PROPERTIES
        VS_SHADER_TYPE       "Vertex"
        VS_SHADER_MODEL      "6.0"
        VS_SHADER_ENTRYPOINT "VSMain"
)

# Weather precipitation vertex shader:
# renderer/Shaders/Weather/PrecipitationVS.hlsl
# Ensure it has:
#   VSOutput VSMain(VSInput input) { ... }
set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/renderer/Shaders/Weather/PrecipitationVS.hlsl"
    PROPERTIES
        VS_SHADER_TYPE       "Vertex"
        VS_SHADER_MODEL      "6.0"
        VS_SHADER_ENTRYPOINT "VSMain"
)

# Add more shaders here if you want explicit control instead of relying on
# default 'main' entrypoints.
#
# Example pattern:
#
# set_source_files_properties(
#     "${CMAKE_SOURCE_DIR}/renderer/Shaders/SomePixelShader.hlsl"
#     PROPERTIES
#         VS_SHADER_TYPE       "Pixel"
#         VS_SHADER_MODEL      "6.0"
#         VS_SHADER_ENTRYPOINT "PSMain"
# )
