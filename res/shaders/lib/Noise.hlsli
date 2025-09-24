// ============================================================================
// Colony Game - Noise Library (HLSL/HLSL6) - Public domain / Unlicense style.
// File: res/shaders/lib/Noise.hlsli
//
// What you get (high level):
//  - hash: 1/2/3D float + integer-seeded hashing (deterministic on GPU)
//  - value noise (2D/3D), gradient noise (fast & higher-quality variants)
//  - finite-difference gradient for noises (for normals/flow)
//  - FBM variants: standard, billow, turbulence, ridged multifractal
//  - domain warp helpers (+ seedable, multi-warp)
//  - periodic (tileable) gradient noise
//  - Worley/Voronoi cellular noise (F1/F2 + cell id)
//  - curl noise (divergence-free 2D vector field)
//  - quasi-random Hammersley points, tiny Bayer dithers
//  - utilities: remap, rotate2, normalFromHeight, etc.
//
// Legacy compatibility preserved:
//  - float2 hash22(float2 p)
//  - float  gnoise2(float2 p)
//  - float  fbm(float2 p)
//  - float  terrainHeight(float2 x)
//
// All functions are inline-friendly. No external textures, includes, or
// constant buffers required. Intended for SM5+.
//
// ----------------------------------------------------------------------------
// Tunables (override before including to change defaults):
//    #define NOISE_DEFAULT_OCTAVES 6
//    #define NOISE_EPS  (1.0/1024.0)
//    #define NOISE_HASHIZE_SIN     0   // 0=int-mix, 1=sin-based hashes
//    #define NOISE_USE_QUINTIC_FADE 1  // fade5 vs. cubic fade3
//    #define NOISE_ENABLE_DOUBLEWARP 1
// ============================================================================

#ifndef COLONY_NOISE_HLSLI
#define COLONY_NOISE_HLSLI 1

#ifndef NOISE_DEFAULT_OCTAVES
#define NOISE_DEFAULT_OCTAVES 6
#endif

#ifndef NOISE_EPS
#define NOISE_EPS (1.0/1024.0)
#endif

#ifndef NOISE_HASHIZE_SIN
#define NOISE_HASHIZE_SIN 0
#endif

#ifndef NOISE_USE_QUINTIC_FADE
#define NOISE_USE_QUINTIC_FADE 1
#endif

#ifndef NOISE_ENABLE_DOUBLEWARP
#define NOISE_ENABLE_DOUBLEWARP 1
#endif

// ----------------------------------------------------------------------------
// Constants & small utilities
// ----------------------------------------------------------------------------
static const float  NOISE_PI     = 3.14159265359;
static const float  NOISE_TWOPI  = 6.28318530718;
static const float  NOISE_INV_U32 = 1.0 / 4294967296.0; // 1/2^32

inline float  sqr(float x)            { return x*x; }
inline float2 sqr(float2 x)           { return x*x; }
inline float3 sqr(float3 x)           { return x*x; }
inline float  saturate01(float x)     { return saturate(x); }
inline float2 saturate01(float2 x)    { return saturate(x); }
inline float3 saturate01(float3 x)    { return saturate(x); }

inline float  remap(float v, float2 inMinMax, float2 outMinMax)
{
    float t = (v - inMinMax.x) / (inMinMax.y - inMinMax.x);
    return lerp(outMinMax.x, outMinMax.y, t);
}

inline float2 rotate2(float2 p, float angle)
{
    float s = sin(angle), c = cos(angle);
    return float2(c*p.x - s*p.y, s*p.x + c*p.y);
}

// Smoothstep-like Perlin fades
inline float fade3(float t)  { return t*t*(3.0 - 2.0*t); }
inline float2 fade3(float2 t){ return t*t*(3.0 - 2.0*t); }
inline float fade5(float t)  { return t*t*t*(t*(t*6.0 - 15.0) + 10.0); }
inline float2 fade5(float2 t){ float2 t2 = t*t, t3 = t2*t; return t3*(t*(t*6.0 - 15.0) + 10.0); }

#define NOISE_FADE_3 0
#define NOISE_FADE_5 1

inline float2 chooseFade(float2 f, int fadeKind)
{
    return (fadeKind == NOISE_FADE_5) ? fade5(f) : fade3(f);
}

