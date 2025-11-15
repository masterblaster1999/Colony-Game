// ============================================================================ 
// Colony Game - common shader helpers  (Windows / D3D11/12)
// Extend/replace existing shaders/common/common.hlsli
// ============================================================================

#ifndef CG_COMMON_HLSLI
#define CG_COMMON_HLSLI

// DXC sometimes warns about pow(0,0) and similar; we clamp/guard below anyway.
#pragma warning( disable : 3571 )

// ---------- Tunables ---------------------------------------------------------

#ifndef CG_TONEMAP
    // 0 = Reinhard, 1 = Hable (Uncharted 2), 2 = ACES Fitted
    #define CG_TONEMAP 2
#endif

#ifndef CG_LUMA_BT
    // 709 luma by default; set to 2020 to switch
    #define CG_LUMA_BT 709
#endif

// If you present to an sRGB swapchain (DXGI_FORMAT_*_SRGB), define this to skip manual encode.
#ifndef CG_OUTPUT_SRGB
    #define CG_OUTPUT_SRGB 1
#endif

// Dither configuration:
// - CG_DITHER_ENABLED: integer flag for preprocessor (0/1) – SAFE for #if.
// - CG_DITHER_STRENGTH: actual float amount used in shader code.
//
// You can override these from the compile command line or another include
// *without* ever using floats in preprocessor conditions.
#ifndef CG_DITHER_ENABLED
    #define CG_DITHER_ENABLED 1
#endif

#ifndef CG_DITHER_STRENGTH
    // Amount of ordered-dither (in 0..1 LDR units).
    // Typical value ~ 1/255; set CG_DITHER_ENABLED 0 to disable.
    #define CG_DITHER_STRENGTH (1.0f / 255.0f)
#endif

// ---------- Constants / small utils -----------------------------------------

static const float CG_PI  = 3.14159265359f;
static const float CG_EPS = 1e-6f;

// Branchless clamp helpers (just wrap HLSL saturate to keep naming consistent)
float3 cg_saturate3(float3 v) { return saturate(v); }
float  cg_saturate (float  v) { return saturate(v); }

// Map [a,b] -> [c,d]
float cg_remap(float x, float a, float b, float c, float d)
{
    float t = (x - a) / max(b - a, CG_EPS);
    return lerp(c, d, saturate(t));
}

// ---------- Luminance (BT.709 / BT.2020) ------------------------------------
// Use linear RGB! (These are *relative luminance* weights)
float cg_luminance(float3 linearRGB)
{
#if   CG_LUMA_BT == 2020
    const float3 w = float3(0.2627f, 0.6780f, 0.0593f); // ITU-R BT.2020
#else // 709 (IEC 61966-2-1)
    const float3 w = float3(0.2126f, 0.7152f, 0.0722f); // ITU-R BT.709 / sRGB
#endif
    return dot(linearRGB, w);
}

// ---------- sRGB <-> linear (IEC 61966-2-1) ---------------------------------
// Scalar versions avoid vector conditionals; use per-channel wrappers.
float cg_linear_to_srgb1(float x)
{
    x = max(0.0f, x);
    return (x <= 0.0031308f) ? (x * 12.92f) : (1.055f * pow(x, 1.0f / 2.4f) - 0.055f);
}
float cg_srgb_to_linear1(float x)
{
    x = max(0.0f, x);
    return (x <= 0.04045f) ? (x / 12.92f) : pow((x + 0.055f) / 1.055f, 2.4f);
}
float3 cg_linear_to_srgb(float3 rgb)
{
    return float3(
        cg_linear_to_srgb1(rgb.r),
        cg_linear_to_srgb1(rgb.g),
        cg_linear_to_srgb1(rgb.b)
    );
}
float3 cg_srgb_to_linear(float3 rgb)
{
    return float3(
        cg_srgb_to_linear1(rgb.r),
        cg_srgb_to_linear1(rgb.g),
        cg_srgb_to_linear1(rgb.b)
    );
}

