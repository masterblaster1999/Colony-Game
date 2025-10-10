// renderer/Shaders/CaveTriPlanarSplat.hlsl
// Forward shader that consumes VertMat (pos,nrm,materialId) and applies
// tri-planar texturing from Texture2DArray slices indexed by materialId.
//
// Notes:
//  • For robust tri-planar NORMAL mapping, see Ben Golus (we do albedo here for clarity).
//  • Adapt lighting to your engine's PBR as needed (simple Lambert below).

cbuffer DrawCB : register(b0)
{
    float4x4 World;           // if vertices already in WS, set to identity
    float4x4 ViewProj;
    float3   SunDir; float _pad0;
    float3   SunColor; float _pad1;
    float    AlbedoScale;     // texture tiling (meters → UV)
}

Texture2DArray MatAlbedo : register(t0); // array slice = materialId
SamplerState   LinearWrap : register(s0);

struct VSIn {
    float3 pos   : POSITION;
    float3 nrm   : NORMAL;
    uint   matId : TEXCOORD0; // Bind as DXGI_FORMAT_R32_UINT
};

struct VSOut {
    float4 pos    : SV_Position;
    float3 wsPos  : TEXCOORD0;
    float3 wsNrm  : TEXCOORD1;
    nointerpolation uint matId : MATID; // do not interpolate materialId
};

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 wpos = mul(World, float4(i.pos,1));
    o.wsPos = wpos.xyz;
    o.wsNrm = normalize(mul((float3x3)World, i.nrm));
    o.pos   = mul(ViewProj, wpos);
    o.matId = i.matId;
    return o;
}

// Basic tri-planar sampling of albedo only (no normal map for brevity)
float3 TriplanarAlbedo(uint matId, float3 wsPos, float3 wsNrm)
{
    float3 n = abs(normalize(wsNrm)) + 1e-5;
    float sum = n.x + n.y + n.z;
    float3 w = n / sum;

    float2 uvX = wsPos.zy * AlbedoScale;
    float2 uvY = wsPos.xz * AlbedoScale;
    float2 uvZ = wsPos.xy * AlbedoScale;

    float3 cx = MatAlbedo.Sample(LinearWrap, float3(uvX, matId)).rgb;
    float3 cy = MatAlbedo.Sample(LinearWrap, float3(uvY, matId)).rgb;
    float3 cz = MatAlbedo.Sample(LinearWrap, float3(uvZ, matId)).rgb;

    return cx*w.x + cy*w.y + cz*w.z;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 albedo = TriplanarAlbedo(i.matId, i.wsPos, i.wsNrm);

    // Simple one-bounce diffuse with a single directional
    float3 L = normalize(-SunDir);
    float ndl = saturate(dot(normalize(i.wsNrm), L));
    float3 color = albedo * (0.1 + ndl) * SunColor;  // tiny ambient + direct

    return float4(color, 1.0);
}