// ----------------------------------------------------------------------------
// Integer mix hash (fast & stable) + float hash frontends
// ----------------------------------------------------------------------------
inline uint hash_u32(uint x)
{
    // Thomas Wang / PCG-style finalizers
    x ^= x * 747796405u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    x *= 3266489917u;
    x ^= x >> 16;
    return x;
}

inline uint hash_u32(uint x, uint seed) { return hash_u32(x ^ seed); }

inline uint hash_u32(uint2 v, uint seed)
{
    uint h = hash_u32(v.x + 0x9E3779B9u * v.y, seed);
    return hash_u32(h + 0x85EBCA6Bu);
}

inline uint hash_u32(uint3 v, uint seed)
{
    uint h = hash_u32(v.x ^ (v.y * 0x9E3779B9u) ^ (v.z * 0x85EBCA6Bu), seed);
    return hash_u32(h + 0xC2B2AE35u);
}

inline float  hash11(uint x, uint seed=0)     { return (hash_u32(x,seed)) * NOISE_INV_U32; }
inline float  hash12(int2  p, uint seed=0)    { return (hash_u32(asuint(p),seed)) * NOISE_INV_U32; }
inline float  hash13(int3  p, uint seed=0)    { return (hash_u32(asuint(p),seed)) * NOISE_INV_U32; }
inline float2 hash22i(int2 p, uint seed=0)    { uint h=hash_u32(asuint(p),seed); return float2(hash_u32(h+0x68E31DA4u), hash_u32(h+0xB5297A4Du))*NOISE_INV_U32; }
inline float3 hash33i(int3 p, uint seed=0)    { uint h=hash_u32(asuint(p),seed); return float3(hash_u32(h+0x68E31DA4u),hash_u32(h+0xB5297A4Du),hash_u32(h+0x1B56C4E9u))*NOISE_INV_U32; }

#if NOISE_HASHIZE_SIN
// Optional sin-based float hash (slower on some GPUs but compact)
inline float2 hash22(float2 p)
{
    float n = sin(dot(p, float2(127.1, 311.7))) * 43758.5453;
    float m = sin(dot(p, float2(269.5, 183.3))) * 43758.5453;
    return frac(float2(n, m));
}
#else
// Legacy-compatible float hash22 (from your snippet), kept as default.
inline float2 hash22(float2 p)
{
    p = frac(p * float2(5.3983, 5.4427));
    p += dot(p, p + 19.19);
    return frac(float2(p.x * p.y, p.x + p.y));
}
#endif

// Also provide float->float hash helpers
inline float hash21(float2 p, uint seed=0)
{
    // Fold to int grid for determinism, then add local fractional jitter
    int2  i = int2(floor(p));
    float2 f = frac(p);
    float  a = hash12(i, seed);
    float  b = hash12(i + int2(1,0), seed);
    float  c = hash12(i + int2(0,1), seed);
    float  d = hash12(i + int2(1,1), seed);
    float2 u = chooseFade(f, NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3);
    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}

// ----------------------------------------------------------------------------
// Value Noise (cheap, useful for masks)
// ----------------------------------------------------------------------------
inline float valueNoise2(float2 p, uint seed=0, int fadeKind = NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3)
{
    int2 i = int2(floor(p));
    float2 f = frac(p);
    float a = hash12(i, seed);
    float b = hash12(i + int2(1,0), seed);
    float c = hash12(i + int2(0,1), seed);
    float d = hash12(i + int2(1,1), seed);
    float2 u = chooseFade(f, fadeKind);
    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y) * 2.0 - 1.0; // center to ~[-1,1]
}

inline float valueNoise3(float3 p, uint seed=0, int fadeKind = NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3)
{
    int3 i = int3(floor(p));
    float3 f = frac(p);
    float c000 = hash13(i, seed);
    float c100 = hash13(i + int3(1,0,0), seed);
    float c010 = hash13(i + int3(0,1,0), seed);
    float c110 = hash13(i + int3(1,1,0), seed);
    float c001 = hash13(i + int3(0,0,1), seed);
    float c101 = hash13(i + int3(1,0,1), seed);
    float c011 = hash13(i + int3(0,1,1), seed);
    float c111 = hash13(i + int3(1,1,1), seed);
    float3 u = float3(chooseFade(f.xy, fadeKind), (fadeKind==NOISE_FADE_5)?fade5(f.z):fade3(f.z));
    float x00 = lerp(c000, c100, u.x);
    float x10 = lerp(c010, c110, u.x);
    float x01 = lerp(c001, c101, u.x);
    float x11 = lerp(c011, c111, u.x);
    float y0  = lerp(x00, x10, u.y);
    float y1  = lerp(x01, x11, u.y);
    return lerp(y0, y1, u.z) * 2.0 - 1.0;
}

