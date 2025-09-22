cbuffer ColorCB : register(b1)
{
    float3 LightDir; float _pad0;
    float3 Albedo;   float _pad1;
}
struct PSIn { float4 svpos : SV_POSITION; float3 nrmW : TEXCOORD0; };

float4 main(PSIn i) : SV_TARGET
{
    float ndl = saturate(dot(normalize(i.nrmW), -normalize(LightDir)));
    float3 col = Albedo * (0.2 + 0.8 * ndl);
    return float4(col, 1);
}
