// res/shaders/Starfield.hlsl
// Procedural starfield for DX11 (VS/PS). No textures, no vertex buffers.
// Layers of per-cell stars + time-based twinkle.
//
// Inputs from C++:
//   cbuffer b0: invResolution (1/width, 1/height), time (seconds), density (stars per layer scalar)

cbuffer StarCB : register(b0)
{
    float2 gInvResolution;  // 1/Width, 1/Height
    float  gTime;           // seconds
    float  gDensity;        // density scalar (1..~4 safe)
};

// Fullscreen triangle without a vertex buffer
struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    // Map 0..2 -> three clip-space vertices forming a big triangle
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOut o;
    o.uv  = uv; // 0..1 across the screen
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// ---------------------------
// Small hash helpers (no textures)
// ---------------------------
float hash21(float2 p) {
    // Simple, stable, and cheap. Not cryptographic.
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 78.233);
    return frac(p.x * p.y);
}
float2 hash22(float2 p) {
    float n = hash21(p);
    return float2(n, hash21(p + n));
}

// Radial star "blob" with soft falloff (gaussian-ish)
float starShape(float2 d, float r)
{
    // d: offset from star center (in cell UV units), r: effective radius
    // Avoid r=0 (fused add)
    float rr = max(r * r, 1e-5);
    float v  = exp(-dot(d, d) / rr); // smooth core, fast to compute
    return v;
}

// One procedural star layer. Returns RGB contribution.
float3 starLayer(float2 uv, float scale, float layerSeed, float time, float density)
{
    // Scale UV to cell space and find current cell
    float2 uvS   = uv * scale;
    float2 cell  = floor(uvS);
    float2 f     = frac(uvS) - 0.5f;

    // Random center offset inside the cell, random radius and color bias
    float2 r2    = hash22(cell + layerSeed) - 0.5f;   // [-0.5, 0.5]
    float  rnd   = hash21(cell * 1.7 + layerSeed);

    // Place one star per cell with some rejection to control density
    float  accept = step(1.0f - 0.85f * saturate(density), rnd); // more density -> fewer rejections
    if (accept < 0.5f) return 0.0f.xxx;

    float2 center = r2 * 0.40f; // star inside the cell
    float  baseR  = lerp(0.0020f, 0.0100f, rnd); // in cell UV units; small footprint

    // Twinkling: low-amplitude temporal modulation, randomized per cell
    float twkFreq = 6.0f + 10.0f * rnd;
    float twkAmp  = 0.15f;
    float twinkle = 1.0f + twkAmp * sin(time * twkFreq + (rnd * 31.0f));

    // Shape and color (slightly warm/cool bias)
    float  s   = starShape(f - center, baseR);
    float3 col = lerp(float3(0.85, 0.88, 1.00), float3(1.00, 0.95, 0.90), rnd);

    return col * s * twinkle;
}

float4 PSMain(VSOut i) : SV_Target
{
    // Normalize uv to [0,1]
    float2 uv = i.uv;

    // Parallax hooks: if you have a view direction per pixel, remap uv here.

    // Accumulate several layers at different scales for richness
    float  t   = gTime;
    float  den = max(gDensity, 0.2f);

    float3 acc = 0.0f.xxx;
    acc += starLayer(uv,  80.0f, 11.0f, t, den);
    acc += starLayer(uv, 140.0f, 37.0f, t, den);
    acc += starLayer(uv, 260.0f, 73.0f, t, den);
    acc += starLayer(uv, 520.0f, 97.0f, t, den * 0.7f);

    // Gentle tonemapping to avoid out-of-gamut spikes when added additively
    float3 color = acc / (1.0f + max(acc.x, max(acc.y, acc.z)));

    return float4(color, 1.0f);
}