// ----------------------------------------------------------------------------
// Gradient Noise (improved Perlin-style) - 2D (fast and high-quality variants)
// ----------------------------------------------------------------------------

// Fast variant: like your original, using random gradients in [-0.5,+0.5]^2
inline float gnoise2_fast(float2 p, uint seed=0, int fadeKind = NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = chooseFade(f, fadeKind);

    float2 g00 = hash22(i + float2(0,0)) - 0.5;
    float2 g10 = hash22(i + float2(1,0)) - 0.5;
    float2 g01 = hash22(i + float2(0,1)) - 0.5;
    float2 g11 = hash22(i + float2(1,1)) - 0.5;

    float a = dot(g00, f - float2(0,0));
    float b = dot(g10, f - float2(1,0));
    float c = dot(g01, f - float2(0,1));
    float d = dot(g11, f - float2(1,1));

    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}

// Higher-quality gradients on the unit circle (costs a few sines)
inline float2 grad2_from_hash(int2 i, uint seed)
{
    float h = hash12(i, seed);
    float ang = h * NOISE_TWOPI;
    float s, c; sincos(ang, s, c);
    return float2(c, s);
}

inline float gnoise2_precise(float2 p, uint seed=0, int fadeKind = NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3)
{
    int2 i = int2(floor(p));
    float2 f = frac(p);
    float2 u = chooseFade(f, fadeKind);

    float2 g00 = grad2_from_hash(i + int2(0,0), seed);
    float2 g10 = grad2_from_hash(i + int2(1,0), seed);
    float2 g01 = grad2_from_hash(i + int2(0,1), seed);
    float2 g11 = grad2_from_hash(i + int2(1,1), seed);

    float a = dot(g00, f - float2(0,0));
    float b = dot(g10, f - float2(1,0));
    float c = dot(g01, f - float2(0,1));
    float d = dot(g11, f - float2(1,1));

    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}

// Periodic / tileable 2D gradient noise with integer period (e.g. int2(256,256))
inline float gnoise2_periodic(float2 p, int2 period, uint seed=0, int fadeKind = NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3)
{
    int2 i = int2(floor(p));
    float2 f = frac(p);
    // Wrap grid coordinates into [0,period)
    int2 w00 = ( (i              % period) + period ) % period;
    int2 w10 = ( (i + int2(1,0)) % period + period ) % period;
    int2 w01 = ( (i + int2(0,1)) % period + period ) % period;
    int2 w11 = ( (i + int2(1,1)) % period + period ) % period;

    float2 g00 = hash22i(w00, seed) - 0.5;
    float2 g10 = hash22i(w10, seed) - 0.5;
    float2 g01 = hash22i(w01, seed) - 0.5;
    float2 g11 = hash22i(w11, seed) - 0.5;

    float2 u = chooseFade(f, fadeKind);
    float a = dot(g00, f - float2(0,0));
    float b = dot(g10, f - float2(1,0));
    float c = dot(g01, f - float2(0,1));
    float d = dot(g11, f - float2(1,1));

    return lerp(lerp(a,b,u.x), lerp(c,d,u.x), u.y);
}

