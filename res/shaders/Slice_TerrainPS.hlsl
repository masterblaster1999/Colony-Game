#pragma pack_matrix(row_major)

Texture2D HeightTex : register(t0);
SamplerState LinearWrap : register(s0);

cbuffer TerrainCB : register(b1)
{
    float3 LightDir;  float _pad0;     // normalized, world space
    float3 BaseColor; float HeightScale; // HeightAmplitude / TileWorld
    float2 HeightTexel; float2 _pad1;  // 1/width, 1/height
};

struct PSIn { float4 svpos : SV_POSITION; float3 wpos : TEXCOORD0; float2 uv : TEXCOORD1; };

float3 computeNormal(float2 uv)
{
    // Central differences on height field
    float hL = HeightTex.SampleLevel(LinearWrap, uv + float2(-HeightTexel.x, 0), 0).r;
    float hR = HeightTex.SampleLevel(LinearWrap, uv + float2( HeightTexel.x, 0), 0).r;
    float hD = HeightTex.SampleLevel(LinearWrap, uv + float2(0, -HeightTexel.y), 0).r;
    float hU = HeightTex.SampleLevel(LinearWrap, uv + float2(0,  HeightTexel.y), 0).r;

    float sx = (hR - hL) * HeightScale;
    float sz = (hU - hD) * HeightScale;
    return normalize(float3(-sx, 1.0f, -sz));
}

float4 main(PSIn i) : SV_TARGET
{
    float3 N = computeNormal(i.uv);
    float NdotL = saturate(dot(N, -LightDir));
    float3 col = BaseColor * (0.2 + 0.8 * NdotL);
    return float4(col, 1.0);
}
