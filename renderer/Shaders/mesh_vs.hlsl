struct VSIn  { float3 Position : POSITION; float3 Normal : NORMAL; float2 Tex : TEXCOORD0; };
struct VSOut { float4 Position : SV_Position; float2 Tex   : TEXCOORD0; float3 Normal : NORMAL; };

cbuffer PerObject : register(b0) { float4x4 gWorld; };
cbuffer PerFrame  : register(b1) { float4x4 gViewProj; };

VSOut main(VSIn vin) {
    VSOut vout;
    float4 wp = mul(float4(vin.Position, 1.0f), gWorld);
    vout.Position = mul(wp, gViewProj);
    vout.Tex = vin.Tex;
    vout.Normal = mul(float4(vin.Normal, 0.0f), gWorld).xyz;
    return vout;
}
