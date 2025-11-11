// -----------------------------------------------------------------------------
// Colony-Game - Common HLSL helpers (DX11/DX12 friendly)
// Path: renderer/Shaders/include/Common.hlsli
// -----------------------------------------------------------------------------
// This header centralizes constants, math, color utilities, BRDF helpers,
// sampling, packing, and a few safe, dependency-free building blocks for your
// shaders. Everything is prefixed with CG_ to avoid collisions.
//
// Compatible with FXC (SM 5.x) and DXC (SM 6.x).
//
// Optional cbuffers are *disabled by default* to avoid conflicts with existing
// shaders. Define CG_DEFINE_CAMERA_CB / CG_DEFINE_OBJECT_CB *before* including
// this file if you want them.
// -----------------------------------------------------------------------------

#ifndef CG_COMMON_HLSLI
#define CG_COMMON_HLSLI

// --------------------------------------
// Versioning & build toggles
// --------------------------------------
#define CG_COMMON_HLSLI_VERSION 0x010000 // 1.0.0

#ifndef COLONY_DEBUG
    #define COLONY_DEBUG 0
#endif

#ifndef CG_USE_LINEAR_WORKFLOW
    #define CG_USE_LINEAR_WORKFLOW 1
#endif

// --------------------------------------
// Numeric constants
// --------------------------------------
static const float  CG_PI        = 3.1415926535897932384626433832795f;
static const float  CG_HALF_PI   = 1.5707963267948966192313216916398f;
static const float  CG_TWO_PI    = 6.2831853071795864769252867665590f;
static const float  CG_INV_PI    = 0.3183098861837906715377675267450f;
static const float  CG_EPS       = 1e-5f;
static const float3 CG_EPS3      = float3(CG_EPS, CG_EPS, CG_EPS);

// --------------------------------------
// Utility macros
// --------------------------------------
#define CG_SAT(x)    saturate(x)
#define CG_SAT01(x)  saturate(x)
#define CG_LERP(a,b,t) lerp(a,b,t)

// Fast, branchless sign for vectors
float  CG_Sign(float  x) { return (x >= 0.0f) ? 1.0f : -1.0f; }
float2 CG_Sign(float2 v) { return float2(CG_Sign(v.x), CG_Sign(v.y)); }
float3 CG_Sign(float3 v) { return float3(CG_Sign(v.x), CG_Sign(v.y), CG_Sign(v.z)); }

// Safe normalize (guards against zero)
float3 CG_SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 <= CG_EPS) return float3(0,0,1);
    return v * rsqrt(len2);
}

// Safe reciprocal
float CG_RcpSafe(float x) { return (abs(x) > CG_EPS) ? (1.0f / x) : 0.0f; }

// Saturated dot helpers
float CG_SatDot(float3 a, float3 b) { return CG_SAT(dot(a, b)); }

// --------------------------------------
// sRGB <-> Linear conversions & luminance
// Accurate transfer functions per sRGB spec (Rec.709 primaries, D65)
// --------------------------------------
float  CG_srgbToLinear1(float cs)
{
    return (cs <= 0.04045f) ? (cs / 12.92f)
                            : pow((cs + 0.055f) / 1.055f, 2.4f);
}
float3 CG_srgbToLinear(float3 c) { return float3(CG_srgbToLinear1(c.r),
                                                 CG_srgbToLinear1(c.g),
                                                 CG_srgbToLinear1(c.b)); }

float  CG_linearToSrgb1(float cl)
{
    return (cl <= 0.0031308f) ? (12.92f * cl)
                              : (1.055f * pow(cl, 1.0f / 2.4f) - 0.055f);
}
float3 CG_linearToSrgb(float3 c) { return float3(CG_linearToSrgb1(c.r),
                                                 CG_linearToSrgb1(c.g),
                                                 CG_linearToSrgb1(c.b)); }

// Relative luminance (Rec.709)
float  CG_Luminance(float3 linearRgb)
{
    return dot(linearRgb, float3(0.2126f, 0.7152f, 0.0722f));
}

// --------------------------------------
// Optional common cbuffers (OFF by default)
// Define CG_DEFINE_CAMERA_CB and/or CG_DEFINE_OBJECT_CB before including.
// You can also override binding registers with CG_*_CB_REG.
// --------------------------------------
#ifndef CG_CAMERA_CB_REG
#define CG_CAMERA_CB_REG b0
#endif

#ifndef CG_OBJECT_CB_REG
#define CG_OBJECT_CB_REG b1
#endif

