cbuffer FrameCB : register(b0)
{
    float4x4 gViewProj;
    float3   gLightDir;
    float    gTime;
    float3   gCameraPos;
    float    _pad0;
};

cbuffer ObjectCB : register(b1)
{
    float4x4 gWorld;
    float4   gColor;
};

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
};
struct VSOut {
    float4 posH : SV_Position;
    float3 nrmW : NORMAL;
    float4 col  : COLOR0;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    float4 posW = mul(float4(i.pos,1), gWorld);
    o.posH = mul(posW, gViewProj);
    // No non-uniform scaling assumed; ok for unit sphere * uniform scale
    float3 nrmW = mul(float4(i.nrm,0), gWorld).xyz;
    o.nrmW = normalize(nrmW);
    o.col = gColor;
    return o;
}
