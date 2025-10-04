// shaders/Starfield.hlsl

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;      // 0..1
};

cbuffer StarfieldCB : register(b0)
{
    float4 uCameraAndTime;  // x=cameraX, y=cameraY, z=time, w=exposure
    float4 uLayerScales;    // x=layer1Scale, y=layer2Scale, z=layer3Scale, w=jitterAmt
    float4 uStarWeights0;   // O, B, A, F
    float4 uStarWeights1;   // G, K, M, spawn
}

// Fullscreen triangle (no vertex buffer)
VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float2 verts[3] = {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0)
    };
    float2 p = verts[vid];
    o.pos = float4(p, 0.0, 1.0);
    // map to 0..1 UV
    o.uv  = p * 0.5 + 0.5;
    return o;
}

// Small hash helpers (deterministic per cell)
float hash11(float x) {
    x = frac(x * 0.1031);
    x *= x + 33.33;
    x *= x + x;
    return frac(x);
}
float2 hash22(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}
float  hash21(float2 p) { return frac(hash22(p).x + hash22(p + 17.0).y); }

// Build a CDF from weights for O,B,A,F,G,K,M
void buildStarTypeCdf(out float cdf[7], float4 w0, float3 w1)
{
    float w[7] = { w0.x, w0.y, w0.z, w0.w, w1.x, w1.y, w1.z };
    float s = w[0]+w[1]+w[2]+w[3]+w[4]+w[5]+w[6];
    // avoid div-by-zero; default to M if all zeros
    if (s <= 1e-6) { s = 1.0; w[6] = 1.0; }
    float acc = 0.0;
    [unroll]
    for (int i = 0; i < 7; ++i) { acc += w[i] / s; cdf[i] = acc; }
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
    return            float3(1.00, 0.70, 0.58); // M
}

// One procedural layer of stars
float3 starLayer(float2 uv, float2 cam, float cellScale, float spawn, float jitter, float time)
{
    // Stable coordinates in "starfield space"
    float2 P      = (uv + cam) * cellScale;
    float2 cell   = floor(P);
    float2 local  = frac(P) - 0.5;

    float r       = hash21(cell);
    if (r > spawn) return 0.0.xxx; // empty cell

    // jitter inside the cell so stars aren't on a grid
    float2 j      = (hash22(cell + 11.0) - 0.5) * (2.0 * jitter);
    float2 d      = local - j;

    // size and twinkle
    float szRand  = hash21(cell + 7.0);
    float sz      = lerp(0.002, 0.010, szRand);               // star radius in UV
    float tw      = 0.90 + 0.10 * sin(time * (2.0 + hash21(cell + 19.0) * 6.2831));
    float m       = smoothstep(sz, 0.0, length(d)) * tw;      // soft sprite

    // spectral color using weights
    float cdf[7];
    buildStarTypeCdf(cdf, uStarWeights0, uStarWeights1.xyz);
    float t       = hash21(cell + 23.0);
    int type      = 6; // default M
    [unroll]
    for (int i = 0; i < 7; ++i) { if (t <= cdf[i]) { type = i; break; } }
    float3 rgb    = colorForType(type);

    // emphasize bright/blue stars a bit
    float lumBias = lerp(0.9, 1.3, (type <= 2) ? 1.0 : 0.0); // O/B/A slightly brighter
    return rgb * m * lumBias;
}

float4 PS(VSOut i) : SV_Target
{
    float2 cam   = uCameraAndTime.xy;
    float  time  = uCameraAndTime.z;
    float  expo  = uCameraAndTime.w;
    float  spawn = uStarWeights1.w;
    float  jitter= saturate(uLayerScales.w);

    // Three parallaxed layers
    float3 col = 0;
    col += starLayer(i.uv, cam / uLayerScales.x, uLayerScales.x, spawn, jitter, time);
    col += starLayer(i.uv, cam / uLayerScales.y, uLayerScales.y, spawn, jitter, time);
    col += starLayer(i.uv, cam / uLayerScales.z, uLayerScales.z, spawn, jitter, time);

    // simple exposure + gamma
    col *= expo;
    col = pow(saturate(col), 1.0/2.2);

    return float4(col, 1.0);
}
