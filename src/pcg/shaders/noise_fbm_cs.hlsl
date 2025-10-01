// src/pcg/shaders/noise_fbm_cs.hlsl
// SM 6.7 compute: fBM from value noise with a 32-bit integer hash.
// Windows / DXC only.

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
// Declarations (so usage can precede definitions)
// ----------------------------------------
uint  hash32(uint x);
float n2(float2 p, uint s);

// ----------------------------------------
// Definitions
// ----------------------------------------

// "wyhash"/PCG-style avalanching integer hash (32-bit)
uint hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

// Map a 32-bit hash to [0,1)
float hash01(uint x)
{
    // Take the upper 24 bits (uniform mantissa) for float precision
    return (float)(hash32(x) >> 8) * (1.0 / 16777216.0);
}

// 2D value noise in [-1,1] with smooth interpolation.
// 'p' is in noise-space (continuous); 's' is a salt/seed.
float n2(float2 p, uint s)
{
    int2  ip = (int2)floor(p);
    float2 f = frac(p);

    // Hash lattice corners (use distinct large primes for mixing).
    // asuint(int2) keeps tiling stable for negative coords.
    uint2 u = asuint(ip);
    uint h00 = (u.x * 73856093u)  ^ (u.y * 19349663u)  ^ s;
    uint h10 = ((u.x + 1u) * 73856093u) ^ (u.y * 19349663u)  ^ s;
    uint h01 = (u.x * 73856093u)  ^ ((u.y + 1u) * 19349663u) ^ s;
    uint h11 = ((u.x + 1u) * 73856093u) ^ ((u.y + 1u) * 19349663u) ^ s;

    float v00 = hash01(h00);
    float v10 = hash01(h10);
    float v01 = hash01(h01);
    float v11 = hash01(h11);

    // Smoothstep-like fade
    float2 t = f * f * (3.0 - 2.0 * f);
    float v0 = lerp(v00, v10, t.x);
    float v1 = lerp(v01, v11, t.x);
    float v  = lerp(v0,  v1,  t.y);

    // Map [0,1) -> [-1,1]
    return v * 2.0 - 1.0;
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
