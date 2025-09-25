// res/shaders/CS_FlowAccumIter.hlsl
// One Jacobi-style gather iteration:
//   accumNext[x] = baseRain + sum_{neighbors n that flow into x} accumPrev[n] * carry
// Converges to upstream contributing area with D8 flow dirs after O(path length) iterations.

// Thread group
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID);

// --- Resources ---
Texture2D<uint>        gFlow     : register(t0);  // D8 dirs (from CS_FlowInit)
Texture2D<float>       gAccumPrev: register(t1);  // previous iteration
RWTexture2D<float>     gAccumNext: register(u0);  // write next iteration

cbuffer CB_FlowAccum : register(b0) {
    float  gBaseRain;   // e.g. 1.0 (each cell contributes at least itself)
    float  gCarry;      // 1.0 for D8 count; <1.0 to attenuate long chains
    uint2  gDim;        // (W,H)
    float2 _pad;
}

static const int2 OFFS[8] = {
    int2(+1,  0), int2(+1, +1), int2( 0, +1), int2(-1, +1),
    int2(-1,  0), int2(-1, -1), int2( 0, -1), int2(+1, -1)
};

// Return the opposite direction index (neighbor->me) given my neighbor index
// If neighbor offset is OFFS[k], then neighbor->me is (k+4) mod 8.
uint Opp(uint k) { return (k + 4u) & 7u; }

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gDim.x || id.y >= gDim.y) return;
    int2 p = int2(id.xy);

    float accum = gBaseRain; // rainfall/source term

    // Gather from neighbors that flow into me
    [unroll]
    for (uint k = 0; k < 8; ++k) {
        int2 n = p + OFFS[k];
        if (n.x < 0 || n.y < 0 || n.x >= int(gDim.x) || n.y >= int(gDim.y)) continue;

        uint dirN = gFlow.Load(int3(n, 0));
        if (dirN == Opp(k)) {
            float aN = gAccumPrev.Load(int3(n, 0));
            accum += aN * gCarry;
        }
    }

    gAccumNext[p] = accum;
}
