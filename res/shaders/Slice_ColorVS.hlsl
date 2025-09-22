#pragma pack_matrix(row_major)

cbuffer CameraCB : register(b0)
{
    float4x4 World;
    float4x4 View;
    float4x4 Proj;
};

struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; };
struct VSOut { float4 svpos : SV_POSITION; float3 nrmW : TEXCOORD0; };

VSOut main(VSIn i)
{
    VSOut o;
    float3 n = mul(float4(i.nrm, 0), World).xyz;
    float3 wp = mul(float4(i.pos, 1), World).xyz;
    o.nrmW = normalize(n);
    float4 vp = mul(float4(wp, 1), View);
    o.svpos = mul(vp, Proj);
    return o;
}
