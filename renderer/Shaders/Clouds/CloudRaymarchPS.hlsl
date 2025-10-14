#include "../Common/CommonAtmosphere.hlsli"

Texture3D<float> g_CloudTex : register(t0);
SamplerState g_LinearBorder : register(s0);

cbuffer CloudRaymarchCB : register(b2) {
    float3 g_CloudMin; float g_CloudMaxHeight;   // world-space AABB y-range
    float3 g_CloudMax; float g_StepCount;        // e.g., stepCount=64
    float  g_SigmaExt; float g_SigmaScat; float g_ShadowStep; float g_ShadowSigma; // scattering params
}

struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float3 tonemapACES(float3 x){
    const float a=2.51,b=0.03,c=2.43,d=0.59,e=0.14;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

// Slab intersection with horizontal cloud layer [minY,maxY]
bool intersectLayer(float3 ro, float3 rd, float y0, float y1, out float t0, out float t1) {
    if (abs(rd.y) < 1e-4) { t0 = t1 = 0; return false; }
    float ty0 = (y0 - ro.y)/rd.y;
    float ty1 = (y1 - ro.y)/rd.y;
    t0 = min(ty0, ty1);
    t1 = max(ty0, ty1);
    return t1 > 0.0 && t1 > t0;
}

float sampleCloud(float3 worldPos) {
    float3 uvw = saturate((worldPos - g_CloudMin) / max(g_CloudMax - g_CloudMin, 1e-3));
    return g_CloudTex.SampleLevel(g_LinearBorder, uvw, 0);
}

// Cheap sun shadow: march a few coarse steps towards sun
float sunShadow(float3 pos, float3 sunDir, float stepLen) {
    float T = 1.0;
    [unroll(8)]
    for (int i=0; i<8; ++i) {
        pos += sunDir * stepLen;
        float d = sampleCloud(pos);
        T *= exp(-d * g_ShadowSigma);
        if (T < 0.05) break;
    }
    return T;
}

float4 main(PSIn i) : SV_Target {
    // World ray
    float2 ndc = i.uv * 2.0 - 1.0;
    float4 h = mul(g_InvViewProj, float4(ndc, 1, 1));
    float3 world = h.xyz / h.w;
    float3 rd = normalize(world - g_CameraPos);
    float3 ro = g_CameraPos;

    float t0, t1;
    if (!intersectLayer(ro, rd, g_CloudMin.y, g_CloudMaxHeight, t0, t1)) {
        return float4(0,0,0,0);
    }
    t0 = max(t0, 0.0);
    float dt = (t1 - t0) / g_StepCount;

    float T = 1.0;            // transmittance
    float3 L = 0.0;           // accumulated light
    float3 sunDir = normalize(g_SunDirection);
    float mu = dot(rd, sunDir);
    float phase = phaseRayleigh(mu) + phaseMie(mu, g_MieG);

    [loop]
    for (int s=0; s<(int)g_StepCount; ++s) {
        float t = t0 + dt * (s + 0.5);
        float3 p = ro + rd * t;
        float dens = sampleCloud(p);

        if (dens > 0.001) {
            float Tshad = sunShadow(p, sunDir, g_ShadowStep);
            float atten = exp(-g_SigmaExt * dens * dt);
            float scat  = g_SigmaScat * dens * dt;

            L += T * scat * Tshad * g_SunIntensity * phase;
            T *= atten;
            if (T < 0.01) break;
        }
    }

    float3 col = tonemapACES(L);
    return float4(col, 1.0 - T); // alpha = cloud opacity
}
