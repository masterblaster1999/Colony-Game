#include "../Common/CommonAtmosphere.hlsli"

Texture2D g_DummyTex : register(t0);
SamplerState g_LinearClamp : register(s0);

float3 tonemapACES(float3 x) {
    // Filmic-ish tone mapping
    const float a = 2.51; const float b = 0.03; const float c = 2.43; const float d = 0.59; const float e = 0.14;
    return saturate((x*(a*x+b)) / (x*(c*x+d)+e));
}

struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float4 main(PSIn i) : SV_Target {
    // Reconstruct view ray in world space from UV
    float2 ndc = i.uv * 2.0 - 1.0;
    float4 h = mul(g_InvViewProj, float4(ndc, 1, 1));
    float3 world = h.xyz / h.w;
    float3 dir = normalize(world - g_CameraPos);

    float mu = saturate(dot(dir, normalize(g_SunDirection)));

    // Cheap optical depths (no integration): horizon tinting by dir.y
    float hFactor = saturate(dir.y * 0.75 + 0.25);
    float3 betaR = g_BetaRayleigh;
    float3 betaM = g_BetaMie.xxx;

    float rPhase = phaseRayleigh(mu);
    float mPhase = phaseMie(mu, g_MieG);

    // Exponential falloff proxy by view elevation:
    float depthR = exp(-max(0.0, (1.0 - hFactor) * 1.5));
    float depthM = exp(-max(0.0, (1.0 - hFactor) * 0.25));

    float3 scatter = g_SunIntensity * (rPhase * betaR * depthR + mPhase * betaM * depthM);

    float3 col = tonemapACES(scatter);
    return float4(col, 1.0);
}
