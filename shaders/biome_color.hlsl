// Simple debug pixel shader that colors a biome ID texture (R8_UINT).
// Bindings (adapt to your renderer):
// Texture2D<uint> BiomeTex : register(t0);
// SamplerState Samp       : register(s0);

static const float3 BIOME_COLORS[11] = {
    float3(0,0.27,0.55),   // Ocean
    float3(0.94,0.86,0.67),// Beach
    float3(0.82,0.71,0.31),// Desert
    float3(0.31,0.71,0.24),// Grassland
    float3(0.12,0.47,0.16),// Forest
    float3(0.08,0.55,0.24),// Rainforest
    float3(0.63,0.71,0.24),// Savanna
    float3(0.20,0.47,0.39),// Taiga
    float3(0.59,0.63,0.59),// Tundra
    float3(0.94,0.94,0.98),// Snow
    float3(0.51,0.51,0.51) // Mountain
};

struct PSIn { float2 uv : TEXCOORD0; };
float4 main(PSIn In) : SV_Target {
    uint id = BiomeTex.Sample(Samp, In.uv).r;
    id = min(id, 10u);
    return float4(BIOME_COLORS[id], 1.0);
}
