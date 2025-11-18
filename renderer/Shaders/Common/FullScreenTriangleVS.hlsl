// renderer/Shaders/Common/FullScreenTriangleVS.hlsl
//
// Full-screen triangle vertex shader.
// Used for post-processing, fullscreen passes, etc.
// Input:    SV_VertexID in [0, 2]
// Outputs:  SV_Position and TEXCOORD0 (UV)

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Positions and UVs for a standard full-screen triangle.
// This avoids the need for a vertex buffer and is very cache-friendly.
static const float2 kFullScreenPositions[3] =
{
    float2(-1.0f, -1.0f),
    float2(-1.0f,  3.0f),
    float2( 3.0f, -1.0f)
};

static const float2 kFullScreenUVs[3] =
{
    float2(0.0f, 1.0f),
    float2(0.0f, -1.0f),
    float2(2.0f, 1.0f)
};

// Core implementation, callable from other shaders if needed.
VSOutput FullScreenTriangleVS(uint vertexId : SV_VertexID)
{
    VSOutput o;

    // Clamp in case of bad index; should always be 0,1,2 in normal use.
    uint idx = min(vertexId, 2u);

    float2 pos = kFullScreenPositions[idx];
    float2 uv  = kFullScreenUVs[idx];

    o.position = float4(pos, 0.0f, 1.0f);
    o.uv       = uv;

    return o;
}

// Entry point used by CMake/VS (VS_SHADER_ENTRYPOINT "VSMain")
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullScreenTriangleVS(vertexId);
}
