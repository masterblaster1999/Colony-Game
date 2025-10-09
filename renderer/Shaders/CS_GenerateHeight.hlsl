// renderer/Shaders/CS_GenerateHeight.hlsl
// Compile with: cs_5_0

RWTexture2D<float> HeightOut : register(u0);

cbuffer Params : register(b0)
{
    uint2 Size;                   // output size in pixels
    uint  Seed;                   // RNG seed
    float Frequency;              // base frequency (e.g., 4–12)
    float Lacunarity;             // octave frequency multiplier (e.g., 2.0)
    float Gain;                   // octave amplitude multiplier (e.g., 0.5)
    uint  Octaves;                // number of octaves (e.g., 6–8)
    float ContinentFalloff;       // radius where falloff starts (0..1, e.g., 0.75)
    float HeightPower;            // exponent to shape mountains/seas (e.g., 1.1)
    float2 _pad;                  // 16B align
};

static const float2 HASHV = float2(127.1, 311.7);

// Hash to 0..1 without lookup tables (fast, deterministically seeded).
float hash21(float2 p)
{
    // Cheap float hash; ok for terrain. For higher quality, swap with a PCG hash.
    p = frac(p * 0.3183099 + float2(Seed * 0.000123, Seed * 0.000987));
    float n = dot(p, HASHV);
    return frac(sin(n) * 43758.5453);
}

// Smooth interpolation
float2 fade2(float2 t) { return t * t * (3.0 - 2.0 * t); }

// Value noise in 2D
float vnoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float a = hash21(i + float2(0,0));
    float b = hash21(i + float2(1,0));
    float c = hash21(i + float2(0,1));
    float d = hash21(i + float2(1,1));
    float2 u = fade2(f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y); // 0..1
}

// Fractional Brownian Motion
float fbm(float2 p)
{
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    [loop] for (uint i = 0; i < Octaves; ++i)
    {
        sum += amp * vnoise(p * freq);
        freq *= Lacunarity;
        amp  *= Gain;
    }
    return sum; // ~0..1 (depends on params)
}

[numthreads(8,8,1)]
void CS(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= Size.x || tid.y >= Size.y) return;

    float2 uv = (tid.xy + 0.5) / (float2)Size;
    float2 p  = uv * Frequency;

    // Domain warping: warp the sampling domain with fBM fields
    float2 q = float2(fbm(p + 3.1), fbm(p + float2(1.7, 9.2)));
    float2 r = float2(fbm(p + 4.0 * q + 11.0), fbm(p + 4.0 * q + 7.0));
    float  n = fbm(p + 4.0 * r);      // warped noise
    n = saturate(n);                  // clamp numeric noise sum

    // Normalize noise to 0..1 (heuristic for vnoise-based fBM)
    n = n * 0.5 + 0.25;

    // Soft radial 'continental' mask
    float2 c = uv * 2.0 - 1.0;
    float  rlen = length(c);
    float  mask = 1.0 - smoothstep(ContinentFalloff, 1.0, rlen);

    float h = pow(saturate(n * mask), HeightPower);
    HeightOut[tid.xy] = h; // 0..1 height
}
