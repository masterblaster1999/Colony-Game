#include "common/common.hlsli"

// Bindings: same registers you've likely used so far.
Texture2D    gMainTex        : register(t0);
SamplerState gLinearSampler  : register(s0);

// Optional output transforms:
// #define CG_TONEMAP_ACES 1
// #define CG_OUTPUT_SRGB  1   // if your swap chain expects sRGB-encoded color

float4 PSMain(PSIn pin) : SV_Target
{
    float4 col = gMainTex.Sample(gLinearSampler, pin.Tex);

#if defined(CG_TONEMAP_ACES)
    col.rgb = cg_aces_approx(col.rgb);
#endif

#if defined(CG_OUTPUT_SRGB)
    col.rgb = cg_linear_to_srgb(col.rgb);
#endif
    return col;
}
