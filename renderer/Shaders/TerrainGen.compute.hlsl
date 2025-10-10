// renderer/Shaders/TerrainGen.compute.hlsl
// Domain-warped multi-octave noise -> height/normal maps for a terrain tile.
// Starter focused on clarity; tune threads, memory patterns, and precision for your renderer.
// References: fBm + domain warping (Quílez); normals by central differences. See notes below.

#ifndef THREADS_X
#define THREADS_X 8
#endif

#ifndef THREADS_Y
#define THREADS_Y 8
#endif

cbuffer TerrainGenCB : register(b0)
{
    uint2 MapSize;          // pixels of this tile (e.g., 512x512)
    float2 WorldOriginXZ;   // world-space origin of tile (meters)
    float  WorldTexel;      // meters per texel
    uint   Seed;            // noise seed

    int    Octaves;         // 3..8 typical
    float  BaseFrequency;   // cycles per meter (e.g., 0.0008)
    float  Lacunarity;      // ~2.0
    float  Gain;            // ~0.5

    float  WarpStrength;    // meters to warp XY domain (e.g., 40)
    float  HeightScale;     // meters amplitude for final height (e.g., 120)
    float  NormalSample;    // spatial epsilon for normal deriv (e.g., WorldTexel)
};

RWTexture2D<float>  OutHeight : register(u0);
RWTexture2D<float4> OutNormal : register(u1);

// ------------------------
// Hash & value noise (2D)
// ------------------------
float hash11(float n) {
    return frac(sin(n) * 43758.5453123);
}

float hash21(float2 p) {
    // Simple lattice hash via dot->sin->frac; good enough for terrain base.
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float a = hash21(i);
    float b = hash21(i + float2(1,0));
    float c = hash21(i + float2(0,1));
    float d = hash21(i + float2(1,1));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}

float fbm2D(float2 p, int octaves, float baseFreq, float lacunarity, float gain) {
    float amp = 0.5;
    float freq = baseFreq;
    float v = 0.0;
    [unroll]
    for (int i = 0; i < 12; ++i) {
        if (i >= octaves) break;
        v += amp * valueNoise2D(p * freq);
        freq *= lacunarity;
        amp  *= gain;
    }
    return v;
}

float terrainField(float2 xz) {
    // Domain warp (Quílez): perturb query point with two low‑freq noise channels.
    float2 pw = xz * (BaseFrequency * 0.5) + Seed * 0.123;
    float2 w = float2(valueNoise2D(pw + 37.0), valueNoise2D(pw + 19.0));
    w = (w * 2.0 - 1.0) * WarpStrength;       // meters
    float2 q = xz + w;

    // fBm mixture: base + a ridged accent (|.| inverted) to add crests.
    float n = fbm2D(q, Octaves, BaseFrequency, Lacunarity, Gain);
    float ridged = 1.0 - abs(2.0 * n - 1.0);
    return 0.72 * n + 0.28 * ridged;
}

float3 calcNormal(float2 xz) {
    // Sobel / central-difference style gradient in world units
    float eps = NormalSample;
    float hL = terrainField(xz - float2(eps, 0.0)) * HeightScale;
    float hR = terrainField(xz + float2(eps, 0.0)) * HeightScale;
    float hD = terrainField(xz - float2(0.0, eps)) * HeightScale;
    float hU = terrainField(xz + float2(0.0, eps)) * HeightScale;

    float3 dx = float3(2.0 * eps, hR - hL, 0.0);
    float3 dz = float3(0.0, hU - hD, 2.0 * eps);
    float3 n  = normalize(cross(dz, dx));
    return n;
}

[numthreads(THREADS_X, THREADS_Y, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= MapSize.x || dtid.y >= MapSize.y) return;

    float2 xz = WorldOriginXZ + float2(dtid.xy) * WorldTexel;

    float height = terrainField(xz) * HeightScale;
    OutHeight[dtid.xy] = height;

    float3 n = calcNormal(xz);
    // Store normal in 0..1 for convenience (xyz) + 1 alpha
    OutNormal[dtid.xy] = float4(n * 0.5 + 0.5, 1.0);
}
