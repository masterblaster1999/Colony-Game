// src/pcg/shaders/noise_fbm_cs.hlsl
// SM 6.7 compute: fBM from value noise with a 32-bit integer hash.
// Windows / DXC only.

#include "noise_lib.hlsli"

// ----------------------------------------
// Thread group configuration
// ----------------------------------------
#ifndef NOISE_THREADS_X
  #define NOISE_THREADS_X 8
#endif
#ifndef NOISE_THREADS_Y
  #define NOISE_THREADS_Y 8
#endif

// ----------------------------------------
// Resources
// ----------------------------------------
RWTexture2D<float> OutTex : register(u0);

// Keep 16-byte packing rules in mind for cbuffers.
// 4 * 4B scalars per 16B register.
cbuffer NoiseParams : register(b0)
{
    uint2  OutputSize;    // xy
    float2 InvOutputSize; // 1/size, 1/size

    float  BaseFreq;      // e.g., 1.0
    float  Lacunarity;    // e.g., 2.0
    float  Gain;          // e.g., 0.5
    uint   Octaves;       // e.g., 6

    uint   Seed;          // base seed
    uint   _pad0;         // padding to keep 16B alignment
    float2 Offset;        // uv offset in noise domain
}

// ----------------------------------------
// Entry point
// ----------------------------------------
[numthreads(NOISE_THREADS_X, NOISE_THREADS_Y, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= OutputSize.x || tid.y >= OutputSize.y)
        return;

    // Normalized coords (center of texel) with user offset
    float2 uv = (float2(tid.xy) + 0.5) * InvOutputSize + Offset;

    float freq = BaseFreq;
    float amp  = 1.0;
    float val  = 0.0;

    // Fractal Brownian Motion
    [loop]
    for (uint o = 0; o < Octaves; ++o)
    {
        val += amp * n2(uv * freq, Seed + o * 1619u);
        freq *= Lacunarity;
        amp  *= Gain;
    }

    // Remap from ~[-OctaveSum, OctaveSum] to [0,1]; keep simple here
    // Caller can re-normalize as desired.
    OutTex[tid.xy] = saturate(val * 0.5 + 0.5);
}
