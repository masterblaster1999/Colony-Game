Texture2D    gAlbedo : register(t0);
SamplerState gSamp   : register(s0);

struct PSIn { float4 Position : SV_Position; float2 Tex : TEXCOORD0; float3 Normal : NORMAL; };

float4 main(PSIn pin) : SV_Target {
    float3 n = normalize(pin.Normal);
    float3 l = normalize(float3(0.4, 0.7, 0.55));
    float  ndotl = saturate(dot(n, l));
    float4 base = gAlbedo.Sample(gSamp, pin.Tex);
    return float4(base.rgb * (0.2 + 0.8 * ndotl), base.a);
}
