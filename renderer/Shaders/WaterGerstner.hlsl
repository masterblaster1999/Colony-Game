// renderer/Shaders/WaterGerstner.hlsl
// VS: displace with multiple Gerstner waves. PS: normals + Fresnel + flow-map warping.
// References: Tessendorf ocean notes (FFT path later), GPU Gems water overview, flow-map technique.

cbuffer WaterCB : register(b0)
{
    float4x4 World;
    float4x4 ViewProj;
    float3   EyePosWS; float TimeSeconds;

    float3   DeepColor;   float _pad0;
    float3   ShallowColor;float _pad1;

    float3   SunDir;  float _pad2;
    float3   SunColor;float _pad3;

    float2   WindDir; float Metalness; float Roughness;

    float    UseFlowMap; float FlowStrength; float _pad4; float _pad5;

    float    Amplitude[8];
    float    Wavelength[8];
    float    Speed[8];
    float    Steepness[8];
    float2   DirXY[8];
}

Texture2D    NormalMapA : register(t0);
Texture2D    NormalMapB : register(t1);
Texture2D    FlowMap    : register(t2);
SamplerState LinearWrap : register(s0);
SamplerState LinearClamp: register(s1);

#ifdef USE_SKY_ENV
TextureCube   SkyEnv    : register(t3);
#endif

struct VSIn  { float3 pos: POSITION; float3 nrm: NORMAL; float2 uv: TEXCOORD0; };
struct VSOut { float4 pos: SV_Position; float3 wsPos: TEXCOORD0; float3 wsNrm: TEXCOORD1; float2 uv: TEXCOORD2; };

float2 dirNormalize(float2 d){ return normalize(d + 1e-5); }

void ApplyGerstner(in float3 wsIn, in float2 uvIn, out float3 wsOut, out float3 nrmOut)
{
    float3 p = wsIn;
    float3 n = float3(0,1,0);

    [unroll] for (int i=0; i<8; ++i)
    {
        float A  = Amplitude[i];
        float L  = max(0.001, Wavelength[i]);
        float k  = 2.0 * 3.14159 / L;
        float2 D = dirNormalize(DirXY[i]);
        float w  = sqrt(9.81 * k); // deep water dispersion
        float c  = max(0.0, Speed[i]);
        float t  = TimeSeconds;

        float theta = k * dot(D, p.xz) - (w + c) * t;

        float Q = saturate(Steepness[i]); // steepness (<= 1)
        float cosT = cos(theta), sinT = sin(theta);

        p.x += (Q * A * D.x) * cosT;
        p.z += (Q * A * D.y) * cosT;
        p.y += A * sinT;

        // Normal from partial derivatives (approx)
        float3 Bi = float3(-D.x * Q * A * sinT * k, 1.0 - Q * A * cosT * k, -D.y * Q * A * sinT * k);
        n = normalize(n + Bi - float3(0,1,0));
    }

    wsOut  = p;
    nrmOut = normalize(n);
}

VSOut VSMain(VSIn i)
{
    VSOut o;
    float3 wsPos, wsNrm;
    float3 inWS = mul(World, float4(i.pos,1)).xyz;
    ApplyGerstner(inWS, i.uv, wsPos, wsNrm);

    o.wsPos = wsPos;
    o.wsNrm = wsNrm;
    o.uv    = i.uv;

    float4 clip = mul(ViewProj, float4(wsPos,1));
    o.pos = clip;
    return o;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float2 FlowUV(float2 uv, float2 flow, float time, float strength)
{
    // Valve-style flow map: RG = dir. Ping-pong two samples to hide stretching.
    float2 dir = normalize(flow * 2.0 - 1.0);
    float  phase = frac(time * 0.5);
    float2 uvA = uv + dir * (phase    ) * strength;
    float2 uvB = uv + dir * (phase-1.0) * strength;
    float  wA  = abs(1.0 - phase*2.0);
    return lerp(uvB, uvA, wA);
}

float3 ApplyNormalMaps(float2 uv, float3 nWS)
{
    // Optional small ripples on top
    float2 uvA = uv * 4.0;
    float2 uvB = uv * 8.0 + float2(0.1,0.3) * TimeSeconds;

    float3 n1 = NormalMapA.Sample(LinearWrap, uvA).xyz * 2.0 - 1.0;
    float3 n2 = NormalMapB.Sample(LinearWrap, uvB).xyz * 2.0 - 1.0;
    float3 tN = normalize(n1*0.6 + n2*0.4);

    // Transform tangent-space ripple into world up frame
    // (cheap: assume tangent aligned with +X,+Z)
    float3 T = normalize(float3(1,0,0));
    float3 B = normalize(cross(nWS, T)); T = cross(B, nWS);
    float3 mapped = normalize(tN.x*T + tN.y*B + tN.z*nWS);
    return normalize(lerp(nWS, mapped, 0.5));
}

float WaterDepthFade(float3 wsPos) { return saturate((wsPos.y - 0.0) * 0.02); }

float4 PSMain(VSOut i) : SV_Target
{
    // Optionally warp UVs by flow map (for rivers)
    float2 flow = FlowMap.Sample(LinearClamp, i.uv).rg;
    float2 uv   = (UseFlowMap > 0.5) ? FlowUV(i.uv, flow, TimeSeconds, FlowStrength) : i.uv;

    float3 N = ApplyNormalMaps(uv, i.wsNrm);

    float3 V = normalize(EyePosWS - i.wsPos);
    float3 L = normalize(-SunDir);
    float3 H = normalize(L + V);

    // Simple N·L + Fresnel environment
    float  NoL = saturate(dot(N, L));
    float  NoV = saturate(dot(N, V));
    float3 F0  = lerp(float3(0.02,0.02,0.02), float3(0.04,0.04,0.04), 0.5);
    float3 F   = fresnelSchlick(NoV, F0);

    float depthK = WaterDepthFade(i.wsPos);
    float3 baseColor = lerp(ShallowColor, DeepColor, depthK);

    float3 diffuse = baseColor * NoL * 0.3;

#ifdef USE_SKY_ENV
    float3 R = reflect(-V, N);
    float3 env = SkyEnv.SampleLevel(LinearClamp, R, Roughness * 6.0).rgb;
#else
    float3 env = lerp(float3(0.3,0.4,0.6), float3(0.05,0.07,0.1), depthK); // fallback
#endif

    float3 spec = env * F;

    float3 col = diffuse + spec * 1.2 + SunColor * pow(saturate(dot(N, H)), 64.0) * 0.1;

    return float4(col, 1.0);
}