// 3D gradient noise (unit-circle gradients projected; adequate for volumes)
inline float gnoise3(float3 p, uint seed=0, int fadeKind = NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3)
{
    int3 i = int3(floor(p));
    float3 f = frac(p);
    float3 u = float3(chooseFade(f.xy, fadeKind), (fadeKind==NOISE_FADE_5)?fade5(f.z):fade3(f.z));

    float3 g000 = normalize(hash33i(i, seed) - 0.5);
    float3 g100 = normalize(hash33i(i + int3(1,0,0), seed) - 0.5);
    float3 g010 = normalize(hash33i(i + int3(0,1,0), seed) - 0.5);
    float3 g110 = normalize(hash33i(i + int3(1,1,0), seed) - 0.5);
    float3 g001 = normalize(hash33i(i + int3(0,0,1), seed) - 0.5);
    float3 g101 = normalize(hash33i(i + int3(1,0,1), seed) - 0.5);
    float3 g011 = normalize(hash33i(i + int3(0,1,1), seed) - 0.5);
    float3 g111 = normalize(hash33i(i + int3(1,1,1), seed) - 0.5);

    float a = dot(g000, f - float3(0,0,0));
    float b = dot(g100, f - float3(1,0,0));
    float c = dot(g010, f - float3(0,1,0));
    float d = dot(g110, f - float3(1,1,0));
    float e = dot(g001, f - float3(0,0,1));
    float g = dot(g101, f - float3(1,0,1));
    float h = dot(g011, f - float3(0,1,1));
    float k = dot(g111, f - float3(1,1,1));

    float x00 = lerp(a,b,u.x);
    float x10 = lerp(c,d,u.x);
    float x01 = lerp(e,g,u.x);
    float x11 = lerp(h,k,u.x);
    float y0  = lerp(x00,x10,u.y);
    float y1  = lerp(x01,x11,u.y);
    return lerp(y0,y1,u.z);
}

// ----------------------------------------------------------------------------
// Finite-difference gradient (noise + d/dx, d/dy) - handy for normals/flow
// ----------------------------------------------------------------------------
inline float3 gnoise2_df(float2 p, uint seed=0)
{
    float n  = gnoise2_precise(p, seed);
    float nx = gnoise2_precise(p + float2(NOISE_EPS, 0.0), seed);
    float ny = gnoise2_precise(p + float2(0.0, NOISE_EPS), seed);
    float dx = (nx - n) / NOISE_EPS;
    float dy = (ny - n) / NOISE_EPS;
    return float3(n, dx, dy);
}

// ----------------------------------------------------------------------------
// FBM & fractal flavors
// ----------------------------------------------------------------------------
inline float fbm_flexible(float2 p, int octaves, float lacunarity, float gain, float amp0,
                          int fadeKind = NOISE_USE_QUINTIC_FADE ? NOISE_FADE_5 : NOISE_FADE_3,
                          uint seed=0)
{
    float a = 0.0;
    float amp = amp0;
    float2 q = p;
    [unroll]
    for(int i=0; i<octaves; ++i)
    {
        a += amp * gnoise2_precise(q, seed + (uint)i*37u, fadeKind);
        q *= lacunarity;
        amp *= gain;
    }
    return a;
}

inline float fbm(float2 p) // legacy-compatible default (6 octaves, 0.5 gain)
{
    float a=0.0, amp=0.5;
    float2 q = p;
    [unroll]
    for(int i=0;i<NOISE_DEFAULT_OCTAVES;++i){ a+=amp*gnoise2_fast(q); q*=2.02; amp*=0.5; }
    return a;
}

inline float fbm3(float3 p, int octaves=NOISE_DEFAULT_OCTAVES, float lacunarity=2.02, float gain=0.5, uint seed=0)
{
    float a=0.0, amp=0.5;
    float3 q=p;
    [unroll]
    for(int i=0;i<octaves;++i){ a+=amp*gnoise3(q, seed + (uint)i*53u); q*=lacunarity; amp*=gain; }
    return a;
}

inline float billow(float2 p, int octaves=NOISE_DEFAULT_OCTAVES, float lacunarity=2.02, float gain=0.5, uint seed=0)
{
    float s = 0.0;
    float a = 0.5;
    float2 q = p;
    [unroll]
    for(int i=0;i<octaves;++i){ s += a * (2.0*abs(gnoise2_precise(q, seed+(uint)i*41u)) - 1.0); q *= lacunarity; a *= gain; }
    return s;
}

inline float turbulence(float2 p, int octaves=NOISE_DEFAULT_OCTAVES, float lacunarity=2.02, float gain=0.5, uint seed=0)
{
    float s = 0.0, a = 0.5; float2 q=p;
    [unroll]
    for(int i=0;i<octaves;++i){ s += a * abs(gnoise2_precise(q, seed+(uint)i*97u)); q *= lacunarity; a *= gain; }
    return s;
}

