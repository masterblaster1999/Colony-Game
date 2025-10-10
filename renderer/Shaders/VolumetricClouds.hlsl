// renderer/Shaders/VolumetricClouds.hlsl
// Domain-warped volumetric clouds with weather map + lightning flashes.
// References: domain warping (iquilez); Horizon/Decima clouds (Schneider/Hillaire).

cbuffer CloudCB : register(b0)
{
    float4x4 InvViewProj;
    float3   CameraWS; float TimeSeconds;

    float3   SunDir;   float _pad0;
    float3   SunColor; float _pad1;

    float CloudBottom;     // meters (e.g., 1500)
    float CloudThickness;  // meters (e.g., 3000)

    float Coverage;   // 0..1 global coverage bias
    float Erosion;    // 0..1 edge erosion intensity
    float Density;    // global density scale

    float Storminess; // 0..1 increases darkening + lightning
}

Texture2D    WeatherMap  : register(t0);
SamplerState LinearClamp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id == 2) ? 2.0 : 0.0, (id == 1) ? 2.0 : 0.0);
    VSOut o; o.pos = float4(uv * 2.0 - 1.0, 0, 1); o.uv = uv; return o;
}

float3 ReconstructRay(float2 uv)
{
    float4 ndc = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    float4 ws  = mul(InvViewProj, ndc); ws /= ws.w;
    return normalize(ws.xyz - CameraWS);
}

// ---------------- Noise (hash-based value noise) ----------------
float3 hash33(float3 p)
{
    p = frac(p * 0.3183099 + 0.003214);
    p += dot(p, p.yzx + 19.19);
    return frac((p.xxy + p.yzz) * p.zyx);
}

float noise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f*f*(3.0 - 2.0*f);

    float n000 = dot(hash33(i + float3(0,0,0)), f - float3(0,0,0));
    float n100 = dot(hash33(i + float3(1,0,0)), f - float3(1,0,0));
    float n010 = dot(hash33(i + float3(0,1,0)), f - float3(0,1,0));
    float n110 = dot(hash33(i + float3(1,1,0)), f - float3(1,1,0));
    float n001 = dot(hash33(i + float3(0,0,1)), f - float3(0,0,1));
    float n101 = dot(hash33(i + float3(1,0,1)), f - float3(1,0,1));
    float n011 = dot(hash33(i + float3(0,1,1)), f - float3(0,1,1));
    float n111 = dot(hash33(i + float3(1,1,1)), f - float3(1,1,1));

    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    return lerp(nxy0, nxy1, u.z);
}

float fbm(float3 p)
{
    float a = 0.5, s = 0.0;
    for (int i=0; i<5; ++i)
    {
        s += a * noise3D(p);
        p = p*2.03 + 17.0;
        a *= 0.5;
    }
    return s;
}

float3 domainWarp(float3 p, float warp)
{
    float3 q = float3(fbm(p*0.3), fbm(p*0.3 + 11.7), fbm(p*0.3 + 23.4));
    return p + warp * (q*2.0 - 1.0);
}

// ---------------- Cloud shaping ----------------
struct WeatherSample { float coverage; float height; float erosion; };

WeatherSample SampleWeather(float2 uv)
{
    float4 w = WeatherMap.SampleLevel(LinearClamp, uv, 0);
    WeatherSample o;
    o.coverage = w.r;      // your authoring: R = coverage
    o.height   = w.g;      // G = cloud top modifier
    o.erosion  = w.b;      // B = erosion
    return o;
}

float Beer(float d) { return exp(-d); }

float PhaseSchlick(float mu, float k) { return (1.0 - k*k) / (4.0*3.14159*pow(1.0 + k*mu, 2.0)); }

// Cheap lightning flash signal
float LightningFlash(float t, float storm)
{
    // Hash time into pseudo-random pulses
    float base = frac(t * (1.3 + storm*4.0));
    float pulse = smoothstep(0.0, 0.02, base) * (1.0 - smoothstep(0.05, 0.15, base));
    return pulse * storm;
}

float4 PSMain(VSOut i) : SV_Target
{
    float3 ro = CameraWS;
    float3 rd = ReconstructRay(i.uv);

    // Intersect camera ray with cloud layer slab
    float3 up = normalize(float3(0,1,0));
    float cloudTop = CloudBottom + CloudThickness;

    float t0 = (CloudBottom - dot(ro, up)) / max(1e-3, dot(rd, up));
    float t1 = (cloudTop   - dot(ro, up)) / max(1e-3, dot(rd, up));
    if (t0 > t1) { float tmp = t0; t0 = t1; t1 = tmp; }
    t0 = max(t0, 0.0);
    if (t0 > t1) return float4(0,0,0,0);

    // Weather sample (screen-space uv doubled as a cheap proxy; swap for world UV)
    WeatherSample W = SampleWeather(i.uv);

    // Raymarch
    const int  STEPS = 64;
    float      t = t0;
    float      dt = max( (t1 - t0) / STEPS, 10.0 ); // step in meters
    float      transmittance = 1.0;
    float3     L = 0.0;

    // Parameters
    float warp     = lerp(0.8, 1.6, W.erosion * Erosion);
    float coverage = saturate(W.coverage*0.8 + Coverage*0.2); // mix global & map
    float kPhase   = 0.5; // forward scattering

    [loop]
    for (int s=0; s<STEPS && transmittance > 0.01; ++s)
    {
        float3 P = ro + rd * (t + 0.5*dt);

        // Map world meters to noise domain (scale down)
        float3 Np = P * 0.0007;                    // 1.4km wavelength base
        Np = domainWarp(Np, warp);

        float base     = fbm(Np);
        float details  = fbm(Np*2.5);
        float density  = saturate((base - (0.5 + (1.0-coverage)*0.2)) * 1.7);
        density        = saturate( lerp(density, density * (1.0 - details*W.erosion), Erosion) );
        density       *= Density;

        // Light integration (1 step toward sun)
        float mu = dot(rd, SunDir);
        float phase = PhaseSchlick(mu, kPhase);

        float lightAtten = 0.6 + 0.4 * saturate(base); // heuristic self-shadow
        float sigma = density * 0.08;                  // extinction coeff

        float atten = Beer(sigma * dt);
        float3 scatter = SunColor * phase * lightAtten * sigma;

        L += transmittance * scatter * dt;
        transmittance *= atten;

        t += dt;
    }

    // Storm darkening + lightning
    float stormDark = lerp(1.0, 0.6, saturate(Storminess));
    float flash     = LightningFlash(TimeSeconds, Storminess);

    float3 color = L * stormDark + flash * float3(6.0, 6.0, 7.0);
    return float4(color, 1.0 - transmittance);
}
