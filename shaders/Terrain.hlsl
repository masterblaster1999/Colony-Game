// Simple directional lit shader, SM5/SM6 compatible.
// To remove the DXC "Promoting older shader model profile to 6.0" warning,
// compile this shader with /T vs_6_0 and /T ps_6_0 instead of 5.x.

cbuffer CB : register(b0)
{
    float4x4 gMVP;
    float4   gLightDir; // xyz = light direction (world or view space), w unused
};

// Set this to 1 at compile time if you want gamma-correct output.
#ifndef APPLY_GAMMA_CORRECTION
#define APPLY_GAMMA_CORRECTION 0
#endif

struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float4 color  : COLOR;
};

struct VSOut
{
    float4 pos    : SV_Position;
    float3 normal : NORMAL;
    float4 color  : COLOR;
};

VSOut VSMain(VSIn v)
{
    VSOut o;

    // Transform into clip space.
    o.pos = mul(float4(v.pos, 1.0f), gMVP);

    // Pass the (un-normalized) normal through; we’ll normalize in the pixel shader
    // after interpolation.
    o.normal = v.normal;

    // Just forward the vertex color.
    o.color = v.color;

    return o;
}

// Simple diffuse + ambient term with a soft wrap.
float3 ApplyDirectionalLighting(float3 baseColor, float3 normal, float3 lightDir)
{
    // Ensure both are normalized.
    float3 n = normalize(normal);
    float3 L = normalize(lightDir);

    // Dot product of normal and "towards light" direction.
    float ndl = dot(n, -L);

    // Clamp and apply a soft wrap: 0–1 mapped into [0.15, 1.0]
    ndl = saturate(ndl);
    float lighting = ndl * 0.85f + 0.15f;

    return baseColor * lighting;
}

float3 ApplyGammaIfNeeded(float3 colorLinear)
{
#if APPLY_GAMMA_CORRECTION
    // Simple 2.2 gamma encode. Adjust exponent if your pipeline uses a different value.
    return pow(colorLinear, 1.0f / 2.2f);
#else
    return colorLinear;
#endif
}

float4 PSMain(VSOut i) : SV_Target
{
    // Use the light direction from the constant buffer.
    float3 litColor = ApplyDirectionalLighting(i.color.rgb, i.normal, gLightDir.xyz);

    // Optionally convert to gamma space if your swapchain expects gamma-encoded colors.
    float3 finalColor = ApplyGammaIfNeeded(litColor);

    // Preserve vertex alpha instead of forcing 1.0.
    return float4(finalColor, i.color.a);
}
