// Buffer-less instanced billboard rendering: DrawInstanced(4, particleCount, 0, 0)
// VS uses SV_InstanceID to index particle buffer and expands a quad from SV_VertexID.

struct Particle { float3 pos; float life; float3 vel; float seed; };

StructuredBuffer<Particle> gParticles : register(t0);

cbuffer PrecipDrawCB : register(b0) {
    float4x4 g_ViewProj;
    float3 g_CamRight; float g_Size;
    float3 g_CamUp;    float g_Opacity;
}

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    float  a   : TEXCOORD1;
};

VSOut main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    Particle p = gParticles[instanceID];

    // Triangle strip quad order: 0:(-1,-1), 1:(1,-1), 2:(-1,1), 3:(1,1)
    float2 corner = float2((vertexID & 1) ? 1.0 : -1.0, (vertexID & 2) ? 1.0 : -1.0);
    float3 worldPos = p.pos + (g_CamRight * corner.x + g_CamUp * corner.y) * g_Size;

    VSOut o;
    o.pos = mul(g_ViewProj, float4(worldPos, 1));
    o.uv  = corner * 0.5 + 0.5;
    o.a   = saturate(p.life / 6.0) * g_Opacity;
    return o;
}
