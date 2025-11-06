// renderer/Shaders/Batch2D_vs.hlsl
struct VSIn  { float2 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 svpos : SV_POSITION; float4 col : COLOR; };
cbuffer Ortho : register(b0) { float4x4 uOrtho; } // if you want it, can be identity for pixel-sized coords
VSOut VSMain(VSIn i) {
    VSOut o;
    o.svpos = mul(float4(i.pos, 0, 1), uOrtho);
    o.col   = i.col;
    return o;
}
