// -----------------------------------------------------------------------------
// CS_SDFDither.hlsl — tiny SDF + dithering compute shader (D3D11, cs_5_0)
// Writes RGBA8 UNORM into UAV. Supports Bayer 8x8 or optional Blue-Noise SRV.
// -----------------------------------------------------------------------------

RWTexture2D<float4> gOutput : register(u0);

// Optional blue-noise (R8_UNORM) bound at t0 when enabled
Texture2D<float> gBlueNoise : register(t0);
SamplerState gBlueSamp : register(s0);

cbuffer SDFParams : register(b0)
{
    // .x = width, .y = height, .z = UseBayer (0/1), .w = UseBlue (0/1)
    uint Width;
    uint Height;
    uint UseBayer;
    uint UseBlue;

    // UV transform (x,y) -> (x*UVScale.x + UVOffset.x, y*UVScale.y + UVOffset.y)
    float2 UVScale;
    float2 UVOffset;

    // Circle
    float2 CircleCenter; // in UV space [0,1]
    float  CircleRadius; // in UV units
    float  AAPixel;      // AA half-width in UV units (e.g., 1.0/Height)

    // Rounded box
    float2 BoxCenter;
    float2 BoxHalf;      // half extents in UV units

    float  BoxRound;     // corner radius
    float  SmoothK;      // smooth union parameter (>=0, e.g., 0.01..0.1)
    float2 _Pad0;

    // Colors (linear)
    float4 FG; // inside color
    float4 BG; // outside color
}

// ---------------- Bayer 8x8 ---------------------------------------------------
static const uint BAYER8[64] = {
     0,48,12,60, 3,51,15,63,
    32,16,44,28,35,19,47,31,
     8,56, 4,52,11,59, 7,55,
    40,24,36,20,43,27,39,23,
     2,50,14,62, 1,49,13,61,
    34,18,46,30,33,17,45,29,
    10,58, 6,54, 9,57, 5,53,
    42,26,38,22,41,25,37,21
};

float bayer8_threshold(uint2 p)
{
    uint v = BAYER8[(p.x & 7u) + ((p.y & 7u) << 3)];
    return (v + 0.5f) / 64.0f;
}

// ---------------- SDF helpers -------------------------------------------------
float sdCircle(float2 p, float r) { return length(p) - r; }

float2 abs2(float2 v) { return float2(abs(v.x), abs(v.y)); }

float sdRoundBox(float2 p, float2 b, float r)
{
    float2 q = abs2(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float opSmoothUnion(float d1, float d2, float k)
{
    // Inigo Quilez-style smooth union
    // k ~ 0.01..0.1 in UV units
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return lerp(d2, d1, h) - k * h * (1.0 - h);
}

// AA coverage around the surface (signed distance d around 0)
float coverageAA(float d, float aa)
{
    // smoothstep(-aa, aa, -d) => 1 inside (d < 0), 0 outside, smooth at edge
    float t = saturate((aa - d) / (2.0 * aa));
    // remap to smoothstep shape
    return smoothstep(0.0, 1.0, t);
}

[numthreads(16,16,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= Width || tid.y >= Height) return;

    // Normalize pixel to [0,1] UV
    float2 uv = (float2(tid.xy) + 0.5) / float2(Width, Height);
    float2 p  = uv * UVScale + UVOffset;

    // SDFs in the same UV space
    float dCircle = sdCircle( p - CircleCenter, CircleRadius );
    float dBox    = sdRoundBox( p - BoxCenter, BoxHalf, BoxRound );

    float d = (SmoothK > 0.0) ? opSmoothUnion(dCircle, dBox, SmoothK)
                              : min(dCircle, dBox);

    float cov = coverageAA(d, AAPixel);

    // Ordered or blue-noise dithering -> binary alpha
    float a = cov;
    if (UseBayer != 0)
    {
        float th = bayer8_threshold(tid.xy);
        a = (cov > th) ? 1.0 : 0.0;
    }
    else if (UseBlue != 0)
    {
        // Sample 64x64 blue-noise; tile implicitly
        float2 bnUV = float2(tid.xy) / 64.0;
        float th = gBlueNoise.SampleLevel(gBlueSamp, bnUV, 0.0);
        a = (cov > th) ? 1.0 : 0.0;
    }

    float4 color = lerp(BG, FG, a);
    gOutput[tid.xy] = color;
}
