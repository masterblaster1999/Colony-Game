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

struct VSIn { float3 pos : POSITION; };
struct VSOut {
    float4 posH : SV_Position;
    float4 col  : COLOR0;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    float4 posW = mul(float4(i.pos,1), gWorld);
    o.posH = mul(posW, gViewProj);
    o.col = gColor;
    return o;
}
