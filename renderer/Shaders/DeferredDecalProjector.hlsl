// renderer/Shaders/DeferredDecalProjector.hlsl
// Box-projected screen-space decal. Render a unit cube with DecalWorld matrix.
// The pixel shader reconstructs world-space from depth, transforms into decal
// local space, and if inside the box, blends a decal albedo onto the scene.
//
// This is a generic example; adapt targets to your GBuffer setup.
// References: Bart Wronski (screen-space decals), IceFall (deferred decals), Unity URP decals.
//
// Vertex input: unit cube mesh (POSITION)
// Outputs: overlay color to a decal target (or directly to base color in a composite pass).

cbuffer DecalCB : register(b0)
{
    float4x4 DecalWorld;        // transforms unit cube -> world
    float4x4 ViewProj;
    float4x4 InvViewProj;
    float4x4 WorldToDecal;      // inverse(DecalWorld)
    float2   InvScreenSize; float2 _pad0;

    float4   DecalColor;        // tint
    float    Opacity;    float  EdgeFade;   float _pad1; float _pad2;
}

Texture2D    SceneDepth   : register(t0); // device-depth (0..1)
Texture2D    DecalAlbedo  : register(t1);
SamplerState LinearClamp  : register(s0);
SamplerState LinearWrap   : register(s1);

struct VSIn  { float3 pos : POSITION; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 wpos = mul(DecalWorld, float4(i.pos,1));
    o.pos = mul(ViewProj, wpos);
    o.uv  = 0.5 * (o.pos.xy / o.pos.w) + 0.5; // screen uv
    return o;
}

// Reconstruct WS from depth + NDC
float3 ReconstructWS(float2 uv, float depth01)
{
    float2 ndcXY = uv * 2.0 - 1.0;
    float4 ndc = float4(ndcXY, depth01 * 2.0 - 1.0, 1.0);
    float4 ws = mul(InvViewProj, ndc);
    return ws.xyz / ws.w;
}

float4 PSMain(VSOut i) : SV_Target
{
    // Sample scene depth at this pixel
    float depth = SceneDepth.Sample(LinearClamp, i.uv).r;

    // Reconstruct world position
    float3 wsPos = ReconstructWS(i.uv, depth);

    // Transform into decal local
    float4 lp4 = mul(WorldToDecal, float4(wsPos, 1.0));
    float3 lp  = lp4.xyz; // decal local space, cube assumed in [-0.5,0.5]^3

    // Inside the projector volume?
    if (any(abs(lp) > 0.5)) discard;

    // UVs from local XY (planar projection); could be tri-planar if desired
    float2 duv = lp.xy + 0.5; // 0..1
    float4 decalTex = DecalAlbedo.Sample(LinearWrap, duv) * DecalColor;

    // Soft edge fade toward box boundaries
    float fadeX = 1.0 - smoothstep(0.5-EdgeFade, 0.5, abs(lp.x));
    float fadeY = 1.0 - smoothstep(0.5-EdgeFade, 0.5, abs(lp.y));
    float fadeZ = 1.0 - smoothstep(0.5-EdgeFade, 0.5, abs(lp.z));
    float fade  = fadeX * fadeY * fadeZ;

    float a = saturate(Opacity * decalTex.a * fade);

    // Output: pre-multiplied decal contribution (overlay). Route to your composite.
    return float4(decalTex.rgb * a, a);
}
