// Update particles on the GPU with wind from noise.

#include "../Common/Noise.hlsli"

struct Particle {
    float3 pos; float life;    // life in seconds
    float3 vel; float seed;    // seed for randomization
};

RWStructuredBuffer<Particle> gParticles : register(u0);

cbuffer PrecipUpdateCB : register(b0) {
    float3 g_CameraPos; float g_DT;
    float  g_TopY;      float g_GroundY;
    float  g_SpawnRadiusXZ; float g_Gravity; float g_WindStrength;
    float  g_Time;  float g_Snow; float2 g_Pad;
}

[numthreads(256,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x;
    Particle p = gParticles[i];

    // Simple wind: curl-ish from domain-warped noise in XZ
    float2 w = float2(perlin3(float3(p.pos.x*0.02, 0.0, p.pos.z*0.02 + g_Time*0.2)),
                      perlin3(float3(p.pos.z*0.02, 0.0, p.pos.x*0.02 - g_Time*0.2))) - 0.5;
    float3 wind = float3(w.x, 0, w.y) * g_WindStrength;

    float3 acc = float3(0, -g_Gravity, 0) + wind * (g_Snow > 0.5 ? 0.5 : 1.0);
    p.vel += acc * g_DT;
    p.pos += p.vel * g_DT;
    p.life -= g_DT;

    // Respawn if dead or below ground
    if (p.life <= 0 || p.pos.y <= g_GroundY) {
        float ang = perlin3(float3(p.seed, 0.0, p.seed*1.7)) * 6.28318;
        float rad = g_SpawnRadiusXZ * (0.5 + perlin3(float3(p.seed*2.3, 0.0, p.seed*0.7))*0.5);
        p.pos = g_CameraPos + float3(cos(ang)*rad, g_TopY - 0.1, sin(ang)*rad);
        p.vel = float3(0, - (g_Snow > 0.5 ? 6.0 : 18.0), 0); // snow slower than rain
        p.life = 6.0;
        p.seed += 13.37;
    }

    gParticles[i] = p;
}
