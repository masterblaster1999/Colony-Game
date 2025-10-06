// demos/erosion/ErosionCS.hlsl
// Simple thermal-diffusion style step (stable for strength in (0, 0.25] with 4-neighbor).
// Input  : Texture2D<float>  (t0)  - source height (R32_FLOAT)
// Output : RWTexture2D<float> (u0) - destination height (R32_FLOAT)

cbuffer ErosionParams : register(b0)
{
    uint2 gSize;          // width, height
    float gStrength;      // 0 < strength <= 0.25 for stability with 4-neighborhood
    float gTalus;         // optional small threshold to avoid micro-oscillation (e.g. 0.0 .. 0.02)
};

Texture2D<float>    gSrc : register(t0);
RWTexture2D<float>  gDst : register(u0);

static const int2 OFFSETS[4] = { int2(-1,0), int2(1,0), int2(0,-1), int2(0,1) };

[numthreads(8,8,1)]
void CSMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gSize.x || tid.y >= gSize.y) return;

    int2 p = int2(tid.xy);

    float hC = gSrc[p];
    float sumDelta = 0.0f;

    // 4-neighbor Laplacian with optional talus threshold
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        int2 q = p + OFFSETS[i];
        if (q.x < 0 || q.x >= gSize.x || q.y < 0 || q.y >= gSize.y) continue;

        float hN = gSrc[q];
        float d  = hN - hC;

        // Optional small deadzone (talus) to reduce shimmering
        if (abs(d) > gTalus)
            sumDelta += d;
    }

    // Diffusion step: move toward neighbor average; stable for small gStrength
    float hNew = hC + gStrength * sumDelta;

    // Clamp to sane range (0..1) for the demo
    gDst[p] = saturate(hNew);
}
