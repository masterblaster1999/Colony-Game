// shaders/Starfield.hlsl
//
// Fullscreen procedural starfield used as a sky/background.
//
// - Renders via a fullscreen triangle using SV_VertexID
// - Star distribution based on spectral type weights (O,B,A,F,G,K,M)
// - Designed for SM6 / DXC-style toolchain
//
// Expected usage on the C++ side:
//   * Bind StarfieldCB to b0
//   * Draw 3 vertices with no vertex buffer bound
//   * Use VSMain / PSMain (or mainVS / main) as entry points
//

// -----------------------------------------------------------------------------
// Vertex <-> Pixel interface
// -----------------------------------------------------------------------------
struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;      // 0..1
};

// -----------------------------------------------------------------------------
// Constant buffer
// -----------------------------------------------------------------------------
cbuffer StarfieldCB : register(b0)
{
    // x = cameraX, y = cameraY, z = time, w = exposure
    float4 uCameraAndTime;

    // x = layer1Scale, y = layer2Scale, z = layer3Scale, w = jitterAmt
    float4 uLayerScales;

    // Star type weights: O, B, A, F
    float4 uStarWeights0;

    // Star type weights: G, K, M, spawnProbability
    float4 uStarWeights1;
};

// -----------------------------------------------------------------------------
// Fullscreen triangle vertex shader (buffer-less)
// -----------------------------------------------------------------------------
VSOut StarfieldVS(uint vid : SV_VertexID)
{
    VSOut o;

    // Hard-coded fullscreen triangle in clip space
    float2 verts[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };

    float2 p = verts[vid];
    o.pos = float4(p, 0.0, 1.0);

    // Map from clip-space (-1..1) to UV (0..1)
    o.uv = p * 0.5 + 0.5;

    return o;
}

// -----------------------------------------------------------------------------
// Hash helpers
// -----------------------------------------------------------------------------
float hash11(float x)
{
    x = frac(x * 0.1031);
    x *= x + 33.33;
    x *= x + x;
    return frac(x);
}

float2 hash22(float2 p)
{
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float hash21(float2 p)
{
    return frac(hash22(p).x + hash22(p + 17.0).y);
}

// -----------------------------------------------------------------------------
// Build CDF from spectral type weights (O,B,A,F,G,K,M)
// -----------------------------------------------------------------------------
void buildStarTypeCdf(out float cdf[7], float4 w0, float3 w1)
{
    float w[7] = {
        w0.x, w0.y, w0.z, w0.w,   // O, B, A, F
        w1.x, w1.y, w1.z          // G, K, M
    };

    float s = w[0] + w[1] + w[2] + w[3] + w[4] + w[5] + w[6];

    // Avoid div-by-zero; default to M stars if all zero
    if (s <= 1e-6)
    {
        s    = 1.0;
        w[6] = 1.0;
    }

    float acc = 0.0;
    [unroll]
    for (int i = 0; i < 7; ++i)
    {
        acc   += w[i] / s;
        cdf[i] = acc;
    }
}

// Approx star color per spectral type (rough tints)
float3 colorForType(int t)
{
    // O,B,A,F,G,K,M → bluish → reddish
    if (t == 0) return float3(0.64, 0.76, 1.00); // O
    if (t == 1) return float3(0.68, 0.78, 1.00); // B
    if (t == 2) return float3(0.76, 0.83, 1.00); // A
    if (t == 3) return float3(0.95, 0.96, 1.00); // F
    if (t == 4) return float3(1.00, 0.93, 0.76); // G
    if (t == 5) return float3(1.00, 0.82, 0.62); // K

    return float3(1.00, 0.70, 0.58);             // M (catch-all)
}

// -----------------------------------------------------------------------------
// One procedural layer of stars
// -----------------------------------------------------------------------------
float3 starLayer(
    float2 uv,
    float2 cam,
    float  cellScale,
    float  spawn,
    float  jitter,
    float  time,
    float  cdf[7])
{
    // Early-out if this layer is disabled
    if (spawn <= 0.0)
        return float3(0.0, 0.0, 0.0);

    // Stable coordinates in "starfield space"
    float2 P     = (uv + cam) * cellScale;
    float2 cell  = floor(P);
    float2 local = frac(P) - 0.5;

    float r = hash21(cell);
    if (r > spawn)
        return float3(0.0, 0.0, 0.0); // empty cell

    // Jitter inside the cell so stars aren't on a perfect grid
    float2 j = (hash22(cell + 11.0) - 0.5) * (2.0 * jitter);
    float2 d = local - j;

    // Size and twinkle
    float  szRand = hash21(cell + 7.0);
    float  sz     = lerp(0.002, 0.010, szRand); // star radius in UV
    float  phase  = time * (2.0 + hash21(cell + 19.0) * 6.2831);
    float  tw     = 0.90 + 0.10 * sin(phase);

    float  dist   = length(d);
    float  m      = smoothstep(sz, 0.0, dist) * tw;  // soft sprite

    // Choose spectral type via CDF
    float  tRand = hash21(cell + 23.0);
    int    type  = 6;                                // default to M
    [unroll]
    for (int i = 0; i < 7; ++i)
    {
        if (tRand <= cdf[i])
        {
            type = i;
            break;
        }
    }

    float3 rgb = colorForType(type);

    // Emphasize bright/blue stars a bit
    float lumBias = (type <= 2) ? 1.3 : 0.9;         // O/B/A slightly brighter
    return rgb * m * lumBias;
}

// -----------------------------------------------------------------------------
// Pixel shader
// -----------------------------------------------------------------------------
float4 StarfieldPS(VSOut i) : SV_Target
{
    float2 cam    = uCameraAndTime.xy;
    float  time   = uCameraAndTime.z;
    float  expo   = max(uCameraAndTime.w, 0.0);      // avoid negative exposure

    // Clamp parameters to sane ranges
    float spawn   = saturate(uStarWeights1.w);
    float jitter  = saturate(uLayerScales.w);

    // Build star-type CDF once per pixel, re-use per layer
    float cdf[7];
    buildStarTypeCdf(cdf, uStarWeights0, uStarWeights1.xyz);

    float3 col = float3(0.0, 0.0, 0.0);

    // Three parallaxed layers (bigger scale = denser / finer pattern)
    col += starLayer(i.uv, cam / max(uLayerScales.x, 1e-3), uLayerScales.x, spawn, jitter, time, cdf);
    col += starLayer(i.uv, cam / max(uLayerScales.y, 1e-3), uLayerScales.y, spawn, jitter, time, cdf);
    col += starLayer(i.uv, cam / max(uLayerScales.z, 1e-3), uLayerScales.z, spawn, jitter, time, cdf);

    // Exposure + simple gamma
    col *= expo;
    col  = pow(saturate(col), 1.0 / 2.2);

    return float4(col, 1.0);
}

// -----------------------------------------------------------------------------
// Entry point wrappers
//
// These exist so you can point your build system at *any* of the
// common names without touching this file again.
// -----------------------------------------------------------------------------
VSOut VSMain(uint vid : SV_VertexID)
{
    return StarfieldVS(vid);
}

VSOut mainVS(uint vid : SV_VertexID)
{
    return StarfieldVS(vid);
}

float4 PSMain(VSOut i) : SV_Target
{
    return StarfieldPS(i);
}

float4 main(VSOut i) : SV_Target
{
    return StarfieldPS(i);
}
