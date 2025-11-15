// ============================================================================
// Colony Game - common shader helpers  (Windows / D3D11/12)
// Drop-in replacement for shaders/common/common.hlsli
// ============================================================================

#ifndef CG_COMMON_HLSLI
#define CG_COMMON_HLSLI
#pragma warning( disable : 3571 ) // pow(0,0) etc., benign with clamps below

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

// Amount of ordered-dither (in 0..1 LDR units). Set to 0.0 to disable.
// NOTE: We *do not* use CG_DITHER_STRENGTH in #if expressions – DXC's
// preprocessor doesn't allow float expressions in #if.
#ifndef CG_DITHER_STRENGTH
    #define CG_DITHER_STRENGTH (1.0/255.0)
#endif

// ---------- Constants / small utils -----------------------------------------

static const float CG_PI  = 3.14159265359f;
static const float CG_EPS = 1e-6f;

// Branchless clamp
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
    const float3 w = float3(0.2627, 0.6780, 0.0593); // ITU-R BT.2020
#else // 709 (IEC 61966-2-1)
    const float3 w = float3(0.2126, 0.7152, 0.0722); // ITU-R BT.709 / sRGB
#endif
    return dot(linearRGB, w);
}

// ---------- sRGB <-> linear (IEC 61966-2-1) ---------------------------------
// Scalar versions avoid vector conditionals; use per-channel wrappers.
float cg_linear_to_srgb1(float x)
{
    x = max(0.0, x);
    return (x <= 0.0031308) ? (x * 12.92) : (1.055 * pow(x, 1.0 / 2.4) - 0.055);
}
float cg_srgb_to_linear1(float x)
{
    x = max(0.0, x);
    return (x <= 0.04045) ? (x / 12.92) : pow((x + 0.055) / 1.055, 2.4);
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
    x = max(0.0, x);
    return x / (1.0 + x);
}

// (1) Hable / Uncharted 2 (filmic; soft toe/shoulder)
float3 cg_tonemap_hable(float3 x)
{
    x = max(0.0, x);
    const float A = 0.15;  const float B = 0.50;
    const float C = 0.10;  const float D = 0.20;
    const float E = 0.02;  const float F = 0.30;
    const float W = 11.2;  // linear white chosen by Hable for normalization

    float3 num   = (x * (A * x + C * B)) + D * E;
    float3 den   = (x * (A * x +     B)) + D * F;
    float3 curve = num / max(den, CG_EPS) - (E / F);

    float white = ((W * (A * W + C * B)) + D * E) /
                  max((W * (A * W + B) + D * F), CG_EPS) - (E / F);
    return curve / white;
}

// (2) ACES fitted (Narkowicz) – lightweight approx to ACES RRT+ODT
float3 cg_tonemap_aces(float3 x)
{
    x = max(0.0, x);
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
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
    return 1.0 - exp(-density * d);
}

float cg_fog_exp2(float d, float density)
{
    float x = density * d;
    return 1.0 - exp(-x * x);
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
    float integral = (abs(ay) < 1e-4)
        ? (t0 * L)
        : (t0 * (1.0 - exp(-ay * L)) / max(ay, CG_EPS));

    float transmittance = exp(-density0 * integral);
    return 1.0 - transmittance;
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
    float2 inset = (info.padPx / max(info.atlasSizePx, float2(1.0, 1.0)));
    float2 minUV = info.rect.xy + inset;
    float2 maxUV = info.rect.xy + info.rect.zw - inset;
    return clamp(uvAtlas, minUV, maxUV);
}

// Optional: maximum *safe* mip level so that a footprint stays within tile.
// Conservative cap: stop when a 2x2 footprint would cross the tile edge.
float cg_atlas_max_mip(CGAtlasInfo info)
{
    float2 innerPx = max(info.tileSizePx - 2.0 * info.padPx, 1.0);
    float  maxMip  = floor(log2(max(min(innerPx.x, innerPx.y), 1.0)));
    return max(maxMip, 0.0);
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
    float2 dx  = ddx(uvA) * float2(w, h);
    float2 dy  = ddy(uvA) * float2(w, h);
    float  rho = max(dot(dx, dx), dot(dy, dy));
    float  lod = 0.5 * log2(max(rho, CG_EPS));
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
    return (B[idx] + 0.5) / 16.0;
}

// NOTE: we use a *runtime* branch instead of #if with floats to keep DXC happy.
float3 cg_apply_dither(float3 ldr, int2 pix)
{
    // If strength is ~0, compiler will constant-fold this away.
    if (CG_DITHER_STRENGTH <= 0.0)
        return ldr;

    return ldr + (cg_bayer4x4(pix) - 0.5) * (CG_DITHER_STRENGTH);
}

// ---------- Final encode helper ---------------------------------------------
// Call this at the very end before writing to the RTV.
float4 cg_encode_final(float3 linearRGB, int2 pixelPos /* SV_Position.xy */)
{
#if CG_OUTPUT_SRGB
    // Target is an sRGB backbuffer; let hardware apply the OETF
    float3 ldr = saturate(linearRGB);
    return float4(cg_apply_dither(ldr, pixelPos), 1.0);
#else
    // Target is linear UNORM; encode to sRGB yourself
    float3 ldr = cg_linear_to_srgb(saturate(linearRGB));
    return float4(cg_apply_dither(ldr, pixelPos), 1.0);
#endif
}

// ---------- Legacy fullscreen quad IO (quad_vs/quad_ps, etc.) ---------------
// Older Colony shaders expect a PSIn type from common.hlsli.
// This matches a simple screen-space quad: position + UV.
struct PSIn
{
    float4 position : SV_POSITION;
    float2 tex      : TEXCOORD0;
};

#endif // CG_COMMON_HLSLI
