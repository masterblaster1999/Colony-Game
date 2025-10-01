// src/pcg/shaders/noise_lib.hlsli
#ifndef NOISE_LIB_HLSLI
#define NOISE_LIB_HLSLI

// 32-bit avalanche hash
inline uint hash32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

// Map a 32-bit hash to [0,1)
inline float hash01(uint x)
{
    return (float)(hash32(x) >> 8) * (1.0 / 16777216.0);
}

// 2D value noise in [-1,1] with smooth interpolation.
inline float n2(float2 p, uint s)
{
    int2  ip = (int2)floor(p);
    float2 f = frac(p);

    uint2 u = asuint(ip);
    uint h00 = (u.x * 73856093u) ^ (u.y * 19349663u) ^ s;
    uint h10 = ((u.x + 1u) * 73856093u) ^ (u.y * 19349663u) ^ s;
    uint h01 = (u.x * 73856093u) ^ ((u.y + 1u) * 19349663u) ^ s;
    uint h11 = ((u.x + 1u) * 73856093u) ^ ((u.y + 1u) * 19349663u) ^ s;

    float v00 = hash01(h00);
    float v10 = hash01(h10);
    float v01 = hash01(h01);
    float v11 = hash01(h11);

    float2 t = f * f * (3.0 - 2.0 * f);
    float v0 = lerp(v00, v10, t.x);
    float v1 = lerp(v01, v11, t.x);
    float v  = lerp(v0,  v1,  t.y);
    return v * 2.0 - 1.0; // [0,1)->[-1,1]
}

#endif // NOISE_LIB_HLSLI