inline float ridgedMF(float2 p, int octaves=NOISE_DEFAULT_OCTAVES, float lacunarity=2.0, float gain=0.5, float ridgeSharpness=0.9, uint seed=0)
{
    float sum = 0.0;
    float amp = 0.5;
    float prev = 1.0;
    float2 q = p;
    [unroll]
    for(int i=0;i<octaves;++i)
    {
        float n = gnoise2_precise(q, seed+(uint)i*131u);
        n = 1.0 - abs(n);         // ridges
        n = pow(saturate(n), ridgeSharpness*2.0 + 1.0);
        sum += n * amp * prev;
        prev = n;
        q *= lacunarity;
        amp *= gain;
    }
    return sum;
}

// ----------------------------------------------------------------------------
// Domain warp helpers
// ----------------------------------------------------------------------------
inline float2 warp2(float2 p, float amp=5.0, float freq=0.06, uint seed=0)
{
    float2 w = float2(fbm_flexible(p*freq, 4, 2.0, 0.5, 0.5, NOISE_FADE_5, seed+11u),
                      fbm_flexible(p*freq + 17.3, 4, 2.0, 0.5, 0.5, NOISE_FADE_5, seed+71u));
    return w * amp;
}

inline float2 warp2_multi(float2 p, float amp=6.0, float f0=0.06, float f1=0.13, uint seed=0)
{
#if NOISE_ENABLE_DOUBLEWARP
    float2 w0 = warp2(p, amp*0.6, f0, seed+123u);
    float2 w1 = warp2(p + w0, amp*0.4, f1, seed+987u);
    return w0 + w1;
#else
    return warp2(p, amp, f0, seed);
#endif
}

inline float warp_fbm(float2 p, int octaves=6, float lacunarity=2.02, float gain=0.5,
                      float warpAmp=5.0, float warpFreq=0.06, uint seed=0)
{
    float2 w = warp2_multi(p, warpAmp, warpFreq, warpFreq*2.17, seed);
    return fbm_flexible(p*0.12 + w, octaves, lacunarity, gain, 0.5, NOISE_FADE_5, seed);
}

// ----------------------------------------------------------------------------
// Cellular (Worley/Voronoi) Noise (2D)
// ----------------------------------------------------------------------------
struct Cellular2D { float2 cell; float2 nearest; float F1; float F2; };

// Euclidean metric
inline float _cellDist(float2 d) { return dot(d,d); } // we keep squared; sqrt at the end

inline Cellular2D worley2(float2 p, uint seed=0)
{
    int2 ip = int2(floor(p));
    float2 fp = frac(p);

    float F1 = 1e9, F2 = 1e9;
    float2 nearest = 0.0;
    float2 cell    = 0.0;

    // Check 3x3 neighborhood
    [unroll]
    for(int j=-1; j<=1; ++j)
    {
        [unroll]
        for(int i=-1; i<=1; ++i)
        {
            int2 g = ip + int2(i,j);
            float2 r = hash22i(g, seed) - 0.5;           // feature point in cell
            float2 q = (float2)g + r - p;
            float d2 = _cellDist(q);
            if(d2 < F1) { F2 = F1; F1 = d2; nearest = (float2)g + r; cell=float2(g); }
            else if(d2 < F2) { F2 = d2; }
        }
    }

    Cellular2D C;
    C.cell    = cell;
    C.nearest = nearest;
    C.F1      = sqrt(F1);
    C.F2      = sqrt(F2);
    return C;
}

// Voronoi-style edges (white lines at cell borders). 'sharp' ~ 2..12
inline float voronoiEdges(float2 p, float sharp=8.0, uint seed=0)
{
    Cellular2D c = worley2(p, seed);
    return saturate( (c.F2 - c.F1) * sharp );
}

// A cell ID (0..1) you can use to color regions
inline float voronoiCellID(float2 p, uint seed=0)
{
    return frac( hash12( int2(floor(p)), seed ) * 13.61803398875 );
}

// ----------------------------------------------------------------------------
// Curl Noise (2D divergence-free field) using potential noise
// ----------------------------------------------------------------------------
inline float2 curlNoise2(float2 p, uint seed=0)
{
    float3 ndf = gnoise2_df(p*1.0, seed);
    // ∇⊥ φ = ( ∂φ/∂y, -∂φ/∂x )
    return float2(ndf.z, -ndf.y);
}

