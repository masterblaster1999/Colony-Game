struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos = float4(v.pos, 0.0, 1.0);
    o.uv  = v.uv;
    return o;
}
