Texture2D g_NoiseTex : register(t0); // optional, can be null
SamplerState g_LinearClamp : register(s0);

struct PSIn {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    float  a   : TEXCOORD1;
};

float4 main(PSIn i) : SV_Target {
    float alpha = i.a * smoothstep(0.0, 0.1, i.uv.y) * smoothstep(1.0, 0.9, i.uv.y);
    // Simple white/gray streak; texture can add variation
    return float4(0.8, 0.85, 0.9, alpha);
}
