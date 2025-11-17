// shaders/quad_vs.hlsl
//
// Full-screen triangle vertex shader using SV_VertexID.
//
// This avoids needing a vertex buffer entirely. It relies on the PSIn
// struct defined in shaders/common/common.hlsli:
// 
// struct PSIn
// {
//     float4 position : SV_POSITION;
//     float2 tex      : TEXCOORD0;
// };

#include "common/common.hlsli"

PSIn VSMain(uint vertexId : SV_VertexID)
{
    PSIn vout;

    // Full-screen triangle positions (clip space)
    // This is a standard pattern used to cover the entire screen
    // with a single triangle without precision issues.
    float2 pos;
    float2 uv;

    // Vertex 0
    if (vertexId == 0)
    {
        pos = float2(-1.0f, -1.0f);   // bottom-left
        uv  = float2(0.0f, 1.0f);
    }
    // Vertex 1
    else if (vertexId == 1)
    {
        pos = float2(-1.0f, 3.0f);    // top-left (beyond screen)
        uv  = float2(0.0f, -1.0f);
    }
    // Vertex 2
    else // vertexId == 2
    {
        pos = float2(3.0f, -1.0f);    // bottom-right (beyond screen)
        uv  = float2(2.0f, 1.0f);
    }

    vout.position = float4(pos, 0.0f, 1.0f);
    vout.tex      = uv;

    return vout;
}
