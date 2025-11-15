#include "common/common.hlsli"

// Bindings: same registers you've likely used so far.
Texture2D    gMainTex       : register(t0);
SamplerState gLinearSampler : register(s0);

// Optional output transforms:
// #define CG_TONEMAP_ACES   1   // enable ACES tonemapping
// #define CG_OUTPUT_SRGB    1   // if your swap chain expects sRGB-encoded color
// #define CG_DEBUG_LUMA     1   // visualize luma as grayscale
// #define CG_DITHER         1   // add a tiny dithering pattern in LDR
// #define CG_CLAMP_OUTPUT   1   // clamp final rgb to [0,1]

float4 PSMain(PSIn pin) : SV_Target
{
    // BUGFIX: PSIn has 'tex', not 'Tex'
    float4 col = gMainTex.Sample(gLinearSampler, pin.tex);

#if defined(CG_TONEMAP_ACES)
    col.rgb = cg_aces_approx(col.rgb);
#endif

#if defined(CG_OUTPUT_SRGB)
    col.rgb = cg_linear_to_srgb(col.rgb);
#endif

#if defined(CG_DEBUG_LUMA)
    // Simple luminance-based grayscale debug view
    const float3 lumaWeights = float3(0.2126f, 0.7152f, 0.0722f);
    float luma = dot(col.rgb, lumaWeights);
    col.rgb = luma.xxx;
#endif

#if defined(CG_DITHER)
    // Cheap, stateless pseudo-random based only on UV
    // Avoids needing position or extra buffers.
    float n = frac(sin(dot(pin.tex, float2(12.9898f, 78.233f))) * 43758.5453f);
    // Add ~1 LSB of 8-bit noise to reduce banding
    col.rgb += (n - 0.5f) / 255.0f;
#endif

#if defined(CG_CLAMP_OUTPUT)
    col.rgb = saturate(col.rgb);
#endif

    return col;
}

// Optional alias for build systems that expect 'main' as the entry point.
float4 main(PSIn pin) : SV_Target
{
    return PSMain(pin);
}