// ---------- Exposure ---------------------------------------------------------
float3 cg_apply_exposure(float3 hdr, float exposureEV)
{
    // exposureEV in stops; EV=0 is identity; positive EV brightens
    return hdr * exp2(exposureEV);
}

// ---------- Tone mapping -----------------------------------------------------
// All operators expect *linear* HDR RGB and return *linear* post-tonemap RGB.

// (0) Reinhard (global, simple)
float3 cg_tonemap_reinhard(float3 x)
{
    x = max(0.0f, x);
    return x / (1.0f + x);
}

// (1) Hable / Uncharted 2 (filmic; soft toe/shoulder)
// Parameters tuned to match Hable's paper, normalized by white point.
float3 cg_tonemap_hable(float3 x)
{
    x = max(0.0f, x);
    const float A = 0.15f;  const float B = 0.50f;
    const float C = 0.10f;  const float D = 0.20f;
    const float E = 0.02f;  const float F = 0.30f;
    const float W = 11.2f;  // linear white chosen by Hable for normalization

    float3 num   = (x * (A * x + C * B)) + D * E;
    float3 den   = (x * (A * x +     B)) + D * F;
    float3 curve = num / max(den, CG_EPS) - (E / F);

    float white  = ((W * (A * W + C * B)) + D * E)
                 / max((W * (A * W + B) + D * F), CG_EPS)
                 - (E / F);
    return curve / white;
}

