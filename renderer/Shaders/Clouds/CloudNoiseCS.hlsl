#include "../Common/Noise.hlsli"

RWTexture3D<float> g_CloudDensity : register(u0);

cbuffer CloudGenCB : register(b0) {
    float3 g_VolumeSize;   float g_DensityScale;   // e.g. (128, 64, 128), 1.0
    float3 g_NoiseScale;   float g_Coverage;       // e.g. (0.005, 0.01, 0.005), 0.45
    float  g_WarpFreq1;    float g_WarpAmp1;       float g_WarpFreq2;   float g_WarpAmp2;
    float  g_PerlinWeight; float g_WorleyWeight;   float g_HeightSharp; float g_HeightBase; // shape by height
}

[numthreads(8,8,8)]
void main(uint3 id : SV_DispatchThreadID) {
    if (any(id >= uint3(g_VolumeSize))) return;

    float3 p = (float3(id) / g_VolumeSize) * g_NoiseScale;  // normalized coords * noise scale
    p = domainWarp(p, g_WarpFreq1, g_WarpAmp1, g_WarpFreq2, g_WarpAmp2);

    float base = fbm3(p * 1.0, 5, 2.0, 0.5);
    float cells = worley01(p * 1.2, 1.5);

    float n = saturate(g_PerlinWeight * base + g_WorleyWeight * cells);

    // height shaping: favor mid-altitudes
    float y = float(id.y) / max(1.0, g_VolumeSize.y - 1.0);
    float h = saturate(1.0 - abs(y - g_HeightBase) * g_HeightSharp);

    float d = saturate(n * h * g_DensityScale - g_Coverage);
    g_CloudDensity[id] = d;
}
