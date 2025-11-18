// renderer/Shaders/Batch2D_ps.hlsl
//
// Pixel shader for batched 2D rendering.
// Fixes: "control reaches end of non-void function" by ensuring all paths return.
//
// Expected VS output layout: position, uv, color.
// Match this with your Batch2D_vs.hlsl output.

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 Texcoord : TEXCOORD0;
    float4 Color    : COLOR0;
};

// Resource bindings:
//  t0 - base texture
//  s0 - sampler for base texture
Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

// Main pixel shader entry point.
// Make sure ColonyShaders.vcxproj (or CMake HLSL rules) set:
//   Entry point: PSMain
//   Shader type: Pixel Shader
//   Profile:     ps_6_0 (or higher)
float4 PSMain(PSInput input) : SV_TARGET
{
    // Sample the texture.
    float4 texColor = gTexture.Sample(gSampler, input.Texcoord);

    // Apply vertex color modulation (tinting / fading).
    float4 outColor = texColor * input.Color;

    // Simple safety clamps if you want:
    // outColor = saturate(outColor);

    return outColor; // <- guarantees a return on every path
}
