// renderer/Shaders/CS_HeightToNormal.hlsl
// Compile with: cs_5_0

Texture2D<float> HeightTex  : register(t0);
RWTexture2D<float4> NormalOut : register(u0);

cbuffer Params : register(b0)
{
    uint2 Size;
    float NormalScale; // e.g., 2.0
    float _pad;
};

float sampleH(int2 p)
{
    p = clamp(p, int2(0,0), int2(Size) - 1);
    return HeightTex.Load(int3(p, 0));
}

[numthreads(8,8,1)]
void CS(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= Size.x || tid.y >= Size.y) return;
    int2 p = int2(tid.xy);

    // Central differences (use Sobel for higher quality)
    float hx = sampleH(p + int2(1,0)) - sampleH(p + int2(-1,0));
    float hy = sampleH(p + int2(0,1)) - sampleH(p + int2(0,-1));

    float3 n = normalize(float3(-hx * NormalScale, 1.0, -hy * NormalScale));
    NormalOut[p] = float4(n * 0.5 + 0.5, 1.0);
}
