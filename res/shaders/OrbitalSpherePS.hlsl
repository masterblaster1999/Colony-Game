struct PSIn {
    float4 posH : SV_Position;
    float3 nrmW : NORMAL;
    float4 col  : COLOR0;
};

float4 PSMain(PSIn i) : SV_Target {
    float NdotL = saturate(dot(i.nrmW, normalize(-float3(0.25,-0.6,0.7))));
    float ambient = 0.15;
    float diff = NdotL * 0.85;
    float3 c = i.col.rgb * (ambient + diff);
    return float4(c, 1.0);
}
