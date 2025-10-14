cbuffer AtmosphereCB : register(b0) {
    float3 g_SunDirection;  float  g_SunIntensity;
    float3 g_CameraPos;      float  g_MieG;          // ~0.8
    float3 g_BetaRayleigh;   float  g_Pad0;          // e.g. (5.5e-6, 13.0e-6, 22.4e-6)
    float3 g_BetaMie;        float  g_Pad1;          // scalar -> rgb
    float   g_PlanetRadius;  float  g_AtmosphereRadius;
    float2  g_Pad2;
}

cbuffer CameraCB : register(b1) {
    float4x4 g_InvViewProj; // to get world ray dir from uv
}

// Phase functions
static const float INV4PI = 0.07957747154594767;
float phaseRayleigh(float mu) {
    return (3.0 * INV4PI / 4.0) * (1.0 + mu*mu);
}
float phaseMie(float mu, float g) {
    float g2 = g*g;
    return (3.0 * INV4PI) * ((1.0 - g2) * (1.0 + mu*mu)) / ((2.0 + g2) * pow(abs(1.0 + g2 - 2.0*g*mu), 1.5));
}
