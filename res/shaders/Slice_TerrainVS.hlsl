#pragma pack_matrix(row_major)

Texture2D HeightTex : register(t0);
SamplerState LinearWrap : register(s0);

cbuffer CameraCB : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Proj;
    float     HeightAmplitude; // world units
    float2    HeightTexel;     // 1/width, 1/height
    float     TileWorld;       // world units per grid tile (for LOD/scale if needed)
    float     _pad0;
};

struct VSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 svpos : SV_POSITION; float3 wpos : TEXCOORD0; float2 uv : TEXCOORD1; };

VSOut main(VSIn i)
{
    float h = HeightTex.SampleLevel(LinearWrap, i.uv, 0).r * HeightAmplitude;
    float3 wp = mul(float4(i.pos + float3(0, h, 0), 1.0f), World).xyz;

    VSOut o;
    o.wpos = wp;
    float4 vp = mul(float4(wp, 1.0f), View);
    o.svpos = mul(vp, Proj);
    o.uv = i.uv;
    return o;
}
