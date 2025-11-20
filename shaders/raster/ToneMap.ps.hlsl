#include "RootSig.hlsli"

// Use the global/default graphics root signature.
// RS_GLOBAL is aliased to RS_GRAPHICS_STATIC_SAMPLERS in RootSig.hlsli.
[RootSignature(RS_GLOBAL)]
Texture2D     gSrc   : register(t0);
SamplerState  gSamp0 : register(s0);  // matches StaticSampler(s0) in the RS

cbuffer DrawCB : register(b0)
{
    float exposure;
    float gamma;
    float2 _pad;
}

float3 Reinhard(float3 x)
{
    return x / (1.0 + x);
}

float4 PSMain(float2 uv : TEXCOORD0) : SV_Target
{
    float3 hdr = gSrc.Sample(gSamp0, uv).rgb * exposure;
    float3 ldr = pow(Reinhard(hdr), 1.0 / gamma);
    return float4(ldr, 1.0);
}