// ----------------------------------------------------------------------------
// Dithering + Hammersley (sampling helpers)
// ----------------------------------------------------------------------------
inline uint reverseBits32(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
    return bits;
}

inline float radicalInverse_VdC(uint i) { return (float)reverseBits32(i) * NOISE_INV_U32; }

inline float2 hammersley(uint i, uint N, uint seed=0)
{
    return float2( (i + hash11(seed)) / (float)N, radicalInverse_VdC(i ^ seed) );
}

// 4x4 Bayer dither (0..1)
inline float ditherBayer4x4(int2 pix)
{
    static const int M[4][4]={
        {0,  8,  2, 10},
        {12, 4, 14,  6},
        {3, 11,  1,  9},
        {15, 7, 13,  5}
    };
    int v = M[pix.y & 3][pix.x & 3];
    return (v + 0.5) / 16.0;
}

// ----------------------------------------------------------------------------
// Normals from height (using gradient you compute or finite difference here)
// ----------------------------------------------------------------------------
inline float3 normalFromHeight(float2 grad, float strength=1.0)
{
    // grad = (dH/dx, dH/dy)
    float3 n = float3(-grad.x * strength, 1.0, -grad.y * strength);
    return normalize(n);
}

// ----------------------------------------------------------------------------
// Advanced terrain recipe (domain warp + blend of FBM and ridges)
// ----------------------------------------------------------------------------
struct TerrainOut { float height; float ridged; float mask; };

inline TerrainOut terrainHeight_advanced(float2 x, uint seed=0)
{
    float2 warp = warp2_multi(x, 6.0, 0.06, 0.12, seed);
    float hBase = fbm_flexible(x*0.12 + warp, 7, 2.0, 0.5, 0.5, NOISE_FADE_5, seed+100u);
    float ridg  = ridgedMF( (x+warp)*0.18, 6, 2.0, 0.5, 0.9, seed+200u);
    float mask  = voronoiEdges( x*0.05 + warp*0.03, 8.0, seed+300u); // for biomes/strata masks
    float h     = 0.75*hBase + 0.35*ridg;
    TerrainOut o; o.height=h; o.ridged=ridg; o.mask=mask; return o;
}

// ----------------------------------------------------------------------------
// LEGACY-COMPATIBLE WRAPPERS (keep your existing calls working)
// ----------------------------------------------------------------------------

// Original minimal 2D gradient noise (name kept, but upgraded to precise fade5)
inline float gnoise2(float2 p)
{
    // You can switch to gnoise2_precise or gnoise2_fast by changing this line.
    return gnoise2_precise(p);
}

// Original FBM signature (6 octaves, amp halving)
inline float fbm_legacy(float2 p){ return fbm(p); }

// Overload keeps the exact original name `fbm` for drop-in
inline float fbm(float2 p, int octaves){ return fbm_flexible(p, octaves, 2.02, 0.5, 0.5, NOISE_FADE_5, 0u); }

// Original terrainHeight, now using improved warp + ridges
inline float terrainHeight(float2 x)
{
    float2 warp = float2( fbm(x*0.06), fbm(x*0.06 + 17.3) ) * 5.0; // still deterministic
    // Upgrade internals a bit but keep general look/scale:
    float h = fbm_flexible(x*0.12 + warp, 6, 2.02, 0.5, 0.5, NOISE_FADE_5, 0u);
    float ridged = 1.0 - abs(gnoise2((x+warp)*0.2)); // crisp ridges
    return 0.7*h + 0.3*ridged; // ~0..1 (remap in PS/VS as needed)
}

// ----------------------------------------------------------------------------
// Handy extras you might call directly
// ----------------------------------------------------------------------------
inline float terrainHeight_simple(float2 x, uint seed=0)
{
    float2 warp = float2( fbm(x*0.06), fbm(x*0.06+17.3) ) * 5.0;
    float h = fbm_flexible(x*0.12 + warp, 6, 2.02, 0.5, 0.5, NOISE_FADE_5, seed);
    float ridged = 1.0 - abs(gnoise2_precise((x+warp)*0.2, seed));
    return 0.7*h + 0.3*ridged;
}

#endif // COLONY_NOISE_HLSLI