// (2) ACES fitted (Narkowicz) – lightweight approx to ACES RRT+ODT
float3 cg_tonemap_aces(float3 x)
{
    x = max(0.0f, x);
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Unified entry (select by CG_TONEMAP)
float3 cg_tonemap(float3 linearRGB)
{
#if   CG_TONEMAP == 0
    return cg_tonemap_reinhard(linearRGB);
#elif CG_TONEMAP == 1
    return cg_tonemap_hable(linearRGB);
#else
    return cg_tonemap_aces(linearRGB);
#endif
}

// ---------- Fog utilities ----------------------------------------------------
// Returns fog *factor* in [0,1] to mix: lerp(scene, fogColor, fogFactor).
// distance d in world units from camera to shaded point.

float cg_fog_linear(float d, float startDist, float endDist)
{
    return saturate((d - startDist) / max(endDist - startDist, CG_EPS));
}

float cg_fog_exp(float d, float density)
{
    // transmittance T = exp(-density * d); fogFactor = 1 - T
    return 1.0f - exp(-density * d);
}

float cg_fog_exp2(float d, float density)
{
    float x = density * d;
    return 1.0f - exp(-x * x);
}

// Analytic height fog (exponential density vs altitude).
// cameraPos/worldPos are in world space; density0 is base density at y=0;
// falloff > 0 controls how quickly density decays with height (1/heightScale).
float cg_fog_height(float3 cameraPos, float3 worldPos, float density0, float falloff)
{
    // Integrate along the ray assuming density(y) = density0 * exp(-falloff * y).
    float3  dir = worldPos - cameraPos;
    float   L   = max(length(dir), CG_EPS);
    float3  V   = dir / L;

    // Closed-form approximation of integral of exp(-falloff * y) along the ray:
    // integral_0^L exp(-falloff * (cameraPos.y + t * V.y)) dt
    float ay = falloff * V.y;
    float t0 = exp(-falloff * cameraPos.y);
    float integral = (abs(ay) < 1e-4f)
        ? (t0 * L)
        : (t0 * (1.0f - exp(-ay * L)) / max(ay, CG_EPS));

    float transmittance = exp(-density0 * integral);
    return 1.0f - transmittance;
}

// Apply fog color in linear space.
float3 cg_apply_fog(float3 sceneLinear, float3 fogLinear, float fogFactor)
{
    return lerp(sceneLinear, fogLinear, saturate(fogFactor));
}

// ---------- Texture atlas helpers -------------------------------------------
// Atlas rect packs a sub-rectangle into [0,1] atlas UV space.
// rect.xy = minUV, rect.zw = sizeUV (fraction of whole atlas).
struct CGAtlasInfo
{
    float4 rect;          // xy: start, zw: size (all in 0..1 atlas UVs)
    float2 atlasSizePx;   // atlas pixel dimensions
    float2 tileSizePx;    // tile pixel dimensions (original content)
    int    padPx;         // padding pixels replicated around the tile
};

// UV pack: tileUV is in [0,1] inside the tile
float2 cg_atlas_pack_uv(float2 tileUV, float4 rect)
{
    return rect.xy + tileUV * rect.zw;
}

// Inset UVs by 'padPx' to keep bilinear/trilinear taps inside the tile.
float2 cg_atlas_inset_uv(float2 uvAtlas, CGAtlasInfo info)
{
    float2 inset = (info.padPx / max(info.atlasSizePx, float2(1.0f, 1.0f)));
    float2 minUV = info.rect.xy + inset;
    float2 maxUV = info.rect.xy + info.rect.zw - inset;
    return clamp(uvAtlas, minUV, maxUV);
}

// Optional: maximum *safe* mip level so that a footprint stays within tile.
// Conservative cap: stop when a 2x2 footprint would cross the tile edge.
float cg_atlas_max_mip(CGAtlasInfo info)
{
    float2 innerPx = max(info.tileSizePx - 2.0f * info.padPx, 1.0f);
    float  maxMip  = floor(log2(max(min(innerPx.x, innerPx.y), 1.0f)));
    return max(maxMip, 0.0f);
}

// Sample with optional LOD clamp (define CG_ATLAS_CLAMP_LOD to enable).
float4 cg_atlas_sample(Texture2D texAtlas, SamplerState samp, float2 tileUV, CGAtlasInfo info)
{
    float2 uvA = cg_atlas_pack_uv(tileUV, info.rect);
    uvA = cg_atlas_inset_uv(uvA, info);

#ifndef CG_ATLAS_CLAMP_LOD
    return texAtlas.Sample(samp, uvA);
#else
    // Derive the hardware LOD then clamp it
    uint w, h, mips;
    texAtlas.GetDimensions(0, w, h, mips);

    float2 dx = ddx(uvA) * float2(w, h);
    float2 dy = ddy(uvA) * float2(w, h);
    float  rho = max(dot(dx, dx), dot(dy, dy));
    float  lod = 0.5f * log2(max(rho, CG_EPS));
    lod = min(lod, cg_atlas_max_mip(info));
    return texAtlas.SampleLevel(samp, uvA, lod);
#endif
}

// ---------- Ordered dithering (4x4 Bayer) -----------------------------------
// Cheap way to hide LDR banding; feed integer pixel coords.
float cg_bayer4x4(int2 p)
{
    // 0..15 distributed; normalize to 0..1
    static const int B[16] =
    {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5
    };
    int idx = ((p.y & 3) << 2) | (p.x & 3);
    return (B[idx] + 0.5f) / 16.0f;
}

float3 cg_apply_dither(float3 ldr, int2 pix)
{
#if CG_DITHER_ENABLED
    return ldr + (cg_bayer4x4(pix) - 0.5f) * CG_DITHER_STRENGTH;
#else
    return ldr;
#endif
}

// ---------- Final encode helper ---------------------------------------------
// Call this at the very end before writing to the RTV.
float4 cg_encode_final(float3 linearRGB, int2 pixelPos /* SV_Position.xy */)
{
#if CG_OUTPUT_SRGB
    // Target is an sRGB backbuffer; let hardware apply the OETF
    float3 ldr = saturate(linearRGB);
    return float4(cg_apply_dither(ldr, pixelPos), 1.0f);
#else
    // Target is linear UNORM; encode to sRGB yourself
    float3 ldr = cg_linear_to_srgb(saturate(linearRGB));
    return float4(cg_apply_dither(ldr, pixelPos), 1.0f);
#endif
}

#endif // CG_COMMON_HLSLI
