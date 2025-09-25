// res/shaders/CS_FlowFinalize.hlsl
// Compress accumulation dynamic range and modulate by slope to estimate standing water / wetness.

Texture2D<float>   gAccum : register(t0);
Texture2D<float>   gSlope : register(t1);
RWTexture2D<float> gWater : register(u0);  // water depth/wetness

cbuffer CB_FlowFinal : register(b0) {
    float  gAccumK;   // e.g. 0.35 .. 1.2 (strength)
    float  gSlopeK;   // e.g. 0.75 .. 2.0 (drainage by slope)
    uint2  gDim;
    float2 _pad;
}

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gDim.x || id.y >= gDim.y) return;
    int2 p = int2(id.xy);

    // compress accumulation to a gentle curve
    float a = gAccum.Load(int3(p,0));
    float s = gSlope.Load(int3(p,0));
    float wet = gAccumK * log(1.0 + a);
    float drain = gSlopeK * s;
    float w = max(0.0, wet - drain);

    gWater[p] = w;
}
