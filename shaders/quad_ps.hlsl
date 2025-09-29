struct PSIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 PSMain(PSIn i) : SV_Target { return float4(i.uv, 0.5, 1.0); }
