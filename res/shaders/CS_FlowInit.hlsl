// res/shaders/CS_FlowInit.hlsl
// Computes D8 outflow direction (0..7) and a simple slope metric (drop/distance).
// D8: O’Callaghan & Mark (1984). See also TauDEM & SAGA docs for conventions.

// Thread group
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID);

// --- Resources ---
Texture2D<float>       gHeight : register(t0);
RWTexture2D<float>     gSlope  : register(u0);  // drop/distance (tan theta)
RWTexture2D<uint>      gFlow   : register(u1);  // 0..7, 255 = pit/no descent

cbuffer CB_FlowInit : register(b0) {
    float2 gTexelSize;   // (1/W, 1/H)
    uint2  gDim;         // (W, H)
    float  gMinSlope;    // threshold to treat as pit (e.g. 1e-5f)
    float  _pad;
}

static const int2 OFFS[8] = {
    int2(+1,  0), int2(+1, +1), int2( 0, +1), int2(-1, +1),
    int2(-1,  0), int2(-1, -1), int2( 0, -1), int2(+1, -1)
};
static const float DIST[8] = { 1.0, 1.41421356, 1.0, 1.41421356, 1.0, 1.41421356, 1.0, 1.41421356 };

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gDim.x || id.y >= gDim.y) return;
    int2 p = int2(id.xy);

    float h = gHeight.Load(int3(p, 0));
    float best = -1e30;
    uint  bestIdx = 255;

    // Check 8 neighbors
    [unroll]
    for (uint i = 0; i < 8; ++i) {
        int2 q = p + OFFS[i];
        // clamp to edges (copy border behavior)
        q = clamp(q, int2(0,0), int2(int(gDim.x)-1, int(gDim.y)-1));
        float hn = gHeight.Load(int3(q, 0));
        float drop = h - hn;
        float s = drop / DIST[i];
        if (s > best) { best = s; bestIdx = i; }
    }

    // If not descending anywhere, it's a pit/flat
    if (best <= gMinSlope) {
        gSlope[p] = 0.0;
        gFlow[p]  = 255;
    } else {
        gSlope[p] = best;
        gFlow[p]  = bestIdx;
    }
}
