// renderer/Shaders/Common/FullScreenTriangleVS.hlsl

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOutput FullScreenTriangleVS(uint vertexId : SV_VertexID)
{
    VSOutput o;

    // standard fullscreen triangle pattern
    if (vertexId == 0) {
        o.position = float4(-1.0, -1.0, 0.0, 1.0);
        o.uv       = float2(0.0, 1.0);
    } else if (vertexId == 1) {
        o.position = float4(-1.0, 3.0, 0.0, 1.0);
        o.uv       = float2(0.0, -1.0);
    } else {
        o.position = float4(3.0, -1.0, 0.0, 1.0);
        o.uv       = float2(2.0, 1.0);
    }

    return o;
}

// Entry point used by CMake/VS (VS_SHADER_ENTRYPOINT "VSMain")
VSOutput VSMain(uint vertexId : SV_VertexID)
{
    return FullScreenTriangleVS(vertexId);
}