#ifdef CG_DEFINE_CAMERA_CB
cbuffer CGCameraCB : register(CG_CAMERA_CB_REG)
{
    float4x4 cg_View;
    float4x4 cg_Proj;
    float4x4 cg_ViewProj;
    float4x4 cg_InvView;
    float4x4 cg_InvProj;
    float4x4 cg_InvViewProj;

    float3   cg_CameraPos;
    float    cg_Time;

    float2   cg_ViewportSize;
    float2   cg_InvViewportSize;

    float    cg_DeltaTime;
    float3   cg__pad0;
};
#endif

#ifdef CG_DEFINE_OBJECT_CB
cbuffer CGObjectCB : register(CG_OBJECT_CB_REG)
{
    float4x4 cg_World;
    float4x4 cg_WorldInvTranspose;
    uint     cg_ObjectId;
    float3   cg__pad1;
};
#endif

// --------------------------------------
// Orthonormal basis (ONB) & TBN helpers
// Frisvad (2012), corrected by Duff et al. (2017) for robustness.
// Returns T,B given unit normal N. (Right-handed basis T x B = N.)
// --------------------------------------
void CG_BuildONB(float3 N, out float3 T, out float3 B)
{
    // Robust "revisited" form (Duff/Burgess/Christensen et al.)
    float s  = CG_Sign(N.z);
    float a  = -1.0f / (s + N.z);
    float b2 = N.x * N.y * a;

    T = float3(1.0f + s * N.x * N.x * a,
               s * b2,
              -s * N.x);

    B = float3(b2,
               s + N.y * N.y * a,
              -N.y);
}

// Build TBN from normal + optional tangent and handedness.
// If tangent is degenerate, falls back to ONB.
void CG_TBN(float3 N, float3 tangentWS, float handedness, out float3 T, out float3 B)
{
    float tLen2 = dot(tangentWS, tangentWS);
    if (tLen2 <= CG_EPS)
    {
        CG_BuildONB(CG_SafeNormalize(N), T, B);
        return;
    }

    T = normalize(tangentWS);
    B = normalize(cross(N, T)) * handedness;
    // Re-orthogonalize T (Gram-Schmidt) to reduce drift:
    T = normalize(cross(B, N));
}

// Transform a tangent-space normal (in [0,1]) to world-space using TBN.
float3 CG_NormalTS_To_WS(float3 normalTS01, float3 N, float3 T, float3 B, float strength)
{
    float3 nTS = normalize(normalTS01 * 2.0f - 1.0f);
    nTS.xy *= strength;
    nTS = normalize(nTS);
    // Avoid matrix mul orientation ambiguity; expand explicitly:
    float3 nWS = normalize(T * nTS.x + B * nTS.y + N * nTS.z);
    return nWS;
}

// --------------------------------------
// Microfacet BRDF helpers (GGX/Trowbridge-Reitz + Smith)
// Heitz 2014 (height-correlated visibility), Walter et al. 2007, Schlick Fresnel.
// Roughness is perceptual; alpha = roughness^2.
// --------------------------------------
float CG_RoughnessToAlpha(float roughness)
{
    roughness = CG_SAT(roughness);
    return max(roughness * roughness, 1e-4f);
}

// Schlick Fresnel (vector & scalar)
float3 CG_F_Schlick(float3 F0, float cosTheta)
{
    // Schlick 1994
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}
float  CG_F_Schlick(float  F0, float cosTheta)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

// GGX / Trowbridge-Reitz NDF (isotropic)
float CG_D_GGX(float NoH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 * CG_INV_PI / (d * d);
}

// Height-correlated Smith visibility (Heitz 2014), isotropic GGX
float CG_V_SmithGGX_Correlated(float NoV, float NoL, float alpha)
{
    float a2   = alpha * alpha;
    float lambdaV = NoL * sqrt(a2 + (1.0f - a2) * NoV * NoV);
    float lambdaL = NoV * sqrt(a2 + (1.0f - a2) * NoL * NoL);
    // 0.5 / (lambdaV + lambdaL) produces the common "V" visibility factor:
    return 0.5f / (lambdaV + lambdaL);
}

// Cook-Torrance microfacet BRDF (specular only)
float3 CG_BRDF_Spec_GGX(
    float3 N, float3 V, float3 L,
    float3 F0, float  roughness)
{
    float3 H   = normalize(V + L);
    float  NoV = CG_SAT(dot(N, V));
    float  NoL = CG_SAT(dot(N, L));
    float  NoH = CG_SAT(dot(N, H));
    float  VoH = CG_SAT(dot(V, H));

    float  alpha = CG_RoughnessToAlpha(roughness);
    float  D = CG_D_GGX(NoH, alpha);
    float  Vv = CG_V_SmithGGX_Correlated(NoV, NoL, alpha);
    float3 F = CG_F_Schlick(F0, VoH);

    // BRDF = D * V * F
    return (D * Vv) * F;
}

// Diffuse term (Lambert)
float3 CG_BRDF_Diffuse_Lambert(float3 albedoLinear)
{
    return albedoLinear * CG_INV_PI;
}

