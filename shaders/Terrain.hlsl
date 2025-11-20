cbuffer CB : register(b0)
{
    float4x4 gMVP;
    float4   gLightDir; // xyz
};

struct VSIn
{
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float4 color  : COLOR;
};

struct VSOut
{
    float4 pos   : SV_Position;
    float3 n     : NORMAL;
    float4 color : COLOR;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos   = mul(float4(v.pos, 1.0f), gMVP);
    o.n     = normalize(v.normal);
    o.color = v.color;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 n   = normalize(i.n);
    float3 L   = normalize(gLightDir.xyz);
    float  ndl = saturate(dot(n, -L)) * 0.85f + 0.15f; // soft wrap
    float3 c   = i.color.rgb * ndl;

    // If you present to an sRGB swapchain, keep this in linear;
    // otherwise apply gamma correction here before output.
    return float4(c, 1.0f);
}
