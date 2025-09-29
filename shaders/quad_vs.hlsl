struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(VSIn i) {
    VSOut o; o.pos = float4(i.pos, 0, 1); o.uv = i.uv; return o;
}
