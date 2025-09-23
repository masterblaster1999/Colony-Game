struct PSIn {
    float4 posH : SV_Position;
    float4 col  : COLOR0;
};

float4 PSMain(PSIn i) : SV_Target {
    return float4(i.col.rgb, i.col.a);
}
