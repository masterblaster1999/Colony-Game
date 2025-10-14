struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut main(uint id : SV_VertexID) {
    float2 pos = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    VSOut o; 
    o.pos = float4(pos, 0.0, 1.0);
    o.uv  = 0.5 * (pos + 1.0);
    return o;
}
