#ifndef CG_COMMON_HLSLI
#define CG_COMMON_HLSLI

// Keep HLSL matrices row-major to avoid surprise transposes when you
// start passing matrices from the CPU. Microsoft documents this pragma
// here: https://learn.microsoft.com/.../dx-graphics-hlsl-appendix-pre-pragma-pack-matrix
#pragma pack_matrix(row_major)

// -----------------------------
// Common constants & helpers
// -----------------------------
static const float CG_PI      = 3.14159265358979323846f;
static const float CG_EPSILON = 1e-6f;

// Vertex input for textured geometry / screen quads.
struct VSIn
{
    float3 Position : POSITION;   // from vertex buffer
    float2 Tex      : TEXCOORD0;  // UVs
};

// What the pixel shader wants to see.
// NOTE: SV_Position is the system-value position in clip space.
struct PSIn
{
    float4 Position : SV_Position;
    float2 Tex      : TEXCOORD0;
};

// Optional ‘richer’ vertex input you can use later for lighting.
struct VSInLit
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float3 Tangent  : TANGENT;
    float2 Tex      : TEXCOORD0;
};

// -----------------------------
// Color space helpers
// -----------------------------
inline float  cg_srgb_to_linear_1(float s) { return (s <= 0.04045f) ? (s / 12.92f) : pow((s + 0.055f) / 1.055f, 2.4f); }
inline float3 cg_srgb_to_linear(float3 c)  { return float3(cg_srgb_to_linear_1(c.r), cg_srgb_to_linear_1(c.g), cg_srgb_to_linear_1(c.b)); }

inline float  cg_linear_to_srgb_1(float s) { return (s <= 0.0031308f) ? (12.92f * s) : (1.055f * pow(s, 1.0f/2.4f) - 0.055f); }
inline float3 cg_linear_to_srgb(float3 c)  { return float3(cg_linear_to_srgb_1(c.r), cg_linear_to_srgb_1(c.g), cg_linear_to_srgb_1(c.b)); }

// ACES-ish tonemap (handy once you introduce HDR lighting).
inline float3 cg_aces_approx(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Texture atlas helper.
inline float2 cg_atlas_uv(float2 uv, float2 scale, float2 offset)
{
    return uv * scale + offset;
}

// Unpack tangent-space normal map sample from [0,1] to [-1,1].
inline float3 cg_unpack_normal(float3 n)
{
    return normalize(n * 2.0f - 1.0f);
}

#endif // CG_COMMON_HLSLI