// --------------------------------------
// Sampling utilities
// --------------------------------------

// Radical inverse van der Corput base 2 (PBRT)
float CG_RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1)  | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2)  | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4)  | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8)  | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10f; // 1/2^32
}

float2 CG_Hammersley(uint i, uint N)
{
    return float2((float)i / (float)N, CG_RadicalInverse_VdC(i));
}

// Cosine-weighted hemisphere sample (z is up)
float3 CG_SampleHemisphereCosine(float2 u)
{
    float phi = CG_TWO_PI * u.x;
    float r   = sqrt(u.y);
    float x   = r * cos(phi);
    float y   = r * sin(phi);
    float z   = sqrt(max(0.0f, 1.0f - x * x - y * y));
    return float3(x, y, z);
}

// GGX VNDF (visible normals) sampling, isotropic (Heitz 2018).
// V must be in *local* (TBN) space with N=(0,0,1). To use in world-space:
//  1) Build T,B from N via CG_BuildONB / CG_TBN.
//  2) Transform V into local using that basis.
//  3) Sample with this function, then transform back to world.
float3 CG_SampleGGX_VNDF_Visible(float3 V, float2 U, float alpha)
{
    // Stretch view
    float3 Vh = normalize(float3(alpha * V.x, alpha * V.y, V.z));

    // Orthonormal basis
    float  lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1    = (lensq > 0.0f) ? float3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq)
                                  : float3(1.0f, 0.0f, 0.0f);
    float3 T2    = cross(Vh, T1);

    // Sample a point on the unit disk
    float r   = sqrt(U.x);
    float phi = CG_TWO_PI * U.y;
    float t1  = r * cos(phi);
    float t2  = r * sin(phi);

    // Warp
    float s   = 0.5f * (1.0f + Vh.z);
    t2        = (1.0f - s) * sqrt(max(0.0f, 1.0f - t1 * t1)) + s * t2;

    // Reproject
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

    // Unstretch
    float3 Ne = normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0f, Nh.z)));
    return Ne;
}

// --------------------------------------
// Octahedral normal packing (2x16f/2x8u friendly)
// Narkowicz 2014. Encode/Decode unit normals to 2D.
// --------------------------------------
float2 CG_OctEncode(float3 n)
{
    n = n / (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.xy;
    if (n.z < 0.0f)
    {
        e = (1.0f - abs(e.yx)) * CG_Sign(e);
    }
    return e;
}

float3 CG_OctDecode(float2 e)
{
    float3 n = float3(e.x, e.y, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
    {
        float2 t = (1.0f - abs(n.yx)) * CG_Sign(n.xy);
        n.x = t.x;
        n.y = t.y;
    }
    return CG_SafeNormalize(n);
}

// Pack/unpack to UNORM8x2 if needed
uint  CG_PackOctahedron8(float3 n)
{
    float2 e = CG_OctEncode(n) * 0.5f + 0.5f;
    e = CG_SAT(e);
    return (uint)(e.x * 255.0f + 0.5f) | (((uint)(e.y * 255.0f + 0.5f)) << 8);
}
float3 CG_UnpackOctahedron8(uint p)
{
    float2 e = float2(float(p & 0xFFu), float((p >> 8) & 0xFFu)) / 255.0f;
    e = e * 2.0f - 1.0f;
    return CG_OctDecode(e);
}

// --------------------------------------
// Lightweight integer hash / RNG
// --------------------------------------
uint  CG_PCG_Hash(uint v)
{
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    v = (v >> 22u) ^ v;
    return v;
}
float CG_Rand01(uint seed) { return (CG_PCG_Hash(seed) & 0x00FFFFFFu) * (1.0f / 16777216.0f); }

// --------------------------------------
// Depth helpers
// --------------------------------------
float CG_LinearizeDepth(float depth01, float zNear, float zFar)
{
    // D3D depth in [0,1]
    return (zFar * zNear) / (zFar - depth01 * (zFar - zNear));
}

// Given clip-space depth (0..1) and inverse view-projection, rebuild world pos.
// uv is in 0..1 screen space.
float3 CG_ReconstructWorldPos(float2 uv, float depth01, float4x4 invViewProj)
{
    float2 ndc = float2(uv * 2.0f - 1.0f);
    float4 p   = float4(ndc, depth01, 1.0f);
    float4 w   = mul(p, invViewProj); // row-vector convention
    return (w.xyz / w.w);
}

// --------------------------------------
// Debug helpers (compiled out in Release unless COLONY_DEBUG!=0)
// --------------------------------------
#if COLONY_DEBUG
    #define CG_DEBUG_ONLY(x) x
#else
    #define CG_DEBUG_ONLY(x)
#endif

#endif // CG_COMMON_HLSLI
